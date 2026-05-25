// =============================================================================
// PortAudio backend — lifecycle management
// =============================================================================

#include "audio/audio_engine.h"
#include "audio/audio_backend.h"
#include "audio/audio_backend_portaudio_helpers.h"
#include "audio/audio_backend_portaudio_internal.h"
#include <portaudio.h>
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif
#include <cstring>
#include <iostream>
#include <vector>
#include <algorithm>

namespace Amplitron {

// Define the mock seams
int (*g_mock_pa_get_device_count)() = nullptr;
const PaDeviceInfo* (*g_mock_pa_get_device_info)(int) = nullptr;
const PaHostApiInfo* (*g_mock_pa_get_host_api_info)(int) = nullptr;
int (*g_mock_pa_get_host_api_count)() = nullptr;
int (*g_mock_pa_host_api_device_index_to_device_index)(int, int) = nullptr;
int (*g_mock_pa_get_default_input_device)() = nullptr;
int (*g_mock_pa_get_default_output_device)() = nullptr;
PaError (*g_mock_pa_open_stream)(PaStream**, const PaStreamParameters*, const PaStreamParameters*, double, unsigned long, PaStreamFlags, PaStreamCallback*, void*) = nullptr;
PaError (*g_mock_pa_start_stream)(PaStream*) = nullptr;
const PaStreamInfo* (*g_mock_pa_get_stream_info)(PaStream*) = nullptr;
PaError (*g_mock_pa_initialize)() = nullptr;

// Helper macros to transparently use mocks if set
#define MOCK_OR_REAL_GET_DEVICE_COUNT() (g_mock_pa_get_device_count ? g_mock_pa_get_device_count() : Pa_GetDeviceCount())
#define MOCK_OR_REAL_GET_DEVICE_INFO(i) (g_mock_pa_get_device_info ? g_mock_pa_get_device_info(i) : Pa_GetDeviceInfo(i))
#define MOCK_OR_REAL_GET_HOST_API_INFO(i) (g_mock_pa_get_host_api_info ? g_mock_pa_get_host_api_info(i) : Pa_GetHostApiInfo(i))
#define MOCK_OR_REAL_GET_HOST_API_COUNT() (g_mock_pa_get_host_api_count ? g_mock_pa_get_host_api_count() : Pa_GetHostApiCount())
#define MOCK_OR_REAL_HOST_API_DEVICE_INDEX(a, d) (g_mock_pa_host_api_device_index_to_device_index ? g_mock_pa_host_api_device_index_to_device_index(a, d) : Pa_HostApiDeviceIndexToDeviceIndex(a, d))
#define MOCK_OR_REAL_GET_DEFAULT_INPUT() (g_mock_pa_get_default_input_device ? g_mock_pa_get_default_input_device() : Pa_GetDefaultInputDevice())
#define MOCK_OR_REAL_GET_DEFAULT_OUTPUT() (g_mock_pa_get_default_output_device ? g_mock_pa_get_default_output_device() : Pa_GetDefaultOutputDevice())
#define MOCK_OR_REAL_INITIALIZE() (g_mock_pa_initialize ? g_mock_pa_initialize() : Pa_Initialize())



// PortAudio callback
int pa_audio_callback(const void* input, void* output,
                              unsigned long frame_count,
                              const PaStreamCallbackTimeInfo* /*time_info*/,
                              PaStreamCallbackFlags /*status_flags*/,
                              void* user_data) {
    auto* engine = static_cast<AudioEngine*>(user_data);
    const auto* in = static_cast<const float*>(input);
    auto* out = static_cast<float*>(output);

    if (!in || !out) {
        if (out) std::memset(out, 0, frame_count * 2 * sizeof(float));
        return paContinue;
    }

    engine->process_audio(in, out, static_cast<int>(frame_count));
    return paContinue;
}

// Device auto-detection
static void auto_detect_devices(int& input_device, int& output_device, int& sample_rate) {
    int device_count = MOCK_OR_REAL_GET_DEVICE_COUNT();
    int num_apis = MOCK_OR_REAL_GET_HOST_API_COUNT();

    // Phase 1: Print all devices
    std::cout << "\nDetected audio devices:" << std::endl;
    for (int i = 0; i < device_count; ++i) {
        const PaDeviceInfo* info = MOCK_OR_REAL_GET_DEVICE_INFO(i);
        if (!info) continue;
        bool is_usb = is_usb_device_name(info->name);
        const PaHostApiInfo* api = MOCK_OR_REAL_GET_HOST_API_INFO(info->hostApi);
        const char* api_name = api ? api->name : "Unknown";
        if (info->maxInputChannels > 0) {
            std::cout << "  [IN]  " << info->name
                      << " (" << api_name << ")"
                      << (is_usb ? " [USB]" : "") << std::endl;
        }
        if (info->maxOutputChannels > 0) {
            std::cout << "  [OUT] " << info->name
                      << " (" << api_name << ")"
                      << (is_usb ? " [USB]" : "") << std::endl;
        }
    }

    // Phase 2: For each host API (ranked by priority), find the best
    // USB input + non-USB/non-projector output PAIR on the SAME API.
    struct ApiCandidate {
        int api_index;
        int priority;
        int usb_input;
        int best_output;
        std::string api_name;
    };

    std::vector<ApiCandidate> candidates;
    for (int a = 0; a < num_apis; ++a) {
        const PaHostApiInfo* api = MOCK_OR_REAL_GET_HOST_API_INFO(a);
        if (!api) continue;

        ApiCandidate c;
        c.api_index = a;
        c.priority = get_host_api_priority(api->type);
        c.usb_input = -1;
        c.best_output = -1;
        c.api_name = api->name;

        for (int d = 0; d < api->deviceCount; ++d) {
            int dev_idx = MOCK_OR_REAL_HOST_API_DEVICE_INDEX(a, d);
            const PaDeviceInfo* info = MOCK_OR_REAL_GET_DEVICE_INFO(dev_idx);
            if (!info) continue;

            bool is_usb = is_usb_device_name(info->name);

            if (is_usb && info->maxInputChannels > 0 && c.usb_input < 0) {
                c.usb_input = dev_idx;
            }
            if (!is_usb && !is_projector_or_hdmi(info->name)
                && info->maxOutputChannels > 0 && c.best_output < 0) {
                c.best_output = dev_idx;
            }
        }

        if (c.best_output < 0) {
            for (int d = 0; d < api->deviceCount; ++d) {
                int dev_idx = MOCK_OR_REAL_HOST_API_DEVICE_INDEX(a, d);
                const PaDeviceInfo* info = MOCK_OR_REAL_GET_DEVICE_INFO(dev_idx);
                if (!info) continue;
                if (!is_usb_device_name(info->name) && info->maxOutputChannels > 0) {
                    c.best_output = dev_idx;
                    break;
                }
            }
        }

        candidates.push_back(c);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ApiCandidate& a, const ApiCandidate& b) {
                  return a.priority > b.priority;
              });

