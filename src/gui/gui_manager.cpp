#include "gui/gui_manager.h"
#include "gui/pedal_board.h"
#include "gui/theme.h"
#include "gui/file_dialog.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <cstring>
#include <cmath>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace GuitarAmp {

GuiManager::GuiManager(AudioEngine& engine)
    : engine_(engine) {}

GuiManager::~GuiManager() {
    shutdown();
}

bool GuiManager::initialize(int width, int height) {
    window_width_ = width;
    window_height_ = height;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // OpenGL 3.3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window_ = SDL_CreateWindow(
        Theme::WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width_, window_height_,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window_) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    gl_context_ = SDL_GL_CreateContext(window_);
    SDL_GL_MakeCurrent(window_, gl_context_);
    SDL_GL_SetSwapInterval(1); // vsync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Amplitron design system
    ImGui::StyleColorsDark();
    Theme::ApplyStyle();

    // Load window icon from assets/icon.svg (resolve path relative to executable)
    {
        std::string icon_path;
        char* base = SDL_GetBasePath();
        if (base) {
            icon_path = std::string(base) + "assets/icon.svg";
            SDL_free(base);
        }
        // Fallback paths for development and installed layouts
        NSVGimage* svg = nullptr;
        if (!icon_path.empty())
            svg = nsvgParseFromFile(icon_path.c_str(), "px", 96.0f);
        if (!svg)
            svg = nsvgParseFromFile("../assets/icon.svg", "px", 96.0f);
        if (!svg)
            svg = nsvgParseFromFile("assets/icon.svg", "px", 96.0f);
        if (svg) {
            const int icon_size = 64;  // 64x64 icon
            NSVGrasterizer* rast = nsvgCreateRasterizer();
            if (rast) {
                unsigned char* img = new unsigned char[icon_size * icon_size * 4];
                nsvgRasterize(rast, svg, 0, 0,
                             icon_size / svg->width,
                             img, icon_size, icon_size,
                             icon_size * 4);

                SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
                    img, icon_size, icon_size, 32, icon_size * 4,
                    0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
                if (icon) {
                    SDL_SetWindowIcon(window_, icon);
                    SDL_FreeSurface(icon);
                }
                delete[] img;
                nsvgDeleteRasterizer(rast);
            }
            nsvgDelete(svg);
        } else {
            std::cerr << "Warning: Could not load assets/icon.svg" << std::endl;
        }
    }

    ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
    ImGui_ImplOpenGL3_Init("#version 330");

    pedal_board_ = std::make_unique<PedalBoard>(engine_);

    initialized_ = true;
    return true;
}

void GuiManager::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    pedal_board_.reset();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (gl_context_) {
        SDL_GL_DeleteContext(gl_context_);
        gl_context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

bool GuiManager::run_frame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(window_))
            return false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Main menu bar
    render_menu_bar();

    // Full-window layout
    SDL_GetWindowSize(window_, &window_width_, &window_height_);

    ImGui::SetNextWindowPos(ImVec2(0, 20));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(window_width_),
                                     static_cast<float>(window_height_) - 20));
    ImGui::Begin("##MainArea", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    render_master_controls();

    ImGui::Separator();

    // Recording controls (above pedal board)
    render_recording_controls();

    ImGui::Separator();

    // Pedal board area
    if (pedal_board_) {
        pedal_board_->render();
    }

    ImGui::End();

    // Popups / floating windows
    if (show_settings_) {
        render_settings_window();
    }
    if (show_save_preset_) {
        render_save_preset_popup();
    }
    if (show_load_preset_) {
        render_load_preset_popup();
    }
    if (show_recording_save_) {
        render_recording_save_dialog();
    }

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    SDL_GL_GetDrawableSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.078f, 0.071f, 0.063f, 1.0f);  // #141210 BG_DARKEST
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);

    return true;
}

