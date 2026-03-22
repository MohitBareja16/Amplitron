#pragma once

#include "common.h"
#include "audio/audio_engine.h"
#include "preset_manager.h"

struct SDL_Window;
typedef void* SDL_GLContext;

namespace GuitarAmp {

class PedalBoard;

class GuiManager {
public:
    GuiManager(AudioEngine& engine);
    ~GuiManager();

    bool initialize(int width = 1280, int height = 720);
    void shutdown();
    bool run_frame();  // returns false when should quit

private:
    void render_menu_bar();
    void render_settings_window();
    void render_meter(const char* label, float level, float x, float y, float w, float h);
    void render_master_controls();
    void render_save_preset_popup();
    void render_load_preset_popup();
    void render_recording_controls();
    void render_recording_save_dialog();

    AudioEngine& engine_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    std::unique_ptr<PedalBoard> pedal_board_;

    bool initialized_ = false;
    bool show_settings_ = false;
    bool show_save_preset_ = false;
    bool show_load_preset_ = false;
    int window_width_ = 1280;
    int window_height_ = 720;

    // Preset UI state
    char preset_name_buf_[128] = "My Preset";
    char preset_desc_buf_[256] = "";
    std::vector<std::string> preset_files_;
    std::string preset_status_msg_;

    // Smoothed meter values
    float smoothed_input_level_ = 0.0f;
    float smoothed_output_level_ = 0.0f;

    // Recording UI state
    bool show_recording_save_ = false;
    bool recording_save_pending_ = false;
    float rec_waveform_buf_[512] = {};
};

} // namespace GuitarAmp
