#include "gui/gui_tuner.h"
#include "gui/theme.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>

namespace GuitarAmp {

void GuiTuner::toggle(bool& show) {
    show = !show;
    if (show) {
        tuner_->set_enabled(true);
        engine_.set_tuner_tap(tuner_);
    } else {
        engine_.clear_tuner_tap();
        tuner_->set_enabled(false);
    }
}

void GuiTuner::render(bool& show) {
    ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
    bool open = show;
    if (!ImGui::Begin("Chromatic Tuner", &open)) {
        ImGui::End();
        if (!open) {
            show = false;
            engine_.clear_tuner_tap();
            tuner_->set_enabled(false);
        }
        return;
    }
    if (!open) {
        show = false;
        engine_.clear_tuner_tap();
        tuner_->set_enabled(false);
        ImGui::End();
        return;
    }

    TunerPedal* tuner = tuner_.get();
    bool has_signal = tuner->signal_detected.load(std::memory_order_relaxed);
    int note_idx = tuner->detected_note.load(std::memory_order_relaxed);
    int octave = tuner->detected_octave.load(std::memory_order_relaxed);
    float cents = tuner->detected_cents.load(std::memory_order_relaxed);
    float freq = tuner->detected_freq.load(std::memory_order_relaxed);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float win_w = ImGui::GetContentRegionAvail().x;

    if (has_signal && note_idx >= 0) {
        // Note name (large centered)
        char note_buf[16];
        std::snprintf(note_buf, sizeof(note_buf), "%s%d",
                      TunerPedal::note_name(note_idx), octave);
        ImVec2 note_size = ImGui::CalcTextSize(note_buf);
        float scale = 3.0f;
        float note_w = note_size.x * scale;
        ImVec2 note_pos = ImGui::GetCursorScreenPos();
        note_pos.x += (win_w - note_w) * 0.5f;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale,
                    note_pos, Theme::TEXT_PRIMARY, note_buf);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * scale + 8));

        // Cents text (colored)
        float abs_cents = std::fabs(cents);
        ImVec4 cents_col = (abs_cents < 2.0f)
            ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f)
            : (abs_cents < 15.0f)
                ? ImVec4(0.9f, 0.8f, 0.2f, 1.0f)
                : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
        char cents_buf[32];
        std::snprintf(cents_buf, sizeof(cents_buf), "%+.1f cents", cents);
        ImVec2 cents_size = ImGui::CalcTextSize(cents_buf);
        ImGui::SetCursorPosX((win_w - cents_size.x) * 0.5f);
        ImGui::TextColored(cents_col, "%s", cents_buf);

        ImGui::Spacing();

        // Cents deviation bar
        float bar_w = win_w - 40;
        float bar_h = 14;
        ImVec2 bar_pos = ImGui::GetCursorScreenPos();
        bar_pos.x += 20;
        dl->AddRectFilled(bar_pos,
            ImVec2(bar_pos.x + bar_w, bar_pos.y + bar_h),
            Theme::KNOB_BG, 4.0f);

        // Center tick
        float cx = bar_pos.x + bar_w * 0.5f;
        dl->AddLine(ImVec2(cx, bar_pos.y - 2), ImVec2(cx, bar_pos.y + bar_h + 2),
                    Theme::TEXT_DIM, 2.0f);

        // Needle
        float needle_norm = clamp(cents / 50.0f, -1.0f, 1.0f);
        float needle_x = cx + needle_norm * (bar_w * 0.5f);
        ImU32 needle_col = ImGui::ColorConvertFloat4ToU32(cents_col);
        dl->AddRectFilled(
            ImVec2(needle_x - 4, bar_pos.y - 3),
            ImVec2(needle_x + 4, bar_pos.y + bar_h + 3),
            needle_col, 3.0f);

        ImGui::Dummy(ImVec2(0, bar_h + 12));

        // Frequency
        char freq_buf[32];
        std::snprintf(freq_buf, sizeof(freq_buf), "%.1f Hz", freq);
        ImVec2 freq_size = ImGui::CalcTextSize(freq_buf);
        ImGui::SetCursorPosX((win_w - freq_size.x) * 0.5f);
        ImGui::TextColored(Theme::TextSecondary(), "%s", freq_buf);
    } else {
        // No signal
        const char* dash = "---";
        ImVec2 dash_size = ImGui::CalcTextSize(dash);
        float scale = 3.0f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        pos.x += (win_w - dash_size.x * scale) * 0.5f;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale,
                    pos, Theme::TEXT_DIM, dash);
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * scale + 8));

        const char* waiting = "Play a note...";
        ImVec2 wt_size = ImGui::CalcTextSize(waiting);
        ImGui::SetCursorPosX((win_w - wt_size.x) * 0.5f);
        ImGui::TextColored(Theme::TextDim(), "%s", waiting);

        ImGui::Dummy(ImVec2(0, 40));
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Mute toggle
    bool mute_on = tuner->params()[0].value >= 0.5f;
    if (mute_on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.55f, 0.2f, 1.0f));
    }
    float btn_w = 140;
    ImGui::SetCursorPosX((win_w - btn_w) * 0.5f);
    if (ImGui::Button(mute_on ? "MUTE ON" : "MUTE OFF", ImVec2(btn_w, 30))) {
        tuner->params()[0].value = mute_on ? 0.0f : 1.0f;
    }
    ImGui::PopStyleColor(2);

    // A4 reference
    ImGui::Spacing();
    float a4_ref = tuner->params()[1].value;
    ImGui::SetNextItemWidth(win_w - 20);
    if (ImGui::SliderFloat("A4 Reference", &a4_ref, 430.0f, 450.0f, "%.0f Hz")) {
        tuner->params()[1].value = a4_ref;
    }

    ImGui::End();
}

} // namespace GuitarAmp