void GuiManager::render_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Preset...", "Ctrl+S")) {
                show_save_preset_ = true;
            }
            if (ImGui::MenuItem("Load Preset...", "Ctrl+O")) {
                show_load_preset_ = true;
                preset_files_ = PresetManager::list_presets();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Settings")) show_settings_ = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                SDL_Event quit_event;
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Audio")) {
            if (engine_.is_running()) {
                if (ImGui::MenuItem("Stop Audio")) engine_.stop();
            } else {
                if (ImGui::MenuItem("Start Audio")) {
                    engine_.restart();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Restart Audio")) {
                engine_.restart();
            }
            ImGui::EndMenu();
        }

        // Status bar (right side)
        float bar_w = ImGui::GetWindowWidth();

        // Recording indicator
        ImGui::SameLine(bar_w - 400);
        if (engine_.recorder().is_recording()) {
            float t = static_cast<float>(ImGui::GetTime());
            ImGui::TextColored(Theme::RecBlink(t), "REC");
            ImGui::SameLine();
            ImGui::Text("%.1fs", engine_.recorder().get_duration());
        }

        // Audio status
        ImGui::SameLine(bar_w - 200);
        if (engine_.is_running()) {
            ImGui::TextColored(Theme::Live(), "LIVE");
        } else {
            ImGui::TextColored(Theme::Stopped(), "STOPPED");
        }
        ImGui::SameLine();
        ImGui::Text("%dHz", engine_.get_sample_rate());

        ImGui::EndMainMenuBar();
    }

    // Error banner when audio is stopped
    if (!engine_.is_running()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.08f, 0.08f, 0.95f));
        ImGui::BeginChild("AudioErrorBanner", ImVec2(0, 36), true);
        ImGui::TextColored(Theme::Stopped(), "Audio stream is STOPPED.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Restart Audio")) {
            engine_.restart();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Settings")) {
            show_settings_ = true;
        }
        std::string err = engine_.get_last_error();
        if (!err.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(Theme::GoldHot(), "  %s", err.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

void GuiManager::render_master_controls() {
    // Smooth metering
    float input_lvl = engine_.get_input_level();
    float output_lvl = engine_.get_output_level();
    smoothed_input_level_ += (input_lvl - smoothed_input_level_) * 0.3f;
    smoothed_output_level_ += (output_lvl - smoothed_output_level_) * 0.3f;

    ImGui::BeginChild("MasterControls", ImVec2(0, 80), true);

    ImGui::Columns(4, "master_cols", false);

    // Input gain
    ImGui::Text("INPUT");
    float input_gain = engine_.get_input_gain();
    if (ImGui::SliderFloat("##InputGain", &input_gain, 0.0f, 5.0f, "%.2f")) {
        engine_.set_input_gain(input_gain);
    }

    ImGui::NextColumn();

    // Input meter
    ImGui::Text("IN LEVEL");
    ImVec2 meter_pos = ImGui::GetCursorScreenPos();
    float meter_w = ImGui::GetColumnWidth() - 20;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(meter_pos, ImVec2(meter_pos.x + meter_w, meter_pos.y + 20),
                       Theme::METER_BG, Theme::ROUNDING_SM);
    float fill = std::min(smoothed_input_level_, 1.0f) * meter_w;
    ImU32 meter_color = (smoothed_input_level_ > 0.9f) ? Theme::METER_RED :
                        (smoothed_input_level_ > 0.6f) ? Theme::METER_YELLOW :
                                                          Theme::METER_GREEN;
    dl->AddRectFilled(meter_pos, ImVec2(meter_pos.x + fill, meter_pos.y + 20),
                       meter_color, Theme::ROUNDING_SM);
    ImGui::Dummy(ImVec2(meter_w, 20));

    ImGui::NextColumn();

    // Output meter
    ImGui::Text("OUT LEVEL");
    meter_pos = ImGui::GetCursorScreenPos();
    meter_w = ImGui::GetColumnWidth() - 20;
    dl->AddRectFilled(meter_pos, ImVec2(meter_pos.x + meter_w, meter_pos.y + 20),
                       Theme::METER_BG, Theme::ROUNDING_SM);
    fill = std::min(smoothed_output_level_, 1.0f) * meter_w;
    meter_color = (smoothed_output_level_ > 0.9f) ? Theme::METER_RED :
                  (smoothed_output_level_ > 0.6f) ? Theme::METER_YELLOW :
                                                     Theme::METER_GREEN;
    dl->AddRectFilled(meter_pos, ImVec2(meter_pos.x + fill, meter_pos.y + 20),
                       meter_color, Theme::ROUNDING_SM);
    ImGui::Dummy(ImVec2(meter_w, 20));

    ImGui::NextColumn();

    // Output gain
    ImGui::Text("OUTPUT");
    float output_gain = engine_.get_output_gain();
    if (ImGui::SliderFloat("##OutputGain", &output_gain, 0.0f, 2.0f, "%.2f")) {
        engine_.set_output_gain(output_gain);
    }

    ImGui::Columns(1);
    ImGui::EndChild();
}

void GuiManager::render_settings_window() {
    ImGui::SetNextWindowSize(ImVec2(600, 550), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio Settings", &show_settings_)) {
        ImGui::End();
        return;
    }

    // --- Current routing summary ---
    ImGui::TextColored(Theme::Gold(), "SIGNAL ROUTING");
    ImGui::BeginChild("RoutingSummary", ImVec2(0, 60), true);
    ImGui::TextColored(Theme::Live(), "Guitar IN:");
    ImGui::SameLine();
    ImGui::Text("%s", engine_.get_input_device_name().c_str());
    ImGui::TextColored(ImVec4(0.35f, 0.60f, 0.95f, 1.0f), "Speaker OUT:");
    ImGui::SameLine();
    ImGui::Text("%s", engine_.get_output_device_name().c_str());
    ImGui::EndChild();

    ImGui::Spacing();

    // --- Latency settings ---
    ImGui::TextColored(Theme::Gold(), "LATENCY");

    // Buffer size
    ImGui::Text("Buffer Size (lower = less latency, more CPU):");
    int buf_size = engine_.get_buffer_size();
    const int buf_sizes[] = {32, 64, 128, 256, 512};
    const char* buf_labels[] = {"32", "64", "128", "256", "512"};
    int current_idx = 1;
    for (int i = 0; i < 5; ++i) {
        if (buf_sizes[i] == buf_size) { current_idx = i; break; }
    }
    if (ImGui::Combo("Buffer Size", &current_idx, buf_labels, 5)) {
        engine_.set_buffer_size(buf_sizes[current_idx]);
    }

    float latency_ms = 1000.0f * engine_.get_buffer_size() / engine_.get_sample_rate();
    ImGui::Text("Estimated latency: %.1f ms", latency_ms);

    // Sample rate
    int sr = engine_.get_sample_rate();
    const int rates[] = {44100, 48000, 96000};
    const char* rate_labels[] = {"44100", "48000", "96000"};
    int sr_idx = 1;
    for (int i = 0; i < 3; ++i) {
        if (rates[i] == sr) { sr_idx = i; break; }
    }
    if (ImGui::Combo("Sample Rate", &sr_idx, rate_labels, 3)) {
        engine_.set_sample_rate(rates[sr_idx]);
    }

    ImGui::Separator();

    // --- Input device (USB Guitar Cable) ---
    ImGui::TextColored(Theme::Gold(),
        "INPUT DEVICE (USB Guitar Cable)");
    ImGui::TextWrapped(
        "Select your USB guitar cable or audio interface. "
        "USB devices are highlighted with [USB].");

    int current_input = engine_.get_input_device();
    auto input_devs = engine_.get_input_devices();
    ImGui::BeginChild("InputDevices", ImVec2(0, 120), true);
    for (auto& dev : input_devs) {
        bool is_selected = (dev.index == current_input);
        std::string label = dev.name;
        if (dev.is_usb_device) {
            label += "  [USB]";
        }

        if (dev.is_usb_device) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::GoldHot());
        }

        if (ImGui::Selectable(label.c_str(), is_selected)) {
            engine_.set_input_device(dev.index);
        }

        if (dev.is_usb_device) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // --- Output device ---
    ImGui::TextColored(Theme::Gold(), "OUTPUT DEVICE (Speakers/Headphones)");

    int current_output = engine_.get_output_device();
    auto output_devs = engine_.get_output_devices();
    ImGui::BeginChild("OutputDevices", ImVec2(0, 120), true);
    for (auto& dev : output_devs) {
        bool is_selected = (dev.index == current_output);
        std::string label = dev.name;
        if (dev.is_usb_device) {
            label += "  [USB - not recommended]";
        }

        if (dev.is_usb_device) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 0.7f));
        }

        if (ImGui::Selectable(label.c_str(), is_selected)) {
            engine_.set_output_device(dev.index);
        }

        if (dev.is_usb_device) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void GuiManager::render_save_preset_popup() {
    ImGui::SetNextWindowSize(ImVec2(420, 250), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Save Preset", &show_save_preset_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Save current pedal configuration as a preset.");
    ImGui::Spacing();

    ImGui::Text("Preset Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##preset_name", preset_name_buf_, sizeof(preset_name_buf_));

    ImGui::Spacing();
    ImGui::Text("Description (optional):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##preset_desc", preset_desc_buf_, sizeof(preset_desc_buf_),
                               ImVec2(-1, 60));

    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(120, 30))) {
        std::string name(preset_name_buf_);
        if (!name.empty()) {
            // Sanitize filename
            std::string filename = name;
            for (char& c : filename) {
                if (c == ' ') c = '_';
                if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                    c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                    c = '_';
            }
            std::string path = PresetManager::get_presets_dir() + "/" + filename + ".json";

            if (PresetManager::save_preset(path, name, std::string(preset_desc_buf_), engine_)) {
                preset_status_msg_ = "Saved: " + path;
                show_save_preset_ = false;
                // Rebuild pedal board to reflect any changes
                if (pedal_board_) pedal_board_->rebuild_widgets();
            } else {
                preset_status_msg_ = "Error: " + PresetManager::last_error();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 30))) {
        show_save_preset_ = false;
    }

    if (!preset_status_msg_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", preset_status_msg_.c_str());
    }

    ImGui::End();
}

