#pragma once

#include "audio/effect.h"
#include "audio/dsp/biquad.h"

namespace GuitarAmp {

class Delay : public Effect {
public:
    Delay();
    void process(float* buffer, int num_samples) override;
    void set_sample_rate(int sample_rate) override;
    void reset() override;
    const char* name() const override { return "Delay"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;
    std::vector<float> delay_buffer_;
    int write_pos_ = 0;
    int max_delay_samples_ = 0;
    OnePole tone_lp_;

    // One-pole smoothed parameter states
    float smoothed_time_ms_ = 350.0f;
    float smoothed_feedback_ = 0.4f;
    float smoothed_tone_ = 0.7f;
    float smoothed_level_ = 0.5f;
};

} // namespace GuitarAmp
