#include "audio/audio_graph_executor.h"
#include <algorithm>
#include <cstring>

namespace Amplitron {

AudioGraphExecutor::AudioGraphExecutor() {}

void AudioGraphExecutor::prepare(int sample_rate, int max_block_size, int max_nodes) {
    sample_rate_ = sample_rate;
    max_block_size_ = max_block_size;
    max_nodes_ = max_nodes;

    // Pre-allocate the memory pool to guarantee no allocations on the audio thread
    buffer_pool_.resize(max_nodes_, std::vector<float>(max_block_size_, 0.0f));
    sum_buffer_.resize(max_block_size_, 0.0f);
    execution_plan_.reserve(max_nodes_);
}

void AudioGraphExecutor::compile(const AudioGraph& graph) {
    execution_plan_.clear();
    const auto& sorted_ids = graph.get_sorted_nodes();
    const auto& nodes = graph.get_nodes();
    const auto& links = graph.get_links();

    if (sorted_ids.empty() || sorted_ids.size() > (size_t)max_nodes_) return;

    // Map each Node ID to a dedicated row in the buffer pool
    std::unordered_map<int, int> node_to_buffer;
    for (size_t i = 0; i < sorted_ids.size(); ++i) {
        node_to_buffer[sorted_ids[i]] = static_cast<int>(i);
    }

    // Build the flat execution array
    for (int node_id : sorted_ids) {
        auto it = std::find_if(nodes.begin(), nodes.end(), [&](const DSPNode& n){ return n.id == node_id; });
        if (it == nodes.end()) continue;

        NodeExecutionStep step;
        step.node_id = node_id;
        step.buffer_index = node_to_buffer[node_id];
        step.type = it->routing_type;
        step.pedal = it->pedal;

        // Trace upstream connections to find which buffers to read from
        for (int in_pin : it->input_pin_ids) {
            for (const auto& link : links) {
                if (link.dest_pin_id == in_pin) {
                    int src_node_id = graph.get_node_from_pin(link.source_pin_id);
                    if (src_node_id != -1 && node_to_buffer.count(src_node_id)) {
                        step.input_sources.push_back({ node_to_buffer[src_node_id] });
                    }
                }
            }
        }
        execution_plan_.push_back(step);
    }
}

void AudioGraphExecutor::process(const float* input, float* output, int num_samples) {
    if (execution_plan_.empty()) {
        std::memcpy(output, input, num_samples * sizeof(float));
        return;
    }

    for (const auto& step : execution_plan_) {
        float* node_input = sum_buffer_.data();

        if (step.input_sources.empty()) {
            // Root node: Takes the master guitar input
            std::memcpy(node_input, input, num_samples * sizeof(float));
        } else {
            // Summation: Additively mix all incoming paths
            std::memset(node_input, 0, num_samples * sizeof(float));
            for (const auto& src : step.input_sources) {
                const float* src_buf = buffer_pool_[src.buffer_index].data();
                for (int i = 0; i < num_samples; ++i) {
                    node_input[i] += src_buf[i];
                }
            }
        }

        float* node_output = buffer_pool_[step.buffer_index].data();

        if (step.type == NodeRoutingType::StandardEffect && step.pedal) {
            // FIX: In-place processing! 
            // 1. Copy the summed input data into our dedicated output buffer
            std::memcpy(node_output, node_input, num_samples * sizeof(float));
            
            // 2. Tell the pedal to process and overwrite that buffer directly
            step.pedal->process(node_output, num_samples); 
        } else {
            // Merge nodes or empty wrappers just pass the summed input directly downstream
            std::memcpy(node_output, node_input, num_samples * sizeof(float));
        }
    }

    // Output of the graph is whatever the final node in the topological chain produced
    const float* final_buf = buffer_pool_[execution_plan_.back().buffer_index].data();
    std::memcpy(output, final_buf, num_samples * sizeof(float));
}

} // namespace Amplitron