void GuiManager::render_load_preset_popup() {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Load Preset", &show_load_preset_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Select a preset to load:");
    ImGui::Spacing();

    if (ImGui::Button("Refresh List")) {
        preset_files_ = PresetManager::list_presets();
    }

    ImGui::Spacing();
    ImGui::BeginChild("PresetList", ImVec2(0, -70), true);

    if (preset_files_.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "No presets found in '%s/' folder.\nSave a preset first, or place .json files there.",
            PresetManager::get_presets_dir().c_str());
    }

    for (auto& filepath : preset_files_) {
        // Show just the filename
        std::string display = filepath;
        size_t slash = display.find_last_of("/\\");
        if (slash != std::string::npos) display = display.substr(slash + 1);

        if (ImGui::Selectable(display.c_str())) {
            if (PresetManager::load_preset(filepath, engine_)) {
                preset_status_msg_ = "Loaded: " + display;
                show_load_preset_ = false;
                // Rebuild pedal board widgets to match new chain
                if (pedal_board_) pedal_board_->rebuild_widgets();
            } else {
                preset_status_msg_ = "Error: " + PresetManager::last_error();
            }
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(120, 30))) {
        show_load_preset_ = false;
    }

    if (!preset_status_msg_.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", preset_status_msg_.c_str());
    }

    ImGui::End();
}