    // Phase 3: Pick the best API that has BOTH a USB input AND an output
    bool found_pair = false;
    for (auto& c : candidates) {
        if (c.usb_input >= 0 && c.best_output >= 0) {
            input_device = c.usb_input;
            output_device = c.best_output;

            const PaDeviceInfo* in_info = MOCK_OR_REAL_GET_DEVICE_INFO(input_device);
            const PaDeviceInfo* out_info = MOCK_OR_REAL_GET_DEVICE_INFO(output_device);

            std::cout << "\n>> Auto-selected (same API: " << c.api_name << "):" << std::endl;
            std::cout << "   INPUT:  " << in_info->name << " [USB Guitar Cable]" << std::endl;
            std::cout << "   OUTPUT: " << out_info->name << " [Speakers]" << std::endl;

            if (in_info->defaultSampleRate > 0) {
                sample_rate = static_cast<int>(in_info->defaultSampleRate);
                std::cout << "   Rate:   " << sample_rate << " Hz (device native)" << std::endl;
            }

            found_pair = true;
            break;
        }
    }

    // Fallback: no USB input found
    if (!found_pair) {
        for (auto& c : candidates) {
            if (c.best_output >= 0) {
                const PaHostApiInfo* api = MOCK_OR_REAL_GET_HOST_API_INFO(c.api_index);
                for (int d = 0; d < api->deviceCount; ++d) {
                    int dev_idx = MOCK_OR_REAL_HOST_API_DEVICE_INDEX(c.api_index, d);
                    const PaDeviceInfo* info = MOCK_OR_REAL_GET_DEVICE_INFO(dev_idx);
                    if (info && info->maxInputChannels > 0) {
                        input_device = dev_idx;
                        output_device = c.best_output;

                        std::cout << "\n>> No USB guitar cable detected." << std::endl;
                        std::cout << "   Using " << c.api_name << " defaults:" << std::endl;
                        std::cout << "   INPUT:  " << info->name << std::endl;
                        std::cout << "   OUTPUT: " << MOCK_OR_REAL_GET_DEVICE_INFO(output_device)->name << std::endl;

                        found_pair = true;
                        break;
                    }
                }
                if (found_pair) break;
            }
        }
    }

