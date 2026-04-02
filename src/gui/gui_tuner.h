#pragma once

#include "audio/audio_engine.h"
#include "audio/effects/tuner.h"
#include <memory>
#include <utility>

namespace GuitarAmp {

/**
 * @brief Handles the chromatic tuner modal UI.
 * Extracted from GuiManager for single-responsibility.
 */
class GuiTuner {
public:
    GuiTuner(AudioEngine& engine, std::shared_ptr<TunerPedal> tuner)
        : engine_(engine), tuner_(std::move(tuner)) {}

    /** @brief Render the tuner modal. Only call when show is true. */
    void render(bool& show);

    /** @brief Toggle tuner on/off. */
    void toggle(bool& show);

    /** @brief Get the tuner instance for menu bar status display. */
    std::shared_ptr<TunerPedal> tuner_instance() const { return tuner_; }

private:
    AudioEngine& engine_;
    std::shared_ptr<TunerPedal> tuner_;
};

} // namespace GuitarAmp
