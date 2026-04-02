#pragma once

#include "audio/effect.h"
#include "audio/dsp/biquad.h"

namespace GuitarAmp {

class CabinetSim : public Effect {
public:
    CabinetSim();
    void process(float* buffer, int num_samples) override;
    void reset() override;
    const char* name() const override { return "Cabinet"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;

    Biquad lp_;   // speaker rolloff
    Biquad hp_;   // low cut
    Biquad peak_; // resonance bump
};

} // namespace GuitarAmp
