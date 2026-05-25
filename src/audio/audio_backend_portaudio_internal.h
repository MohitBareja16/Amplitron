#pragma once

#include <portaudio.h>

namespace Amplitron {

/**
 * @brief Opaque state for PortAudio backend.
 * Defined here so all portaudio split files can access it.
 */
struct AudioBackendState {
    PaStream* stream = nullptr;
};

// Test injection seams for PortAudio
extern int (*g_mock_pa_get_device_count)();
extern const PaDeviceInfo* (*g_mock_pa_get_device_info)(int);
extern const PaHostApiInfo* (*g_mock_pa_get_host_api_info)(int);
extern int (*g_mock_pa_get_host_api_count)();
extern int (*g_mock_pa_host_api_device_index_to_device_index)(int, int);
extern int (*g_mock_pa_get_default_input_device)();
extern int (*g_mock_pa_get_default_output_device)();

extern PaError (*g_mock_pa_open_stream)(PaStream**, const PaStreamParameters*, const PaStreamParameters*, double, unsigned long, PaStreamFlags, PaStreamCallback*, void*);
extern PaError (*g_mock_pa_start_stream)(PaStream*);
extern PaError (*g_mock_pa_stop_stream)(PaStream*);
extern PaError (*g_mock_pa_close_stream)(PaStream*);
extern const PaStreamInfo* (*g_mock_pa_get_stream_info)(PaStream*);
extern PaError (*g_mock_pa_initialize)();

} // namespace Amplitron
