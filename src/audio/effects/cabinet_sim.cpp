#include "audio/effects/cabinet_sim.h"
#include "audio/effect_factory.h"

namespace GuitarAmp {

static EffectRegistrar<CabinetSim> reg("Cabinet");

CabinetSim::CabinetSim() {
    params_ = {
        {"Type",    0.0f, 0.0f, 2.0f, 0.0f, "", "Speaker cabinet type. 0 = 1x12 (bright/focused), 1 = 2x12 (balanced), 2 = 4x12 (huge low-end)."},
        {"Bright",  0.5f, 0.0f, 1.0f, 0.5f, "", "Simulates microphone placement. Higher values add a high-frequency resonance peak for more cut."},
    };

    // Default LP at ~5kHz (speaker rolloff)
    lp_.b0 = 0.067455f; lp_.b1 = 0.134911f; lp_.b2 = 0.067455f;
    lp_.a1 = -1.14298f; lp_.a2 = 0.41280f;

    // Default HP at ~80Hz (low cut)
    hp_.b0 = 0.9565f; hp_.b1 = -1.9131f; hp_.b2 = 0.9565f;
    hp_.a1 = -1.9112f; hp_.a2 = 0.9150f;

    // Resonance peak ~2kHz
    peak_.b0 = 1.05f; peak_.b1 = -1.65f; peak_.b2 = 0.65f;
    peak_.a1 = -1.65f; peak_.a2 = 0.70f;
}

void CabinetSim::process(float* buffer, int num_samples) {
    if (!enabled_) return;

    float bright = params_[1].value;

    for (int i = 0; i < num_samples; ++i) {
        float dry = buffer[i];
        float x = buffer[i];

        x = hp_.process(x);
        x = lp_.process(x);

        // Blend in resonance based on brightness
        float peaked = peak_.process(x);
        x = x * (1.0f - bright * 0.3f) + peaked * bright * 0.3f;

        buffer[i] = dry * (1.0f - mix_) + x * mix_;
    }
}

void CabinetSim::reset() {
    lp_.reset();
    hp_.reset();
    peak_.reset();
}

} // namespace GuitarAmp
