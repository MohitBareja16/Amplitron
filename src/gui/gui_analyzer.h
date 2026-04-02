#pragma once

#include "audio/audio_engine.h"
#include "gui/spectrum_analyzer.h"
#include <array>
#include <memory>

namespace GuitarAmp {

/**
 * @brief Handles the real-time analyzer panel (VU meters + spectrum).
 * Extracted from GuiManager for single-responsibility.
 */
class GuiAnalyzer {
public:
    explicit GuiAnalyzer(AudioEngine& engine);

    /** @brief Render the collapsible analyzer panel. */
    void render();

    /** @brief Whether the panel is expanded (affects layout). */
    bool is_expanded() const { return expanded_; }

    /** @brief Height to reserve in the parent layout for this panel. */
    float analyzer_reserved_height() const { return expanded_ ? 245.0f : 38.0f; }

private:
    void render_vu_bar(const char* id,
                       const char* label,
                       float rms_level,
                       float peak_hold,
                       bool clip_active,
                       float clip_flash,
                       ImU32 base_color,
                       ImU32 peak_color);

    AudioEngine& engine_;

    // Smoothed meter values
    float smoothed_input_rms_ = 0.0f;
    float smoothed_output_rms_ = 0.0f;
    float input_rms_peak_hold_ = 0.0f;
    float output_rms_peak_hold_ = 0.0f;
    float input_clip_flash_ = 0.0f;
    float output_clip_flash_ = 0.0f;

    bool expanded_ = true;
    SpectrumAnalyzer::DisplayMode analyzer_mode_ = SpectrumAnalyzer::DisplayMode::Output;
    std::unique_ptr<SpectrumAnalyzer> spectrum_analyzer_;
    std::array<float, AudioEngine::ANALYZER_FFT_SIZE> analyzer_input_buf_{};
    std::array<float, AudioEngine::ANALYZER_FFT_SIZE> analyzer_output_buf_{};
    uint64_t analyzer_last_sequence_ = 0;
};

} // namespace GuitarAmp
