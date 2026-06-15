// RAWDAW native core — BPM detection implementation.
//
// Strategy (validated against synthetic click tracks at 90..174 BPM,
// 44.1k & 48k, exact octave + sub-BPM precision):
//
//   1. PRIMARY estimator — onset-envelope spectral-flux autocorrelation.
//      This is authoritative for the returned `bpm`. It is robust on real
//      broadband material and does not require the long convergence window
//      that BeatDetektor's contest needs.
//   2. CROSS-CHECK — BeatDetektor (CubicFX), driven exactly like the project's
//      reference main.cpp (FFT -> collapse -> process -> contest -> run()).
//      Reported as `bpmRaw`; only nudges confidence when it agrees.
//   3. Beat phase — comb cross-correlation of the onset envelope at the chosen
//      period, emitted as `beats[]` over the real signal span.
#include "bpm.h"

// The vendored FFT.h uses M_PI and unqualified sin() inside templates but does
// not include <cmath> itself; pull these in first so it compiles portably on
// strict toolchains (MSVC/libc++/libstdc++) without modifying the vendored file.
#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using std::sin;

#include "../vendor/beatdetektor/FFT.h"
#include "../vendor/beatdetektor/BeatDetektor.h"

#include <algorithm>
#include <vector>
#include <utility>

namespace rawcore {

// STFT geometry for the onset envelope. 1024 real samples are treated as 512
// complex points (interleaved re/im) by the radix-2 DanielsonLanczos transform,
// matching the packing the vendored FFT.h expects.
static constexpr int WIN = 1024;
static constexpr int HOP = 512;
static constexpr int kBuf = 1024;   // BeatDetektor reference frame size

// ---- mono downmix -----------------------------------------------------------
static std::vector<float> downmix(const float* const* channels, int nChannels,
                                  size_t frames) {
    std::vector<float> mono(frames, 0.0f);
    if (nChannels <= 0) return mono;
    const double inv = 1.0 / nChannels;
    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        for (int c = 0; c < nChannels; ++c) acc += channels[c][i];
        mono[i] = (float)(acc * inv);
    }
    return mono;
}

// ---- onset envelope via half-wave-rectified spectral flux -------------------
static std::vector<double> onsetEnvelope(const std::vector<float>& x,
                                         double sampleRate, double& envRate) {
    size_t nf = (x.size() >= WIN) ? (x.size() - WIN) / HOP + 1 : 0;
    std::vector<double> env;
    env.reserve(nf);

    std::vector<float> fd(WIN);
    std::vector<double> prevMag(WIN / 4, 0.0);
    DanielsonLanczos<(WIN / 2), float> dl;

    double han[WIN];
    for (int i = 0; i < WIN; ++i) han[i] = 0.5 * (1 - cos(2 * M_PI * i / (WIN - 1)));

    for (size_t f = 0; f < nf; ++f) {
        size_t base = f * HOP;
        for (int i = 0; i < WIN; ++i) fd[i] = (float)(x[base + i] * han[i]);

        // bit-reversal reindex (radix-2, in-place) before the butterfly pass
        unsigned long n = (unsigned long)(WIN / 2) << 1, j = 1, m, i;
        for (i = 1; i < n; i += 2) {
            if (j > i) { std::swap(fd[j - 1], fd[i - 1]); std::swap(fd[j], fd[i]); }
            m = WIN / 2;
            while (m >= 2 && j > m) { j -= m; m >>= 1; }
            j += m;
        }
        dl.apply(&fd[0]);

        // positive spectral flux over the lower quarter of the spectrum
        double flux = 0.0;
        for (int k = 0; k < WIN / 4; ++k) {
            double re = fd[2 * k], im = fd[2 * k + 1];
            double mag = std::sqrt(re * re + im * im);
            double d = mag - prevMag[k];
            if (d > 0) flux += d;
            prevMag[k] = mag;
        }
        env.push_back(flux);
    }

    // mean-subtract + half-wave rectify to suppress sustained energy
    double mean = 0.0;
    for (double v : env) mean += v;
    mean /= std::max((size_t)1, env.size());
    for (double& v : env) { v -= mean; if (v < 0) v = 0; }

    envRate = sampleRate / HOP;
    return env;
}

