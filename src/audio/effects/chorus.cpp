#include "audio/effects/chorus.h"
#include "audio/effect_factory.h"

namespace GuitarAmp {

static EffectRegistrar<Chorus> reg("Chorus");

Chorus::Chorus() {
    params_ = {
        {"Rate",   1.5f, 0.1f, 10.0f, 1.5f, "Hz", "Speed of the modulation sweep. Higher values create faster wobbling effects."},
        {"Depth",  5.0f, 0.5f, 20.0f, 5.0f, "ms", "Intensity of the modulation. Higher values create a deeper, more pronounced pitch shift."},
        {"Level",  0.5f, 0.0f,  1.0f, 0.5f, "", "Mix volume of the chorus effect. 0 is dry, 1 is fully wet."},
    };
    set_sample_rate(DEFAULT_SAMPLE_RATE);
}

void Chorus::set_sample_rate(int sample_rate) {
    Effect::set_sample_rate(sample_rate);
    max_delay_samples_ = static_cast<int>(sample_rate * 0.05f); // 50ms max
    delay_buffer_.assign(max_delay_samples_, 0.0f);
    write_pos_ = 0;
}

void Chorus::process(float* buffer, int num_samples) {
    if (!enabled_) return;

    float rate = params_[0].value;
    float depth_ms = params_[1].value;
    float level = params_[2].value;

    float depth_samples = depth_ms * 0.001f * sample_rate_;
    float lfo_inc = rate / sample_rate_;

    for (int i = 0; i < num_samples; ++i) {
        float dry = buffer[i];

        // Write current sample to delay buffer
        delay_buffer_[write_pos_] = buffer[i];

        // LFO modulated delay
        float lfo = 0.5f * (1.0f + std::sin(TWO_PI * lfo_phase_));
        float delay = 1.0f + lfo * depth_samples;

        // Linear interpolation for fractional delay
        float read_pos_f = static_cast<float>(write_pos_) - delay;
        if (read_pos_f < 0.0f) read_pos_f += max_delay_samples_;

        int read_pos_i = static_cast<int>(read_pos_f);
        float frac = read_pos_f - read_pos_i;

        int pos0 = read_pos_i % max_delay_samples_;
        int pos1 = (read_pos_i + 1) % max_delay_samples_;

        float delayed = delay_buffer_[pos0] * (1.0f - frac) + delay_buffer_[pos1] * frac;

        buffer[i] = dry * (1.0f - level * 0.5f) + delayed * level;

        write_pos_ = (write_pos_ + 1) % max_delay_samples_;
        lfo_phase_ += lfo_inc;
        if (lfo_phase_ >= 1.0f) lfo_phase_ -= 1.0f;
    }
}

void Chorus::reset() {
    std::fill(delay_buffer_.begin(), delay_buffer_.end(), 0.0f);
    write_pos_ = 0;
    lfo_phase_ = 0.0f;
}

} // namespace GuitarAmp
