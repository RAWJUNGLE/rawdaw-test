// RAWDAW native core — BPM detection kernel
// Wraps the BeatDetektor (CubicFX) algorithm shipped in Libs/beatdetektor-audiolab.
// Faithfully replicates the reference FFT -> collapse -> detect pipeline from
// the project's main.cpp, but driven offline from an in-memory audio buffer
// instead of a live OpenAL capture device.
#pragma once
#include <vector>
#include <cstddef>

namespace rawcore {

struct BpmResult {
    double bpm = 0.0;          // best estimate (one decimal precision from contest)
    double bpmRaw = 0.0;       // running estimate from the single detector
    double confidence = 0.0;   // 0..1, derived from contest fill ratio
    std::vector<double> beats; // estimated beat times (seconds) across the buffer
};

// Detect tempo from de-interleaved mono/stereo PCM.
// channels: pointer array, each of `frames` length, values in [-1, 1].
// nChannels >= 1 (extra channels are averaged to mono internally).
BpmResult detectBpm(const float* const* channels,
                    int nChannels,
                    size_t frames,
                    double sampleRate,
                    double bpmMin = 80.0,
                    double bpmMax = 180.0);

} // namespace rawcore
