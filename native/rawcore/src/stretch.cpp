// RAWDAW native core — offline time-stretch / pitch-shift implementation.
#include "stretch.h"

// The vendored signalsmith fft.h uses std::memcpy without including <cstring>
// itself; provide it first so the header compiles on strict toolchains without
// modifying the vendored sources.
#include <cstring>
#include "../vendor/signalsmith-stretch/signalsmith-stretch.h"

#include <algorithm>
#include <cmath>

namespace rawcore {

using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

// Build a per-channel pointer view that Signalsmith can index as inputs[c][i].
namespace {
struct PtrView {
    std::vector<const float*> ch;
    const float* operator[](int c) const { return ch[c]; }
};
struct OutView {
    std::vector<float*> ch;
    float* operator[](int c) const { return ch[c]; }
};
} // namespace

// Convert marker beats to (inputSample, outputSample) breakpoints.
static std::vector<std::pair<double, double>>
breakpoints(const StretchParams& p, size_t inFrames) {
    std::vector<std::pair<double, double>> bp;
    double spb = p.secondsPerBeat > 0 ? p.secondsPerBeat : 0.5;
    for (const auto& m : p.markers) {
        double inS = m.inputBeat * spb * p.sampleRate;
        double outS = m.outputBeat * spb * p.sampleRate;
        bp.emplace_back(inS, outS);
    }
    std::sort(bp.begin(), bp.end(),
              [](auto& a, auto& b) { return a.second < b.second; });
    // anchor start/end so the whole buffer is covered
    if (bp.empty() || bp.front().second > 0.0)
        bp.insert(bp.begin(), { 0.0, 0.0 });
    double lastIn = bp.back().first, lastOut = bp.back().second;
    if (lastIn < (double)inFrames) {
        double remIn = (double)inFrames - lastIn;
        // extend output proportionally to the last local rate (or 1:1)
        bp.emplace_back((double)inFrames, lastOut + remIn);
    }
    return bp;
}

StretchResult stretchOffline(const float* const* channels, int nChannels,
                             size_t frames, const StretchParams& params) {
    StretchResult result;
    if (!channels || nChannels <= 0 || frames == 0) return result;

    Stretch stretch;
    stretch.presetDefault(nChannels, (float)params.sampleRate);
    stretch.setTransposeSemitones((float)params.semitones);

    PtrView in;
    for (int c = 0; c < nChannels; ++c) in.ch.push_back(channels[c]);

    // ---- uniform stretch (no markers) --------------------------------------
    if (params.markers.size() < 2) {
        double ratio = params.timeRatio > 0 ? params.timeRatio : 1.0;
        size_t outFrames = (size_t)std::llround((double)frames * ratio);
        if (outFrames == 0) outFrames = 1;

        result.channels.assign(nChannels, std::vector<float>(outFrames, 0.0f));
        OutView out;
        for (int c = 0; c < nChannels; ++c) out.ch.push_back(result.channels[c].data());

        stretch.exact(in, (int)frames, out, (int)outFrames);
        result.frames = outFrames;
        return result;
    }

    // ---- warp-marker stretch (piecewise, time-varying) ---------------------
    auto bp = breakpoints(params, frames);
    size_t outFrames = (size_t)std::llround(bp.back().second);
    if (outFrames == 0) outFrames = 1;

    result.channels.assign(nChannels, std::vector<float>(outFrames, 0.0f));

    // Process each segment with its own local playback rate. We feed the
    // stretcher contiguous input ranges and write contiguous output ranges,
    // re-basing the channel pointers per segment via offset views.
    stretch.reset();
    size_t outCursor = 0;
    for (size_t s = 0; s + 1 < bp.size(); ++s) {
        double inA = bp[s].first,  inB = bp[s + 1].first;
        double outA = bp[s].second, outB = bp[s + 1].second;
        int segIn = (int)std::llround(inB - inA);
        int segOut = (int)std::llround(outB - outA);
        if (segIn <= 0 || segOut <= 0) continue;
        if ((size_t)(inA) + segIn > frames) segIn = (int)(frames - (size_t)inA);
        if (outCursor + segOut > outFrames) segOut = (int)(outFrames - outCursor);
        if (segIn <= 0 || segOut <= 0) continue;

        PtrView segInView;
        for (int c = 0; c < nChannels; ++c)
            segInView.ch.push_back(channels[c] + (size_t)inA);
        OutView segOutView;
        for (int c = 0; c < nChannels; ++c)
            segOutView.ch.push_back(result.channels[c].data() + outCursor);

        // process() keeps internal continuity across calls; use it per segment
        stretch.process(segInView, segIn, segOutView, segOut);
        outCursor += segOut;
    }

    result.frames = outFrames;
    return result;
}

} // namespace rawcore