    // Last resort: system defaults
    if (!found_pair) {
        input_device = MOCK_OR_REAL_GET_DEFAULT_INPUT();
        output_device = MOCK_OR_REAL_GET_DEFAULT_OUTPUT();
        std::cout << "\n>> Using system default input/output devices." << std::endl;
    }
}

bool AudioEngine::initialize() {
    PaError err = MOCK_OR_REAL_INITIALIZE();
    if (err != paNoError) {
        std::cerr << "PortAudio init failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    initialized_ = true;

    auto_detect_devices(input_device_, output_device_, sample_rate_);

    return true;
}

void AudioEngine::shutdown() {
    stop();
    if (initialized_) {
        Pa_Terminate();
        initialized_ = false;
    }
}



bool AudioEngine::start() {
    if (!initialized_ || running_) return false;

    const PaDeviceInfo* in_dev = MOCK_OR_REAL_GET_DEVICE_INFO(input_device_);
    const PaDeviceInfo* out_dev = MOCK_OR_REAL_GET_DEVICE_INFO(output_device_);
    (void)in_dev; (void)out_dev;

    double desired_latency = static_cast<double>(buffer_size_) / sample_rate_;

    PaStreamParameters input_params;
    input_params.device = input_device_;
    input_params.channelCount = 1;
    input_params.sampleFormat = paFloat32;
    input_params.suggestedLatency = desired_latency;
    input_params.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters output_params;
    output_params.device = output_device_;
    output_params.channelCount = 2;
    output_params.sampleFormat = paFloat32;
    output_params.suggestedLatency = desired_latency;
    output_params.hostApiSpecificStreamInfo = nullptr;

#ifdef _WIN32
    PaWasapiStreamInfo wasapi_in_info = {};
    PaWasapiStreamInfo wasapi_out_info = {};
    if (in_dev) {
        const PaHostApiInfo* api = MOCK_OR_REAL_GET_HOST_API_INFO(in_dev->hostApi);
        if (api && api->type == paWASAPI) {
            wasapi_in_info.size = sizeof(PaWasapiStreamInfo);
            wasapi_in_info.hostApiType = paWASAPI;
            wasapi_in_info.version = 1;
            wasapi_in_info.flags = paWinWasapiExclusive;
            input_params.hostApiSpecificStreamInfo = &wasapi_in_info;

            wasapi_out_info.size = sizeof(PaWasapiStreamInfo);
            wasapi_out_info.hostApiType = paWASAPI;
            wasapi_out_info.version = 1;
            wasapi_out_info.flags = paWinWasapiExclusive;
            output_params.hostApiSpecificStreamInfo = &wasapi_out_info;

            std::cout << "  Using WASAPI Exclusive Mode" << std::endl;
        }
    }
#endif

    unsigned long frames = static_cast<unsigned long>(buffer_size_);

    PaError err = g_mock_pa_open_stream ? g_mock_pa_open_stream(&backend_->stream, &input_params, &output_params, sample_rate_, frames, paClipOff | paDitherOff, pa_audio_callback, this) : Pa_OpenStream(
        &backend_->stream,
        &input_params,
        &output_params,
        sample_rate_,
        frames,
        paClipOff | paDitherOff,
        pa_audio_callback,
        this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
        std::cerr << "Retrying with buffer size " << buffer_size_ << "..." << std::endl;
        err = g_mock_pa_open_stream ? g_mock_pa_open_stream(&backend_->stream, &input_params, &output_params, sample_rate_, buffer_size_, paClipOff | paDitherOff, pa_audio_callback, this) : Pa_OpenStream(
            &backend_->stream,
            &input_params,
            &output_params,
            sample_rate_,
            buffer_size_,
            paClipOff | paDitherOff,
            pa_audio_callback,
            this
        );
        if (err != paNoError) {
            std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    const PaStreamInfo* si = g_mock_pa_get_stream_info ? g_mock_pa_get_stream_info(backend_->stream) : Pa_GetStreamInfo(backend_->stream);
    if (si && si->sampleRate > 0.0) {
        const int actual_rate = static_cast<int>(si->sampleRate + 0.5);
        if (actual_rate != sample_rate_) {
            sample_rate_ = actual_rate;
            update_metronome_timing();
            std::lock_guard<std::mutex> lock(effect_mutex_);
            for (const auto& node : main_graph_.get_nodes()) {
                if (node.pedal) {
                    node.pedal->set_sample_rate(sample_rate_);
                    node.pedal->reset();
                }
            }
            if (tuner_tap_) {
                tuner_tap_->set_sample_rate(sample_rate_);
                tuner_tap_->reset();
            }
        }
    }

    err = g_mock_pa_start_stream ? g_mock_pa_start_stream(backend_->stream) : Pa_StartStream(backend_->stream);
    if (err != paNoError) {
        std::cerr << "Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(backend_->stream);
        backend_->stream = nullptr;
        return false;
    }

    running_ = true;
    si = g_mock_pa_get_stream_info ? g_mock_pa_get_stream_info(backend_->stream) : Pa_GetStreamInfo(backend_->stream);
    const PaDeviceInfo* in_info = MOCK_OR_REAL_GET_DEVICE_INFO(input_device_);
    const PaDeviceInfo* out_info = MOCK_OR_REAL_GET_DEVICE_INFO(output_device_);
    std::cout << "Audio stream started:" << std::endl;
    std::cout << "  Input:   " << (in_info ? in_info->name : "Unknown") << std::endl;
    std::cout << "  Output:  " << (out_info ? out_info->name : "Unknown") << std::endl;
    std::cout << "  Rate:    " << (si ? si->sampleRate : sample_rate_) << " Hz" << std::endl;
    if (si) {
        std::cout << "  Latency: in=" << (si->inputLatency * 1000.0) << " ms"
                  << "  out=" << (si->outputLatency * 1000.0) << " ms" << std::endl;
    }
    return true;
}

void AudioEngine::stop() {
    if (backend_->stream) {
        if (running_) {
            Pa_StopStream(backend_->stream);
            running_ = false;
        }
        Pa_CloseStream(backend_->stream);
        backend_->stream = nullptr;
    }
}

bool AudioEngine::restart() {
    stop();
    bool ok = start();
    if (!ok) {
        last_error_ = "Failed to restart audio stream. Check device settings.";
        std::cerr << "[Amplitron] " << last_error_ << std::endl;
    } else {
        last_error_.clear();
    }
    return ok;
}

} // namespace Amplitron
