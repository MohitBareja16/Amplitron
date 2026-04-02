#include "audio/effects/reverb.h"
#include "audio/effect_factory.h"

namespace GuitarAmp {

static EffectRegistrar<Reverb> reg("Reverb");

// Comb filter delay lengths (in samples at 44100Hz, will be scaled)
static const int COMB_LENGTHS[] = {1116, 1188, 1277, 1356};
static const int ALLPASS_LENGTHS[] = {556, 441};

Reverb::Reverb() {
    params_ = {
        {"Decay",  0.6f, 0.1f, 0.99f, 0.6f, "", "Length of the reverb tail. Higher values simulate larger acoustic spaces like halls or caves."},
        {"Damp",   0.4f, 0.0f, 1.0f,  0.4f, "", "High-frequency damping. Higher values absorb treble faster, simulating softer room materials."},
        {"Level",  0.3f, 0.0f, 1.0f,  0.3f, "", "Mix volume of the reverb effect. Controls how wet or distant the overall sound feels."},
    };
    init_filters();
}

void Reverb::set_sample_rate(int sample_rate) {
    Effect::set_sample_rate(sample_rate);
    init_filters();
}

void Reverb::init_filters() {
    float scale = static_cast<float>(sample_rate_) / 44100.0f;

    for (int i = 0; i < NUM_COMBS; ++i) {
        int len = static_cast<int>(COMB_LENGTHS[i] * scale);
        combs_[i].buffer.assign(len, 0.0f);
        combs_[i].write_pos = 0;
        combs_[i].lp_state = 0.0f;
    }

    for (int i = 0; i < NUM_ALLPASS; ++i) {
        int len = static_cast<int>(ALLPASS_LENGTHS[i] * scale);
        allpasses_[i].buffer.assign(len, 0.0f);
        allpasses_[i].write_pos = 0;
        allpasses_[i].feedback = 0.5f;
    }
}

void Reverb::process(float* buffer, int num_samples) {
    if (!enabled_) return;

    float decay = params_[0].value;
    float damp = params_[1].value;
    float level = params_[2].value;

    for (int i = 0; i < num_samples; ++i) {
        float dry = buffer[i];
        float input = buffer[i] * 0.2f;
        float out = 0.0f;

        // Parallel comb filters
        for (int c = 0; c < NUM_COMBS; ++c) {
            auto& comb = combs_[c];
            int buf_len = static_cast<int>(comb.buffer.size());
            int read_pos = comb.write_pos;

            float delayed = comb.buffer[read_pos];

            // Damping LP filter
            comb.lp_state = delayed * (1.0f - damp) + comb.lp_state * damp;
            float fb_sample = comb.lp_state * decay;

            comb.buffer[comb.write_pos] = input + fb_sample;
            comb.write_pos = (comb.write_pos + 1) % buf_len;

            out += delayed;
        }

        // Series allpass filters
        for (int a = 0; a < NUM_ALLPASS; ++a) {
            auto& ap = allpasses_[a];
            int buf_len = static_cast<int>(ap.buffer.size());

            float delayed = ap.buffer[ap.write_pos];
            float temp = out + delayed * ap.feedback;
            ap.buffer[ap.write_pos] = temp;
            out = delayed - out * ap.feedback;

            ap.write_pos = (ap.write_pos + 1) % buf_len;
        }

        buffer[i] = dry * (1.0f - level) + out * level;
    }
}

void Reverb::reset() {
    for (auto& c : combs_) {
        std::fill(c.buffer.begin(), c.buffer.end(), 0.0f);
        c.write_pos = 0;
        c.lp_state = 0.0f;
    }
    for (auto& a : allpasses_) {
        std::fill(a.buffer.begin(), a.buffer.end(), 0.0f);
        a.write_pos = 0;
    }
}

} // namespace GuitarAmp
