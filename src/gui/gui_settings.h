#pragma once

#include "audio/audio_engine.h"

namespace GuitarAmp {

/**
 * @brief Renders the Audio Settings modal window.
 * Extracted from GuiManager for single-responsibility.
 */
class GuiSettings {
public:
    explicit GuiSettings(AudioEngine& engine) : engine_(engine) {}

    /** @brief Render the settings window. Only call when show is true. */
    void render(bool& show);

private:
    AudioEngine& engine_;
};

} // namespace GuitarAmp
