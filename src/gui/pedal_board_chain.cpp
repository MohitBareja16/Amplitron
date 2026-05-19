#include "gui/pedal_board.h"
#include "gui/pedal_widget.h"
#include "gui/theme.h"
#include "gui/gui_graph_state.h"
#include <imgui.h>
#include <unordered_map>
#include <cmath>

namespace Amplitron {

void PedalBoard::render_signal_chain() {
    auto& ui_state = GuiGraphState::get_instance();
    auto& audio_graph = engine_.graph(); 
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    ImVec2 canvas_end = ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);

    ImGui::SetCursorScreenPos(canvas_pos);
    ImGui::InvisibleButton("canvas_panning_hotspot", canvas_size, ImGuiButtonFlags_MouseButtonRight);
    ImGui::SetItemAllowOverlap();
    
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ui_state.scrolling.x += ImGui::GetIO().MouseDelta.x;
        ui_state.scrolling.y += ImGui::GetIO().MouseDelta.y;
    }

    if (ui_state.show_grid) {
        float GRID_SZ = 32.0f;
        ImU32 GRID_COLOR = IM_COL32(36, 34, 30, 255);
        for (float x = std::fmod(ui_state.scrolling.x, GRID_SZ); x < canvas_size.x; x += GRID_SZ) {
            draw_list->AddLine(ImVec2(canvas_pos.x + x, canvas_pos.y), ImVec2(canvas_pos.x + x, canvas_end.y), GRID_COLOR);
        }
        for (float y = std::fmod(ui_state.scrolling.y, GRID_SZ); y < canvas_size.y; y += GRID_SZ) {
            draw_list->AddLine(ImVec2(canvas_pos.x, canvas_pos.y + y), ImVec2(canvas_end.x, canvas_pos.y + y), GRID_COLOR);
        }
    }

    draw_list->PushClipRect(canvas_pos, canvas_end, true);

    ImVec2 offset = ImVec2(canvas_pos.x + ui_state.scrolling.x, canvas_pos.y + ui_state.scrolling.y);
    std::unordered_map<int, ImVec2> pin_positions_cache;

    int node_to_delete = -1; // Safely track deletions outside the render loop

    float default_spacing_x = 40.0f;
    for (const auto& node : audio_graph.get_nodes()) {
        ui_state.set_default_position_if_missing(node.id, default_spacing_x, 60.0f);
        default_spacing_x += 240.0f; 

        auto& node_layout = ui_state.node_positions[node.id];
        ImVec2 node_screen_pos = ImVec2(offset.x + node_layout.position.x, offset.y + node_layout.position.y);

        float node_width = (node.routing_type == NodeRoutingType::StandardEffect) ? 160.0f : 110.0f;
        float node_height = (node.routing_type == NodeRoutingType::StandardEffect) ? 320.0f : 70.0f;

        ImGui::PushID(node.id);

        if (node.routing_type == NodeRoutingType::StandardEffect) {
            PedalWidget* target_widget = nullptr;
            for (auto& w : widgets_) {
                if (w->get_effect() == node.pedal) { target_widget = w.get(); break; }
            }

            if (target_widget) {
                ImGui::SetCursorScreenPos(node_screen_pos);
                ImGui::BeginGroup();
                target_widget->render(); 
                ImGui::EndGroup();

                ImGui::SetCursorScreenPos(node_screen_pos);
                ImGui::InvisibleButton("native_drag_handle", ImVec2(node_width - 25.0f, 30.0f));
                ImGui::SetItemAllowOverlap(); 
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    node_layout.position.x += ImGui::GetIO().MouseDelta.x;
                    node_layout.position.y += ImGui::GetIO().MouseDelta.y;
                }
            }
        } else {
            ImVec2 node_end = ImVec2(node_screen_pos.x + node_width, node_screen_pos.y + node_height);
            ImU32 bg_color = (node.routing_type == NodeRoutingType::Splitter) ? IM_COL32(30, 45, 60, 255) : IM_COL32(50, 35, 60, 255);
            draw_list->AddRectFilled(node_screen_pos, node_end, bg_color, Theme::ROUNDING_MD);
            draw_list->AddRect(node_screen_pos, node_end, IM_COL32(180, 140, 80, 180), Theme::ROUNDING_MD, 0, 1.5f);

            ImGui::SetCursorScreenPos(node_screen_pos);
            ImGui::InvisibleButton("util_drag_handle", ImVec2(node_width - 25.0f, node_height));
            ImGui::SetItemAllowOverlap();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                node_layout.position.x += ImGui::GetIO().MouseDelta.x;
                node_layout.position.y += ImGui::GetIO().MouseDelta.y;
            }
            ImVec2 text_pos = ImVec2(node_screen_pos.x + 12.0f, node_screen_pos.y + 25.0f);
            draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), node.name.c_str());
        }

        // ====================================================================
        // THE DELETION [X] BUTTON
        // ====================================================================
        bool is_amp = (node.name == "Amp Sim"); 

        if (!is_amp) {
            ImVec2 cross_pos;
            if (node.routing_type == NodeRoutingType::StandardEffect) {
                // Standard pedals: Your exact original positioning
                cross_pos = ImVec2(node_screen_pos.x + node_width + 10.0f, node_screen_pos.y + 2.0f);
            } else {
                // Splitter & Mixer: Tuck the cross safely inside the colored box bounds
                cross_pos = ImVec2(node_screen_pos.x + node_width - 24.0f, node_screen_pos.y + 4.0f);
            }

            ImGui::SetCursorScreenPos(cross_pos);
            
            // Your exact color styling
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.1f, 0.1f, 0.8f));
            
            // Use SmallButton and exact string formatting
            std::string remove_label = "X##rm" + std::to_string(node.id);
            if (ImGui::SmallButton(remove_label.c_str())) {
                node_to_delete = node.id;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove %s from chain", node.name.c_str());
            }
            
            ImGui::PopStyleColor(2);
            ImGui::SetItemAllowOverlap();
        }

        // ====================================================================
        // FIX: THE WIRE DROP ZONE (Input Pins)
        // ====================================================================
        for (size_t idx = 0; idx < node.input_pin_ids.size(); ++idx) {
            int pin_id = node.input_pin_ids[idx];
            float pin_y = node_screen_pos.y + (node_height * (idx + 1.0f) / (node.input_pin_ids.size() + 1.0f));
            ImVec2 pin_pos(node_screen_pos.x - 2.0f, pin_y); 
            pin_positions_cache[pin_id] = pin_pos;

            draw_list->AddCircleFilled(pin_pos, 5.0f, IM_COL32(46, 204, 113, 255)); 
            draw_list->AddCircle(pin_pos, 6.5f, IM_COL32(255, 255, 255, 200));

            ImGui::SetCursorScreenPos(ImVec2(pin_pos.x - 10.0f, pin_pos.y - 10.0f));
            ImGui::PushID(pin_id);
            ImGui::InvisibleButton("in_pin", ImVec2(20.0f, 20.0f));
            
            // Check if hovered while releasing a dragged wire
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (ui_state.active_src_pin_id != -1) {
                    audio_graph.add_link(ui_state.active_src_pin_id, pin_id);
                    engine_.commit_graph_changes();
                    ui_state.active_src_pin_id = -1;
                }
            }
            ImGui::SetItemAllowOverlap();
            ImGui::PopID();
        }

        // ====================================================================
        // FIX: THE WIRE DRAG START (Output Pins)
        // ====================================================================
        for (size_t idx = 0; idx < node.output_pin_ids.size(); ++idx) {
            int pin_id = node.output_pin_ids[idx];
            float pin_y = node_screen_pos.y + (node_height * (idx + 1.0f) / (node.output_pin_ids.size() + 1.0f));
            ImVec2 pin_pos(node_screen_pos.x + node_width + 2.0f, pin_y);
            pin_positions_cache[pin_id] = pin_pos;

            // Track active wire position to snap to the pin perfectly
            if (ui_state.active_src_pin_id == pin_id) ui_state.active_src_pin_pos = pin_pos;

            draw_list->AddCircleFilled(pin_pos, 5.0f, IM_COL32(231, 76, 60, 255)); 
            draw_list->AddCircle(pin_pos, 6.5f, IM_COL32(255, 255, 255, 200));

            ImGui::SetCursorScreenPos(ImVec2(pin_pos.x - 10.0f, pin_pos.y - 10.0f));
            ImGui::PushID(pin_id);
            ImGui::InvisibleButton("out_pin", ImVec2(20.0f, 20.0f));
            
            // Start drafting wire instantly on Mouse DOWN
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ui_state.active_src_pin_id = pin_id;
            }
            ImGui::SetItemAllowOverlap();
            ImGui::PopID();
        }

        ImGui::PopID();
    }

    // Process Deletions safely after iterating
    if (node_to_delete != -1) {
        audio_graph.remove_node(node_to_delete);
        engine_.commit_graph_changes();
    }

    // Draw Patch Cables
    for (const auto& link : audio_graph.get_links()) {
        if (pin_positions_cache.count(link.source_pin_id) && pin_positions_cache.count(link.dest_pin_id)) {
            ImVec2 p1 = pin_positions_cache[link.source_pin_id];
            ImVec2 p2 = pin_positions_cache[link.dest_pin_id];
            
            ImVec2 cp1 = ImVec2(p1.x + 45.0f, p1.y);
            ImVec2 cp2 = ImVec2(p2.x - 45.0f, p2.y);
            draw_list->AddBezierCubic(p1, cp1, cp2, p2, IM_COL32(212, 175, 55, 255), 3.0f);
        }
    }

    // Draw Wire Spline Drafting
    if (ui_state.active_src_pin_id != -1) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        ImVec2 p1 = ui_state.active_src_pin_pos;
        ImVec2 cp1 = ImVec2(p1.x + 45.0f, p1.y);
        ImVec2 cp2 = ImVec2(mouse_pos.x - 45.0f, mouse_pos.y);
        draw_list->AddBezierCubic(p1, cp1, cp2, mouse_pos, IM_COL32(255, 255, 255, 160), 2.0f, 0);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ui_state.active_src_pin_id = -1; // Snap cable back if dropped in empty space
        }
    }

    draw_list->PopClipRect();
}

} // namespace Amplitron