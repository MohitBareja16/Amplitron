#pragma once

#include "audio/effect.h"
#include "audio/dsp/envelope_follower.h"

namespace GuitarAmp {

class Compressor : public Effect {
public:
    Compressor();
    void process(float* buffer, int num_samples) override;
    void reset() override;
    const char* name() const override { return "Compressor"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;
    EnvelopeFollower env_;
    float smoothed_attack_ms_ = 5.0f;    // matches default param
    float smoothed_release_ms_ = 100.0f; // matches default param
};

} // namespace GuitarAmp
