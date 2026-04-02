#pragma once

#include "audio/effect.h"
#include "audio/dsp/envelope_follower.h"

namespace GuitarAmp {

class WahPedal : public Effect {
public:
    WahPedal();
    void process(float* buffer, int num_samples) override;
    void reset() override;
    const char* name() const override { return "Wah"; }
    std::vector<EffectParam>& params() override { return params_; }

private:
    std::vector<EffectParam> params_;

    // State-variable filter (Chamberlin topology) state
    float svf_lp_ = 0.0f;
    float svf_bp_ = 0.0f;

    // Envelope follower state (auto-wah)
    EnvelopeFollower env_;

    // Smoothed sweep position (avoids zipper noise)
    float sweep_smooth_ = 0.5f;

    // Smoothed Q / resonance (avoids zipper noise on knob moves)
    float q_smooth_ = 3.5f;

    // Cached parameter snapshot used when params_mutex cannot be acquired.
    // Initialised to the same defaults as the params_ vector.
    bool  cached_is_auto_  = false;
    float cached_sweep_    = 0.5f;
    float cached_q_        = 3.5f;
    float cached_sens_     = 0.5f;
    float cached_atk_ms_   = 5.0f;
    float cached_rel_ms_   = 100.0f;
};

} // namespace GuitarAmp