// ---- autocorrelation tempo with harmonic-sum fundamental preference ---------
// Returns chosen lag (in envelope frames, fractional) and a sharpness score.
static double acfTempo(const std::vector<double>& env, double envRate,
                       double bpmMin, double bpmMax,
                       double& outLag, double& outSharp) {
    int lagMin = (int)std::floor(60.0 / bpmMax * envRate);
    int lagMax = (int)std::ceil(60.0 / bpmMin * envRate);
    lagMin = std::max(1, lagMin);
    lagMax = std::min((int)env.size() - 1, lagMax);
    if (lagMax <= lagMin) { outLag = 0; outSharp = 0; return 0; }

    std::vector<double> acf(lagMax + 1, 0.0);
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        double s = 0.0;
        for (size_t i = lag; i < env.size(); ++i) s += env[i] * env[i - lag];
        acf[lag] = s;
    }

    // Prefer the fundamental by rewarding lags whose 2x/3x multiples also peak.
    double best = -1.0;
    int bestLag = lagMin;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        double s = acf[lag];
        if (2 * lag < (int)acf.size()) s += 0.5 * acf[2 * lag];
        if (3 * lag < (int)acf.size()) s += 0.33 * acf[3 * lag];
        if (s > best) { best = s; bestLag = lag; }
    }

    // parabolic interpolation for sub-frame (sub-BPM) precision
    double y0 = acf[std::max(1, bestLag - 1)];
    double y1 = acf[bestLag];
    double y2 = acf[std::min((int)acf.size() - 1, bestLag + 1)];
    double denom = (y0 - 2 * y1 + y2);
    double delta = denom != 0 ? 0.5 * (y0 - y2) / denom : 0.0;
    double refLag = bestLag + delta;
    if (refLag < 1) refLag = bestLag;

    // sharpness: peak height vs local mean of the raw acf, normalised 0..1
    double localMean = 0.0;
    int cnt = 0;
    for (int lag = lagMin; lag <= lagMax; ++lag) { localMean += acf[lag]; ++cnt; }
    localMean /= std::max(1, cnt);
    double sharp = (y1 > 0 && localMean > 0) ? (y1 - localMean) / (y1 + localMean)
                                             : 0.0;
    outLag = refLag;
    outSharp = std::max(0.0, std::min(1.0, sharp));
    return 60.0 * envRate / refLag;
}

// ---- beat-phase reconstruction via comb cross-correlation -------------------
static std::vector<double> reconstructBeats(const std::vector<double>& env,
                                            double envRate, double periodFrames,
                                            double totalSeconds) {
    std::vector<double> beats;
    if (periodFrames < 1.0 || env.empty()) return beats;

    // Find the phase offset (0..period) whose comb best aligns with onsets.
    int P = (int)std::round(periodFrames);
    if (P < 1) return beats;
    double bestScore = -1.0;
    int bestPhase = 0;
    for (int phase = 0; phase < P; ++phase) {
        double score = 0.0;
        for (double pos = phase; pos < (double)env.size(); pos += periodFrames)
            score += env[(size_t)pos];
        if (score > bestScore) { bestScore = score; bestPhase = phase; }
    }

    for (double pos = bestPhase; pos < (double)env.size(); pos += periodFrames) {
        double tSec = pos / envRate;
        if (tSec <= totalSeconds) beats.push_back(tSec);
    }
    return beats;
}

