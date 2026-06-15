// RAWDAW native core — N-API surface.
// Exposes the C++ kernels to the Electron main process. The renderer reaches
// these through preload's contextBridge (window.RawCore.*), never directly.
//
// detectBpm(channels, sampleRate, [opts]) -> { bpm, bpmRaw, confidence, beats }
//   channels:   Array<Float32Array>  (one per channel, equal length) OR a
//               single Float32Array (treated as mono).
//   sampleRate: number
//   opts:       { bpmMin?: number = 80, bpmMax?: number = 180 }
//
// stretchOffline(...) is declared in stretch.h and registered here too when the
// stretch kernel is compiled in (guarded by RAWCORE_WITH_STRETCH).
#include <napi.h>
#include <vector>
#include "bpm.h"
#ifdef RAWCORE_WITH_STRETCH
#include "stretch.h"
#endif

namespace {

// Pull a Float32Array (or coerce a numeric array) into a contiguous float vec.
static bool toFloatVector(const Napi::Value& v, std::vector<float>& out) {
    if (v.IsTypedArray()) {
        Napi::TypedArray ta = v.As<Napi::TypedArray>();
        if (ta.TypedArrayType() != napi_float32_array) return false;
        Napi::Float32Array f = v.As<Napi::Float32Array>();
        out.assign(f.Data(), f.Data() + f.ElementLength());
        return true;
    }
    if (v.IsArray()) {
        Napi::Array a = v.As<Napi::Array>();
        out.resize(a.Length());
        for (uint32_t i = 0; i < a.Length(); ++i)
            out[i] = a.Get(i).ToNumber().FloatValue();
        return true;
    }
    return false;
}

Napi::Value DetectBpm(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "detectBpm(channels, sampleRate, [opts])")
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    // Collect channels into owning vectors, then build a pointer table.
    std::vector<std::vector<float>> chanData;
    if (info[0].IsArray() &&
        info[0].As<Napi::Array>().Length() > 0 &&
        info[0].As<Napi::Array>().Get((uint32_t)0).IsTypedArray()) {
        Napi::Array arr = info[0].As<Napi::Array>();
        chanData.resize(arr.Length());
        for (uint32_t c = 0; c < arr.Length(); ++c) {
            if (!toFloatVector(arr.Get(c), chanData[c])) {
                Napi::TypeError::New(env, "channels must be Float32Array[]")
                    .ThrowAsJavaScriptException();
                return env.Null();
            }
        }
    } else {
        chanData.resize(1);
        if (!toFloatVector(info[0], chanData[0])) {
            Napi::TypeError::New(env, "channels must be a Float32Array")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
    }

    double sampleRate = info[1].ToNumber().DoubleValue();
    double bpmMin = 80.0, bpmMax = 180.0;
    if (info.Length() >= 3 && info[2].IsObject()) {
        Napi::Object o = info[2].As<Napi::Object>();
        if (o.Has("bpmMin")) bpmMin = o.Get("bpmMin").ToNumber().DoubleValue();
        if (o.Has("bpmMax")) bpmMax = o.Get("bpmMax").ToNumber().DoubleValue();
    }

    // equalise channel lengths to the shortest (defensive)
    size_t frames = chanData.empty() ? 0 : chanData[0].size();
    for (auto& c : chanData) frames = std::min(frames, c.size());

    std::vector<const float*> ptrs;
    ptrs.reserve(chanData.size());
    for (auto& c : chanData) ptrs.push_back(c.data());

    rawcore::BpmResult r = rawcore::detectBpm(
        ptrs.data(), (int)ptrs.size(), frames, sampleRate, bpmMin, bpmMax);

    Napi::Object out = Napi::Object::New(env);
    out.Set("bpm", Napi::Number::New(env, r.bpm));
    out.Set("bpmRaw", Napi::Number::New(env, r.bpmRaw));
    out.Set("confidence", Napi::Number::New(env, r.confidence));
    Napi::Array beats = Napi::Array::New(env, r.beats.size());
    for (size_t i = 0; i < r.beats.size(); ++i)
        beats.Set((uint32_t)i, Napi::Number::New(env, r.beats[i]));
    out.Set("beats", beats);
    return out;
}

#ifdef RAWCORE_WITH_STRETCH
Napi::Value StretchOffline(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "stretchOffline(channels, opts)")
            .ThrowAsJavaScriptException();
        return env.Null();
    }
    std::vector<std::vector<float>> chanData;
    Napi::Array arr = info[0].As<Napi::Array>();
    chanData.resize(arr.Length());
    for (uint32_t c = 0; c < arr.Length(); ++c)
        toFloatVector(arr.Get(c), chanData[c]);

    Napi::Object o = info[1].As<Napi::Object>();
    rawcore::StretchParams p;
    p.sampleRate    = o.Get("sampleRate").ToNumber().DoubleValue();
    p.timeRatio     = o.Has("timeRatio") ? o.Get("timeRatio").ToNumber().DoubleValue() : 1.0;
    p.semitones     = o.Has("semitones") ? o.Get("semitones").ToNumber().DoubleValue() : 0.0;
    if (o.Has("markers") && o.Get("markers").IsArray()) {
        Napi::Array m = o.Get("markers").As<Napi::Array>();
        for (uint32_t i = 0; i < m.Length(); ++i) {
            Napi::Object mk = m.Get(i).As<Napi::Object>();
            rawcore::StretchMarker sm;
            sm.inputBeat  = mk.Get("inputBeat").ToNumber().DoubleValue();
            sm.outputBeat = mk.Get("outputBeat").ToNumber().DoubleValue();
            p.markers.push_back(sm);
        }
    }
    if (o.Has("secondsPerBeat"))
        p.secondsPerBeat = o.Get("secondsPerBeat").ToNumber().DoubleValue();

    size_t frames = chanData.empty() ? 0 : chanData[0].size();
    std::vector<const float*> ptrs;
    for (auto& c : chanData) ptrs.push_back(c.data());

    rawcore::StretchResult sr = rawcore::stretchOffline(
        ptrs.data(), (int)ptrs.size(), frames, p);

    Napi::Array outCh = Napi::Array::New(env, sr.channels.size());
    for (size_t c = 0; c < sr.channels.size(); ++c) {
        Napi::Float32Array fa = Napi::Float32Array::New(env, sr.channels[c].size());
        std::copy(sr.channels[c].begin(), sr.channels[c].end(), fa.Data());
        outCh.Set((uint32_t)c, fa);
    }
    Napi::Object out = Napi::Object::New(env);
    out.Set("channels", outCh);
    out.Set("frames", Napi::Number::New(env, (double)sr.frames));
    return out;
}
#endif

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("detectBpm", Napi::Function::New(env, DetectBpm));
#ifdef RAWCORE_WITH_STRETCH
    exports.Set("stretchOffline", Napi::Function::New(env, StretchOffline));
#endif
    return exports;
}

} // namespace

NODE_API_MODULE(rawcore, Init)
