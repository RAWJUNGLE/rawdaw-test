// RAWDAW native core — offline time-stretch / pitch-shift kernel.
// Wraps Signalsmith Stretch (shipped in Libs/signalsmith-stretch) for
// deterministic offline rendering. Supports a uniform time ratio + pitch shift,
// and optional warp markers (inputBeat/outputBeat pairs) that drive a piecewise
// time-varying stretch, mirroring the renderer's computeStretchSegments logic.
#pragma once
#include <vector>
#include <cstddef>

namespace rawcore {

struct StretchMarker {
    double inputBeat = 0.0;   // position in the source, in beats
    double outputBeat = 0.0;  // position in the rendered output, in beats
};

struct StretchParams {
    double sampleRate = 44100.0;
    double timeRatio = 1.0;        // output/input length when no markers given
    double semitones = 0.0;        // pitch shift
    double secondsPerBeat = 0.5;   // tempo context for marker beats (120 BPM)
    std::vector<StretchMarker> markers; // sorted by outputBeat; >=2 to warp
};

struct StretchResult {
    std::vector<std::vector<float>> channels; // de-interleaved output
    size_t frames = 0;
};

// Render `frames` samples (per channel) of input through the stretcher.
StretchResult stretchOffline(const float* const* channels, int nChannels,
                             size_t frames, const StretchParams& params);

} // namespace rawcore