// ---- BeatDetektor cross-check (reference pipeline) --------------------------
// Returns BPM estimate or 0 if the contest never converged.
static double beatDetektorCrossCheck(const std::vector<float>& monoIn,
                                     double sampleRate,
                                     double bpmMin, double bpmMax) {
    // The reference contest constructor constrains BPM_MAX <= BPM_MIN*2-1
    // (a single octave). Clamp the requested range into one octave centred on
    // the user range so the contest can actually lock.
    float bdMin = (float)bpmMin;
    float bdMax = (float)std::min(bpmMax, bpmMin * 2.0 - 1.0);
    if (bdMax <= bdMin) bdMax = bdMin + 1.0f;

    // Loop short material up to >= 40 s so the contest has time to converge.
    std::vector<float> mono = monoIn;
    double haveSec = (double)mono.size() / sampleRate;
    if (haveSec < 40.0 && !monoIn.empty()) {
        size_t target = (size_t)(40.0 * sampleRate);
        mono.reserve(target);
        while (mono.size() < target)
            mono.insert(mono.end(), monoIn.begin(),
                        monoIn.begin() + std::min(monoIn.size(),
                                                  target - mono.size()));
    }

    BeatDetektor det(bdMin, bdMax);
    BeatDetektorContest contest;

    DanielsonLanczos<(kBuf / 2), float> dl;
    std::vector<float> fd(kBuf);
    size_t nf = (mono.size() >= (size_t)kBuf) ? (mono.size() / kBuf) : 0;

    std::vector<float> collapse(kBuf, 0.0f);
    for (size_t f = 0; f < nf; ++f) {
        size_t base = f * kBuf;
        for (int i = 0; i < kBuf; ++i) fd[i] = mono[base + i];

        unsigned long n = (unsigned long)(kBuf / 2) << 1, j = 1, m, i;
        for (i = 1; i < n; i += 2) {
            if (j > i) { std::swap(fd[j - 1], fd[i - 1]); std::swap(fd[j], fd[i]); }
            m = kBuf / 2;
            while (m >= 2 && j > m) { j -= m; m >>= 1; }
            j += m;
        }
        dl.apply(&fd[0]);

        // collapse to the first 256 + last 256 interleaved bins (reference)
        for (int k = 0; k < 256; ++k) collapse[k] = fd[k];
        for (int k = 0; k < 256; ++k) collapse[256 + k] = fd[kBuf - 256 + k];

        double timer = (double)(base + kBuf) / sampleRate;
        det.process(timer, collapse);
        contest.process(timer, &det);
        contest.run();   // CRITICAL: computes win_bpm_int; process() alone won't
    }

    if (contest.win_bpm_int) return (double)contest.win_bpm_int / 10.0;
    return 0.0;
}

// ---- public entry -----------------------------------------------------------
BpmResult detectBpm(const float* const* channels, int nChannels, size_t frames,
                    double sampleRate, double bpmMin, double bpmMax) {
    BpmResult result;
    if (!channels || nChannels <= 0 || frames == 0 || sampleRate <= 0)
        return result;

    std::vector<float> mono = downmix(channels, nChannels, frames);
    double totalSeconds = (double)frames / sampleRate;

    // 1. PRIMARY: onset-envelope autocorrelation (authoritative)
    double envRate = 0.0;
    std::vector<double> env = onsetEnvelope(mono, sampleRate, envRate);

    double lagFrames = 0.0, sharp = 0.0;
    double acfBpm = acfTempo(env, envRate, bpmMin, bpmMax, lagFrames, sharp);

    result.bpm = acfBpm;
    result.confidence = sharp;

    // 2. CROSS-CHECK: BeatDetektor
    double bdBpm = beatDetektorCrossCheck(mono, sampleRate, bpmMin, bpmMax);
    result.bpmRaw = bdBpm;

    // agreement within an octave nudges confidence up
    if (acfBpm > 0 && bdBpm > 0) {
        double ratios[3] = { bdBpm / acfBpm, bdBpm / (acfBpm * 2.0),
                             bdBpm / (acfBpm * 0.5) };
        for (double r : ratios) {
            if (r > 0.96 && r < 1.04) {
                result.confidence = std::min(1.0, result.confidence + 0.15);
                break;
            }
        }
    }

    // 3. Beat phase from the chosen ACF period
    if (lagFrames >= 1.0)
        result.beats = reconstructBeats(env, envRate, lagFrames, totalSeconds);

    return result;
}

} // namespace rawcore
