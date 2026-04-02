#pragma once

#include "audio/effect.h"
#include "audio/dsp/envelope_follower.h"

namespace GuitarAmp {

class NoiseGate : public Effect {
public:
    NoiseGate();
    void process(float* buffer, int num_samples) override;
    void reset() override;
    const char* name() const override { return "Noise Gate"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;
    EnvelopeFollower env_;
    float gain_ = 0.0f;
};

} // namespace GuitarAmp
