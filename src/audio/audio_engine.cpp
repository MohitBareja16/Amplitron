#include "audio/audio_engine.h"
#include "audio/audio_backend.h"
#include <iostream>
#include <algorithm>

namespace Amplitron {

AudioEngine::AudioEngine() {
    process_buffer_.resize(MAX_BUFFER_SIZE, 0.0f);
    process_buffer_right_.resize(MAX_BUFFER_SIZE, 0.0f);
    backend_ = create_audio_backend();

    // Pre-allocate the graph memory pools immediately on startup
    main_executor_ = std::make_shared<AudioGraphExecutor>();
    // Assuming standard values, use your engine's actual sample rate / block size variables here
    main_executor_->prepare(48000, 512, 32); 
    
    // Seed the shadow executor so the audio thread has something safe to read instantly
    audio_shadow_executor_ = main_executor_;
}

AudioEngine::~AudioEngine() {
    shutdown();
    
    destroy_audio_backend(backend_);
    backend_ = nullptr;
}

void AudioEngine::set_buffer_size(int size) {
    size = std::max(MIN_BUFFER_SIZE, std::min(MAX_BUFFER_SIZE, size));
    int prev_size = buffer_size_;
    bool was_running = running_;
    if (was_running) stop();
    buffer_size_ = size;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with buffer size " + std::to_string(size) + ". Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            buffer_size_ = prev_size;
            start();
        } else {
            last_error_.clear();
        }
    }
}



void AudioEngine::set_sample_rate(int rate) {
    int prev_rate = sample_rate_;
    bool was_running = running_;
    if (was_running) stop();
    sample_rate_ = rate;
    
    {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        // FIX: Iterate over the nodes in the new AudioGraph
        for (const auto& node : main_graph_.get_nodes()) {
            if (node.pedal) { // Check if it's a standard effect and not a bare merge node
                node.pedal->set_sample_rate(rate);
                node.pedal->reset();
            }
        }
        if (tuner_tap_) {
            tuner_tap_->set_sample_rate(rate);
            tuner_tap_->reset();
        }
    }
    
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with sample rate " + std::to_string(rate) + " Hz. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            sample_rate_ = prev_rate;
            
            std::lock_guard<std::mutex> lock(effect_mutex_);
            // FIX: Revert the sample rates using the graph nodes
            for (const auto& node : main_graph_.get_nodes()) {
                if (node.pedal) {
                    node.pedal->set_sample_rate(prev_rate);
                    node.pedal->reset();
                }
            }
            if (tuner_tap_) {
                tuner_tap_->set_sample_rate(prev_rate);
                tuner_tap_->reset();
            }
            start();
        } else {
            last_error_.clear();
        }
    }
}
void AudioEngine::commit_graph_changes() {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    
    // 1. Create a brand new executor (so we don't mutate memory the audio thread is currently reading)
    auto new_executor = std::make_shared<AudioGraphExecutor>();
    new_executor->prepare(sample_rate_, buffer_size_, 32);
    
    // 2. Compile the latest UI graph into the new executor
    new_executor->compile(main_graph_);
    
    // 3. Promote it to the main slot. The audio thread will grab it on the next try_lock!
    main_executor_ = new_executor;
}
}