#include "audio/audio_graph.h"
#include <deque>
#include <unordered_map>

namespace Amplitron {

int AudioGraph::add_node(const std::string& name, NodeRoutingType type, std::shared_ptr<Effect> pedal) {
    DSPNode node;
    node.id = next_id_++; // Uses your unified member counter
    node.name = name;
    node.routing_type = type;
    node.pedal = pedal;

    // Dynamically configure pin structures using the same unified ID pool
    if (type == NodeRoutingType::Splitter) {
    node.input_pin_ids.push_back(next_id_++);  // 1 Input Pin
    node.output_pin_ids.push_back(next_id_++); // Output Pin Branch A
    node.output_pin_ids.push_back(next_id_++); // Output Pin Branch B
} else if (type == NodeRoutingType::Mixer || type == NodeRoutingType::MergeSum) {
    node.input_pin_ids.push_back(next_id_++);  // Input Pin Branch A
    node.input_pin_ids.push_back(next_id_++);  // Input Pin Branch B
    node.output_pin_ids.push_back(next_id_++); // 1 Output Pin
} else {
    node.input_pin_ids.push_back(next_id_++);  
    node.output_pin_ids.push_back(next_id_++); 
}

    nodes_.push_back(node);
    
    // Auto-recompile topology order whenever a structural block changes
    rebuild_topology();
    
    return node.id;
}

int AudioGraph::add_link(int source_pin_id, int dest_pin_id) {
    // Prevent duplicate connections between the exact same pair of pins
    for (const auto& existing_link : links_) {
        if (existing_link.source_pin_id == source_pin_id && existing_link.dest_pin_id == dest_pin_id) {
            return existing_link.id; 
        }
    }

    GraphLink link;
    link.id = next_id_++; // Uses your unified member counter
    link.source_pin_id = source_pin_id;
    link.dest_pin_id = dest_pin_id;
    
    links_.push_back(link);

    // Validate if the new patch wire forms an impossible audio loop feedback cycle
    if (!rebuild_topology()) {
        // If a feedback loop is detected, pop the dangerous link back off to keep the engine safe
        links_.pop_back();
        return -1; 
    }

    return link.id;
}

int AudioGraph::get_node_from_pin(int pin_id) const {
    // Search through all nodes to find which one owns the given Pin ID
    for (const auto& node : nodes_) {
        for (int p : node.input_pin_ids) {
            if (p == pin_id) return node.id;
        }
        for (int p : node.output_pin_ids) {
            if (p == pin_id) return node.id;
        }
    }
    return -1; // Pin ID not found in any registered node
}

bool AudioGraph::rebuild_topology() {
    // Kahn's algorithm or DFS to topologically sort the nodes based on links.
    // Since your test suite cases are already passing, we can use a basic 
    // Kahn's sort dependency tracker to map links to execution order.
    
    sorted_node_ids_.clear();
    std::unordered_map<int, int> in_degree;
    
    // Initialize in-degree count for all active nodes
    for (const auto& node : nodes_) {
        in_degree[node.id] = 0;
    }
    
    // Calculate how many incoming cables are hooked up to each node
    for (const auto& link : links_) {
        int dest_node = get_node_from_pin(link.dest_pin_id);
        if (dest_node != -1) {
            in_degree[dest_node]++;
        }
    }
    
    // Gather all source nodes that have 0 dependencies
    std::vector<int> process_queue;
    for (const auto& node : nodes_) {
        if (in_degree[node.id] == 0) {
            process_queue.push_back(node.id);
        }
    }
    
    // Topologically extract nodes from the dependency queue
    size_t head = 0;
    while (head < process_queue.size()) {
        int current_node_id = process_queue[head++];
        sorted_node_ids_.push_back(current_node_id);
        
        // Decrement dependencies for downstream targets linked to this node
        for (const auto& node : nodes_) {
            if (node.id != current_node_id) continue;
            
            for (int out_pin : node.output_pin_ids) {
                for (const auto& link : links_) {
                    if (link.source_pin_id == out_pin) {
                        int target_node = get_node_from_pin(link.dest_pin_id);
                        if (target_node != -1) {
                            in_degree[target_node]--;
                            if (in_degree[target_node] == 0) {
                                process_queue.push_back(target_node);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // If the sorted list length doesn't match total nodes, an impossible feedback loop exists!
    if (sorted_node_ids_.size() != nodes_.size()) {
        return false; // Rejects connection modifications to protect engine stability
    }
    
    return true; // Topology built successfully!
}

bool AudioGraph::remove_node(int node_id) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(), 
                           [node_id](const DSPNode& n) { return n.id == node_id; });
    
    if (it != nodes_.end()) {
        // 1. Destroy all cables attached to this node's Input Pins
        for (int pin : it->input_pin_ids) {
            links_.erase(std::remove_if(links_.begin(), links_.end(), 
                [pin](const GraphLink& l) { return l.dest_pin_id == pin; }), links_.end());
        }
        // 2. Destroy all cables attached to this node's Output Pins
        for (int pin : it->output_pin_ids) {
            links_.erase(std::remove_if(links_.begin(), links_.end(), 
                [pin](const GraphLink& l) { return l.source_pin_id == pin; }), links_.end());
        }
        
        // 3. Erase the node and recompile the audio thread topology
        nodes_.erase(it);
        rebuild_topology();
        return true;
    }
    return false;
}

} // namespace Amplitron