#pragma once

#include "common.h"

namespace GuitarAmp {

struct EffectParam {
    std::string name;
    float value;
    float min_val;
    float max_val;
    float default_val;
    std::string unit;
    std::string tooltip;
};

class Effect {
public:
    virtual ~Effect() = default;

    virtual void process(float* buffer, int num_samples) = 0;
    virtual void set_sample_rate(int sample_rate) { sample_rate_ = sample_rate; }
    virtual void reset() = 0;

    virtual const char* name() const = 0;
    virtual std::vector<EffectParam>& params() = 0;

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

    void set_mix(float mix) { mix_ = clamp(mix, 0.0f, 1.0f); }
    float get_mix() const { return mix_; }

    // Protects params() value fields from concurrent UI writes and audio reads.
    // UI thread: std::lock_guard when writing any EffectParam::value.
    // Audio thread: std::mutex::try_lock inside process(); fall back to cached
    // values if the lock is held by the UI.
    std::mutex params_mutex;

protected:
    int sample_rate_ = DEFAULT_SAMPLE_RATE;
    bool enabled_ = true;
    float mix_ = 1.0f;

    // Wet/dry mix helper
    void apply_mix(const float* dry, float* wet, int num_samples) {
        if (mix_ >= 1.0f) return;
        for (int i = 0; i < num_samples; ++i) {
            wet[i] = dry[i] * (1.0f - mix_) + wet[i] * mix_;
        }
    }
};

} // namespace GuitarAmp
