#include "audio/audio_engine.h"
#include <algorithm>

namespace Amplitron {

void AudioEngine::sync_graph_with_dummy_effects() {
    {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        
        // Check if there are any Mixer or Splitter nodes currently in the graph
        bool has_custom_routing = false;
        for (const auto& node : main_graph_.get_nodes()) {
            if (node.routing_type == NodeRoutingType::Mixer || node.routing_type == NodeRoutingType::Splitter) {
                has_custom_routing = true;
                break;
            }
        }
        
        if (!has_custom_routing) {
            // 1. Reset the main graph model completely
            main_graph_ = AudioGraph();
            
            int input_node_id = main_graph_.add_node("Input", NodeRoutingType::StandardEffect, nullptr);
            main_graph_.set_node_as_input(input_node_id, true);
            
            int prev_output_pin = main_graph_.get_nodes().back().output_pin_ids.empty() ? -1 : main_graph_.get_nodes().back().output_pin_ids[0];
            
            // 2. Loop through the linear pedals and wire them back-to-back in the DAG
            for (auto& fx : dummy_effects_) {
                fx->set_sample_rate(sample_rate_);
                fx->reset();
                int node_id = main_graph_.add_node(fx->name(), NodeRoutingType::StandardEffect, fx);
                
                if (std::string(fx->name()) == "Amp Sim") {
                    main_graph_.set_node_as_output(node_id, true);
                }
                
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
        } else {
            // We have a custom modular graph with Mixers. We must preserve Mixer nodes and custom links!
            // 1. Remove standard nodes that are no longer in dummy_effects_
            std::vector<int> nodes_to_remove;
            for (const auto& node : main_graph_.get_nodes()) {
                if (node.routing_type == NodeRoutingType::StandardEffect && node.pedal != nullptr) {
                    auto it = std::find(dummy_effects_.begin(), dummy_effects_.end(), node.pedal);
                    if (it == dummy_effects_.end()) {
                        nodes_to_remove.push_back(node.id);
                    }
                }
            }
            for (int nid : nodes_to_remove) {
                main_graph_.remove_node(nid);
            }
            
            // 2. Add standard nodes for effects in dummy_effects_ that are not yet in the graph
            for (int i = 0; i < static_cast<int>(dummy_effects_.size()); ++i) {
                auto& fx = dummy_effects_[i];
                // Find if a node already exists for this effect
                bool exists = false;
                for (const auto& node : main_graph_.get_nodes()) {
                    if (node.pedal == fx) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists) {
                    fx->set_sample_rate(sample_rate_);
                    fx->reset();
                    int node_id = main_graph_.add_node(fx->name(), NodeRoutingType::StandardEffect, fx);
                    if (std::string(fx->name()) == "Amp Sim") {
                        main_graph_.set_node_as_output(node_id, true);
                    }
                    
                    // Connect this new node into the graph!
                    // Find the "previous" node in dummy_effects_ (or Input node if i == 0)
                    int prev_node_id = -1;
                    if (i == 0) {
                        // Find Input node
                        for (const auto& node : main_graph_.get_nodes()) {
                            if (node.is_graph_input) {
                                prev_node_id = node.id;
                                break;
                            }
                        }
                    } else {
                        auto& prev_fx = dummy_effects_[i - 1];
                        for (const auto& node : main_graph_.get_nodes()) {
                            if (node.pedal == prev_fx) {
                                prev_node_id = node.id;
                                break;
                            }
                        }
                    }
                    
                    if (prev_node_id != -1) {
                        // Get the output pin of prev_node and input pin of new_node
                        int prev_out_pin = -1;
                        const auto* prev_node_ptr = main_graph_.find_node(prev_node_id);
                        if (prev_node_ptr && !prev_node_ptr->output_pin_ids.empty()) {
                            prev_out_pin = prev_node_ptr->output_pin_ids[0];
                        }
                        
                        int new_in_pin = -1;
                        const auto* new_node_ptr = main_graph_.find_node(node_id);
                        if (new_node_ptr && !new_node_ptr->input_pin_ids.empty()) {
                            new_in_pin = new_node_ptr->input_pin_ids[0];
                        }
                        
                        if (prev_out_pin != -1 && new_in_pin != -1) {
                            // Find any links originating from prev_out_pin
                            std::vector<GraphLink> outgoing_links;
                            for (const auto& link : main_graph_.get_links()) {
                                if (link.source_pin_id == prev_out_pin) {
                                    outgoing_links.push_back(link);
                                }
                            }
                            
                            // Redirect those links to originate from the new node's output pin instead!
                            int new_out_pin = -1;
                            if (new_node_ptr && !new_node_ptr->output_pin_ids.empty()) {
                                new_out_pin = new_node_ptr->output_pin_ids[0];
                            }
                            
                            for (const auto& l : outgoing_links) {
                                main_graph_.remove_link(l.id);
                                if (new_out_pin != -1) {
                                    main_graph_.add_link(new_out_pin, l.dest_pin_id);
                                }
                            }
                            
                            // Connect prev_node -> new_node
                            main_graph_.add_link(prev_out_pin, new_in_pin);
                        }
                    }
                }
            }
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