void GuiManager::render_recording_controls() {
    auto& rec = engine_.recorder();
    bool is_recording = rec.is_recording();
    bool is_paused = rec.is_paused();
    bool has_unsaved = rec.has_unsaved();

    float panel_height = is_recording ? 120.0f : 40.0f;
    ImGui::BeginChild("RecordingPanel", ImVec2(0, panel_height), true,
                       ImGuiWindowFlags_NoScrollbar);

    if (is_recording) {
        // === RECORDING ACTIVE ===

        // Top row: controls + timer
        // Record/Pause button
        if (is_paused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            if (ImGui::Button("RESUME", ImVec2(80, 28))) {
                rec.resume();
            }
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("PAUSE", ImVec2(80, 28))) {
                rec.pause();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();

        // Stop button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("STOP", ImVec2(80, 28))) {
            rec.stop();
            rec.write_metadata(rec.filepath(), engine_);
            show_recording_save_ = true;
            recording_save_pending_ = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        // Blinking REC indicator
        float t = static_cast<float>(ImGui::GetTime());
        if (is_paused) {
            ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.2f, 1.0f), "  PAUSED");
        } else {
            float blink = (std::sin(t * 4.0f) > 0.0f) ? 1.0f : 0.3f;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.15f, 0.15f, blink));
            ImGui::Text("  ●  REC");
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();

        // Timer MM:SS.ms
        float duration = rec.get_duration();
        int mins = static_cast<int>(duration) / 60;
        int secs = static_cast<int>(duration) % 60;
        int ms = static_cast<int>((duration - static_cast<int>(duration)) * 10);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
                           "  %02d:%02d.%d", mins, secs, ms);

        ImGui::SameLine();

        // Peak meter (compact)
        float peak = rec.get_current_peak();
        ImGui::TextColored(peak > 0.9f ? ImVec4(1, 0.2f, 0.2f, 1) :
                           peak > 0.6f ? ImVec4(1, 0.8f, 0.2f, 1) :
                                         ImVec4(0.2f, 0.8f, 0.2f, 1),
                           "  Peak: %.1f dB",
                           peak > 0.0001f ? 20.0f * std::log10(peak) : -96.0f);

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
        int64_t file_bytes = rec.get_samples_written() * 2; // 16-bit PCM
        if (file_bytes > 1024 * 1024)
            ImGui::Text("%.1f MB", file_bytes / (1024.0f * 1024.0f));
        else
            ImGui::Text("%.0f KB", file_bytes / 1024.0f);

        // === WAVEFORM DISPLAY ===
        ImGui::Spacing();
        rec.get_waveform(rec_waveform_buf_, Recorder::WAVEFORM_SIZE);

        ImVec2 wave_pos = ImGui::GetCursorScreenPos();
        float wave_w = ImGui::GetContentRegionAvail().x;
        float wave_h = 50.0f;

        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Dark background for waveform
        draw->AddRectFilled(wave_pos,
                            ImVec2(wave_pos.x + wave_w, wave_pos.y + wave_h),
                            IM_COL32(20, 18, 16, 255), 4.0f);

        // Center line
        float center_y = wave_pos.y + wave_h * 0.5f;
        draw->AddLine(ImVec2(wave_pos.x, center_y),
                      ImVec2(wave_pos.x + wave_w, center_y),
                      IM_COL32(60, 55, 48, 255));

        // Waveform bars (mirrored around center)
        ImU32 wave_color = is_paused ? IM_COL32(180, 160, 50, 200)
                                      : IM_COL32(200, 80, 60, 220);
        ImU32 wave_color_bright = is_paused ? IM_COL32(220, 200, 80, 255)
                                             : IM_COL32(255, 100, 70, 255);

        int num_bars = static_cast<int>(wave_w);
        float samples_per_pixel = static_cast<float>(Recorder::WAVEFORM_SIZE) / num_bars;

        for (int i = 0; i < num_bars; ++i) {
            int idx = static_cast<int>(i * samples_per_pixel);
            if (idx >= Recorder::WAVEFORM_SIZE) idx = Recorder::WAVEFORM_SIZE - 1;
            float val = rec_waveform_buf_[idx];
            float bar_h = val * wave_h * 0.48f;
            if (bar_h < 0.5f) continue;

            float x = wave_pos.x + i;
            ImU32 col = val > 0.8f ? wave_color_bright : wave_color;
            draw->AddLine(ImVec2(x, center_y - bar_h),
                          ImVec2(x, center_y + bar_h), col);
        }

        // Border
        draw->AddRect(wave_pos,
                      ImVec2(wave_pos.x + wave_w, wave_pos.y + wave_h),
                      IM_COL32(70, 65, 55, 255), 4.0f);

        ImGui::Dummy(ImVec2(wave_w, wave_h));

    } else if (has_unsaved) {
        // === UNSAVED RECORDING ===
        ImGui::TextColored(Theme::Gold(), "Recording complete");
        ImGui::SameLine();
        ImGui::Text("  %.1f s  |  ", rec.get_duration());
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
        if (ImGui::Button("Save As...", ImVec2(100, 24))) {
            show_recording_save_ = true;
            recording_save_pending_ = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Discard", ImVec2(80, 24))) {
            rec.discard();
            preset_status_msg_ = "Recording discarded.";
        }
        ImGui::PopStyleColor(2);

    } else {
        // === READY STATE ===
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.05f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("●  REC", ImVec2(90, 28))) {
            std::string filepath = Recorder::generate_filename();
            rec.start(filepath, engine_.get_sample_rate());
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "  Ready to record  |  WAV 16-bit %d Hz",
                           engine_.get_sample_rate());
    }

    ImGui::EndChild();
}

void GuiManager::render_recording_save_dialog() {
    if (!recording_save_pending_) {
        show_recording_save_ = false;
        return;
    }

    // Launch native save dialog (runs on this frame, blocks briefly)
    recording_save_pending_ = false;
    show_recording_save_ = false;

    auto& rec = engine_.recorder();
    std::string dest = show_save_dialog("recording.wav", "WAV Audio", "wav");

    if (!dest.empty()) {
        if (rec.save_to(dest)) {
            preset_status_msg_ = "Saved: " + dest;
        } else {
            preset_status_msg_ = "Failed to save recording.";
        }
    }
    // If cancelled, keep as unsaved — user can save later or discard
}

} // namespace GuitarAmp
