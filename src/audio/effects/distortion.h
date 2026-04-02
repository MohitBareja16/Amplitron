#pragma once

#include "audio/effect.h"
#include "audio/dsp/biquad.h"

namespace GuitarAmp {

class Distortion : public Effect {
public:
    Distortion();
    void process(float* buffer, int num_samples) override;
    void reset() override;
    const char* name() const override { return "Distortion"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;
    OnePole tone_lp_;

    // One-pole smoothing states (avoids zipper noise on parameter changes)
    float drive_smoothed_ = 2.0f;
    float tone_smoothed_  = 0.6f;
    float level_smoothed_ = 0.5f;
};

} // namespace GuitarAmp
