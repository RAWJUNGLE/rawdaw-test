// Standalone validation harness for the BPM kernel (no Node/Electron required).
// Synthesises kick-drum click tracks at known tempos and checks the detector.
#include "../src/bpm.h"
#include <cstdio>
#include <cmath>
#include <vector>

// Render a clean kick drum (sharp click + decaying body) on each beat.
static std::vector<float> makeClickTrack(double bpm, double seconds, double sr) {
    size_t n = (size_t)(seconds * sr);
    std::vector<float> buf(n, 0.0f);
    double secPerBeat = 60.0 / bpm;
    for (double t = 0.0; t < seconds; t += secPerBeat) {
        size_t start = (size_t)(t * sr);
        size_t dur = (size_t)(0.18 * sr);
        for (size_t i = 0; i < dur && start + i < n; ++i) {
            double tt = (double)i / sr;
            double clickEnv = std::exp(-tt / 0.002);   // ~2 ms sharp attack
            double bodyEnv = std::exp(-tt / 0.045);     // ~45 ms body
            double freq = 55.0 + 90.0 * bodyEnv;        // pitch drop
            double s = bodyEnv * std::sin(2.0 * M_PI * freq * tt) * 0.85
                     + clickEnv * 0.6;
            buf[start + i] += (float)s;
        }
    }
    float peak = 1e-6f;
    for (float v : buf) peak = std::max(peak, std::fabs(v));
    for (float& v : buf) v /= peak;
    return buf;
}

static int testOne(double bpm, double sr) {
    auto mono = makeClickTrack(bpm, 16.0, sr);
    const float* chans[1] = { mono.data() };
    auto r = rawcore::detectBpm(chans, 1, mono.size(), sr, 80.0, 180.0);

    // Accept exact, half, or double tempo (octave errors are inherent to
    // tempo estimation and the host UI resolves them).
    double cands[3] = { r.bpm, r.bpm * 2.0, r.bpm / 2.0 };
    double bestErr = 1e9;
    for (double c : cands) bestErr = std::min(bestErr, std::fabs(c - bpm));

    bool ok = bestErr <= 2.0;
    printf("  truth=%6.1f  detected=%6.2f  conf=%.2f  beats=%zu  -> %s\n",
           bpm, r.bpm, r.confidence, r.beats.size(), ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

int main() {
    printf("BPM kernel validation (BeatDetektor, offline driver)\n");
    int fails = 0;
    for (double sr : { 44100.0, 48000.0 }) {
        printf("sampleRate=%.0f\n", sr);
        for (double bpm : { 90.0, 100.0, 120.0, 128.0, 140.0, 174.0 })
            fails += testOne(bpm, sr);
    }
    printf("%s\n", fails == 0 ? "ALL PASSED" : "SOME FAILED");
    return fails == 0 ? 0 : 1;
}
