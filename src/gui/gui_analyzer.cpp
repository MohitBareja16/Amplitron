#include "gui/gui_analyzer.h"
#include "gui/theme.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace GuitarAmp {

GuiAnalyzer::GuiAnalyzer(AudioEngine& engine)
    : engine_(engine),
      spectrum_analyzer_(std::make_unique<SpectrumAnalyzer>()) {}

void GuiAnalyzer::render_vu_bar(const char* id,
                                const char* label,
                                float rms_level,
                                float peak_hold,
                                bool clip_active,
                                float clip_flash,
                                ImU32 base_color,
                                ImU32 peak_color) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    float db_value = (rms_level > 0.0001f) ? (20.0f * std::log10(rms_level)) : -96.0f;
    ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 1.0f), "%.1f dB", db_value);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 18.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 bg_col = Theme::METER_BG;
    if (clip_active || clip_flash > 0.01f) {
        const float flash = clamp(clip_flash, 0.0f, 1.0f);
        int alpha = static_cast<int>(90.0f + flash * 130.0f);
        bg_col = IM_COL32(180, 30, 30, alpha);
    }

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg_col, Theme::ROUNDING_SM);

    float rms_fill = clamp(rms_level, 0.0f, 1.0f) * width;
    dl->AddRectFilled(pos, ImVec2(pos.x + rms_fill, pos.y + height), base_color, Theme::ROUNDING_SM);

    float peak_x = pos.x + clamp(peak_hold, 0.0f, 1.0f) * width;
    dl->AddLine(ImVec2(peak_x, pos.y - 1.0f), ImVec2(peak_x, pos.y + height + 1.0f), peak_color, 2.0f);

    if (clip_active || clip_flash > 0.01f) {
        dl->AddText(ImVec2(pos.x + width - 32.0f, pos.y - 1.0f), IM_COL32(255, 90, 90, 255), "CLIP");
    }

    ImGui::Dummy(ImVec2(width, height + 6.0f));
    ImGui::PopID();
}

void GuiAnalyzer::render() {
    float panel_h = expanded_ ? 230.0f : 34.0f;
    ImGui::BeginChild("AnalyzerPanel", ImVec2(0, panel_h), true, ImGuiWindowFlags_NoScrollbar);

    const bool expanded = ImGui::CollapsingHeader("Real-Time Analyzer", ImGuiTreeNodeFlags_DefaultOpen);
    expanded_ = expanded;
    engine_.set_analyzer_enabled(expanded);
    if (!expanded) {
        ImGui::EndChild();
        return;
    }

    const float dt = std::max(ImGui::GetIO().DeltaTime, 1.0f / 240.0f);

    const float input_rms = engine_.get_input_rms();
    const float output_rms = engine_.get_output_rms();
    smoothed_input_rms_ += (input_rms - smoothed_input_rms_) * 0.22f;
    smoothed_output_rms_ += (output_rms - smoothed_output_rms_) * 0.22f;

    const float peak_decay = 0.45f;
    input_rms_peak_hold_ = std::max(smoothed_input_rms_, input_rms_peak_hold_ - peak_decay * dt);
    output_rms_peak_hold_ = std::max(smoothed_output_rms_, output_rms_peak_hold_ - peak_decay * dt);

    const bool input_clipped = engine_.consume_input_clipped();
    const bool output_clipped = engine_.consume_output_clipped();
    if (input_clipped) input_clip_flash_ = 1.0f;
    if (output_clipped) output_clip_flash_ = 1.0f;
    input_clip_flash_ = std::max(0.0f, input_clip_flash_ - dt * 2.0f);
    output_clip_flash_ = std::max(0.0f, output_clip_flash_ - dt * 2.0f);

    int mode_index = static_cast<int>(analyzer_mode_);
    ImGui::TextUnformatted("Spectrum:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("##AnalyzerMode", &mode_index, "Input\0Output\0Overlay\0")) {
        analyzer_mode_ = static_cast<SpectrumAnalyzer::DisplayMode>(mode_index);
    }

    ImGui::Columns(2, "analyzer_vu_cols", false);
    render_vu_bar("input_vu",
                  "INPUT RMS",
                  smoothed_input_rms_,
                  input_rms_peak_hold_,
                  input_clipped,
                  input_clip_flash_,
                  IM_COL32(60, 200, 110, 230),
                  IM_COL32(255, 230, 120, 255));
    ImGui::NextColumn();
    render_vu_bar("output_vu",
                  "OUTPUT RMS",
                  smoothed_output_rms_,
                  output_rms_peak_hold_,
                  output_clipped,
                  output_clip_flash_,
                  IM_COL32(80, 170, 245, 230),
                  IM_COL32(255, 230, 120, 255));
    ImGui::Columns(1);

    const uint64_t analyzer_seq = engine_.get_analyzer_sequence();
    if (analyzer_seq != analyzer_last_sequence_) {
        if (engine_.copy_analyzer_snapshot(analyzer_input_buf_.data(),
                                           analyzer_output_buf_.data(),
                                           AudioEngine::ANALYZER_FFT_SIZE)) {
            spectrum_analyzer_->update(analyzer_input_buf_.data(),
                                       analyzer_output_buf_.data(),
                                       engine_.get_sample_rate(),
                                       dt);
            analyzer_last_sequence_ = analyzer_seq;
        }
    }

    ImVec2 plot_pos = ImGui::GetCursorScreenPos();
    ImVec2 plot_size(ImGui::GetContentRegionAvail().x, 112.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(plot_pos,
                      ImVec2(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y),
                      IM_COL32(20, 22, 28, 255),
                      Theme::ROUNDING_SM);

    if (spectrum_analyzer_) {
        spectrum_analyzer_->draw(dl, plot_pos, plot_size, analyzer_mode_);
    }

    ImGui::Dummy(plot_size);

    const float axis_left = ImGui::GetCursorPosX();
    const float axis_w = ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(Theme::TextSecondary(), "20 Hz");
    ImGui::SameLine(axis_left + axis_w * 0.48f);
    ImGui::TextColored(Theme::TextSecondary(), "1 kHz");
    ImGui::SameLine(axis_left + axis_w - 52.0f);
    ImGui::TextColored(Theme::TextSecondary(), "20 kHz");

    ImGui::EndChild();
}

} // namespace GuitarAmp
