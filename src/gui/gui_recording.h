#pragma once

#include "audio/audio_engine.h"
#include "audio/recorder.h"
#include <string>

namespace GuitarAmp {

/**
 * @brief Handles recording controls and waveform display UI.
 * Extracted from GuiManager for single-responsibility.
 */
class GuiRecording {
public:
    explicit GuiRecording(AudioEngine& engine) : engine_(engine) {}

    /** @brief Render recording controls (start/stop/pause/waveform). */
    void render_controls();

    /** @brief Render the "Save Recording" file dialog. */
    void render_save_dialog(bool& show);

    /** @brief Whether a save dialog is pending. */
    bool is_save_pending() const { return recording_save_pending_; }
    void set_save_pending(bool pending) { recording_save_pending_ = pending; }

    /** @brief Access status message for shared display. */
    std::string& status_message() { return status_msg_; }

    /** @brief Whether to show the save dialog. */
    bool& show_save() { return show_recording_save_; }

private:
    AudioEngine& engine_;

    bool show_recording_save_ = false;
    bool recording_save_pending_ = false;
    float rec_waveform_buf_[512] = {};
    std::string status_msg_;
};

} // namespace GuitarAmp
