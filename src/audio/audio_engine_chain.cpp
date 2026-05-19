#include "audio/audio_engine.h"
#include <algorithm>

namespace Amplitron {

void AudioEngine::sync_graph_with_dummy_effects() {
    // 1. Reset the main graph model completely
    main_graph_ = AudioGraph();
    int prev_output_pin = -1;
    
    // 2. Loop through the linear pedals and wire them back-to-back in the DAG
    for (auto& fx : dummy_effects_) {
        int node_id = main_graph_.add_node(fx->name(), NodeRoutingType::StandardEffect, fx);
        
        const auto& nodes = main_graph_.get_nodes();
        if (nodes.empty()) continue;
        const auto& current_node = nodes.back();
        
        // Connect the previous pedal's output pin to this pedal's input pin
        if (prev_output_pin != -1 && !current_node.input_pin_ids.empty()) {
            main_graph_.add_link(prev_output_pin, current_node.input_pin_ids[0]);
        }
        
        // Track this pedal's output pin for the next connection downstream
        if (!current_node.output_pin_ids.empty()) {
            prev_output_pin = current_node.output_pin_ids[0];
        }
    }
    
    // 3. Compile the topology plan and push it to the hot audio thread safely
    commit_graph_changes();
}

void AudioEngine::add_effect(std::shared_ptr<Effect> fx) {
    dummy_effects_.push_back(fx);
    sync_graph_with_dummy_effects();
}

void AudioEngine::insert_effect(int index, std::shared_ptr<Effect> fx) {
    if (index >= 0 && index <= static_cast<int>(dummy_effects_.size())) {
        dummy_effects_.insert(dummy_effects_.begin() + index, fx);
        sync_graph_with_dummy_effects();
    }
}

void AudioEngine::remove_effect(int index) {
    if (index >= 0 && index < static_cast<int>(dummy_effects_.size())) {
        dummy_effects_.erase(dummy_effects_.begin() + index);
        sync_graph_with_dummy_effects();
    }
}

void AudioEngine::move_effect(int from, int to) {
    int size = static_cast<int>(dummy_effects_.size());
    if (from < 0 || from >= size || to < 0 || to >= size) {
        return;
    }
    
    if (from == to) return;

    auto fx = dummy_effects_[from];
    dummy_effects_.erase(dummy_effects_.begin() + from);
    dummy_effects_.insert(dummy_effects_.begin() + to, fx);
    
    sync_graph_with_dummy_effects();
}

void AudioEngine::restore_effects_state(std::vector<std::shared_ptr<Effect>> state) {
    dummy_effects_ = state;
    sync_graph_with_dummy_effects();
}

void AudioEngine::set_tuner_tap(std::shared_ptr<Effect> tap) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    tuner_tap_ = std::move(tap);
    if (tuner_tap_) {
        tuner_tap_->set_sample_rate(sample_rate_);
        tuner_tap_->reset();
    }
    topology_dirty_.store(true, std::memory_order_release);
}

void AudioEngine::clear_tuner_tap() {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    tuner_tap_.reset();
    topology_dirty_.store(true, std::memory_order_release);
}

bool AudioEngine::has_tuner_tap() const {
    return tuner_tap_ != nullptr;
}

} // namespace Amplitron
