#include "test_framework.h"
#include "audio/audio_engine.h"
#include "audio/audio_backend_portaudio_helpers.h"
#include <portaudio.h>
#include <cstring>

using namespace Amplitron;
using namespace TestFramework;

TEST(PortAudioDevices_IsUsbDeviceDetectsUsbInName) {
    ASSERT_TRUE(is_usb_device_name("USB Audio Device"));
    ASSERT_TRUE(is_usb_device_name("Behringer UCG102 [USB]"));
    ASSERT_TRUE(is_usb_device_name("Focusrite Scarlett Solo"));
    ASSERT_FALSE(is_usb_device_name("Built-in Microphone"));
    ASSERT_FALSE(is_usb_device_name("Realtek High Definition Audio"));
}

TEST(PortAudioDevices_ProjectorDetectsDisplayPort) {
    ASSERT_TRUE(is_projector_or_hdmi("Epson Projector"));
    ASSERT_TRUE(is_projector_or_hdmi("HDMI Display"));
    ASSERT_TRUE(is_projector_or_hdmi("DisplayPort Output"));
    ASSERT_FALSE(is_projector_or_hdmi("Built-in Output"));
}

TEST(PortAudioDevices_HostApiPriority) {
    ASSERT_GT(get_host_api_priority(paJACK), 0);
    ASSERT_GT(get_host_api_priority(paALSA), 0);
    ASSERT_GT(get_host_api_priority(paWASAPI), 0);
    ASSERT_GT(get_host_api_priority(paCoreAudio), 0);
    ASSERT_GT(get_host_api_priority(paInDevelopment), 0);
}

TEST(PortAudioLifecycle_OpenStreamWithNullDevice) {
    AudioEngine engine;
    
    // Force invalid devices
    bool ok_in = engine.set_input_device(paNoDevice);
    bool ok_out = engine.set_output_device(paNoDevice);
    ASSERT_FALSE(ok_in);
    ASSERT_FALSE(ok_out);

    // Should fail because initialized_ is false
    bool started = engine.start();
    ASSERT_FALSE(started);
    ASSERT_FALSE(engine.is_running());
}

TEST(PortAudioLifecycle_StartStopStartCycle) {
    AudioEngine engine;
    engine.initialize();

    bool started = engine.start();
    if (started) {
        ASSERT_TRUE(engine.is_running());
        engine.stop();
        ASSERT_FALSE(engine.is_running());
        
        bool restarted = engine.start();
        ASSERT_TRUE(restarted);
        ASSERT_TRUE(engine.is_running());
    }

    engine.shutdown();
}

TEST(PortAudioLifecycle_Restart) {
    AudioEngine engine;
    engine.initialize();
    engine.restart();
    engine.shutdown();
}

TEST(PortAudioDevices_EnumerateDevicesReturnsVector) {
    AudioEngine engine;
    engine.initialize();

    auto in_devs = engine.get_input_devices();
    auto out_devs = engine.get_output_devices();

    std::string in_name = engine.get_input_device_name();
    std::string out_name = engine.get_output_device_name();
    
    ASSERT_FALSE(in_name.empty());
    ASSERT_FALSE(out_name.empty());
    
    engine.shutdown();
}

TEST(PortAudioDevices_DeviceNamesUninitialized) {
    AudioEngine engine; // uninitialized, input_device_ is -1
    ASSERT_EQ(engine.get_input_device_name(), "None");
    ASSERT_EQ(engine.get_output_device_name(), "None");
}

TEST(PortAudioDevices_DevicesShareHostApiSafe) {
    bool shared = devices_share_host_api(paNoDevice, paNoDevice);
    ASSERT_FALSE(shared);
}

TEST(PortAudioDevices_SetDeviceBranchCoverage) {
    Pa_Initialize(); // Manual init to keep valid state
    
    AudioEngine engine_valid;
    engine_valid.initialize();
    
    auto in_devs = engine_valid.get_input_devices();
    auto out_devs = engine_valid.get_output_devices();
    
    if (!in_devs.empty() && !out_devs.empty()) {
        int valid_in = in_devs[0].index;
        int valid_out = out_devs[0].index;

        // Hit maxChannels < 1 branches
        int output_only = -1;
        for (const auto& dev : out_devs) {
            if (dev.max_input_channels == 0) {
                output_only = dev.index;
                break;
            }
        }
        if (output_only >= 0) {
            engine_valid.set_input_device(output_only);
        }

        int input_only = -1;
        for (const auto& dev : in_devs) {
            if (dev.max_output_channels == 0) {
                input_only = dev.index;
                break;
            }
        }
        if (input_only >= 0) {
            engine_valid.set_output_device(input_only);
        }

        // 1. Hit !devices_share_host_api by leaving output as -1
        AudioEngine engine_uninit;
        engine_uninit.set_input_device(valid_in);
        engine_uninit.set_output_device(valid_out); // also hits !share when input was just set
    }
    
    Pa_Terminate();
}

namespace Amplitron {
class PortAudioTestSaboteur {
public:
    static void sabotage_and_test() {
        AudioEngine engine;
        engine.initialize();
        
        auto in_devs = engine.get_input_devices();
        auto out_devs = engine.get_output_devices();
        
        if (!in_devs.empty() && !out_devs.empty()) {
            if (engine.start()) {
                // Sabotage the engine so that the next start() fails!
                engine.initialized_ = false;
                
                // Try to set input device. It will stop, set, and then try to start() again.
                // start() will immediately return false because initialized_ is false.
                // It will then hit the revert logic and fail to revert too (since initialized_ is still false).
                bool in_ok = engine.set_input_device(in_devs[0].index);
                ASSERT_FALSE(in_ok);
                
                engine.initialized_ = true;
            }
            
            if (engine.start()) {
                engine.initialized_ = false;
                
                bool out_ok = engine.set_output_device(out_devs[0].index);
                ASSERT_FALSE(out_ok);
                
                engine.initialized_ = true; // Restore for clean shutdown
            }
        }
    }
};
} // namespace Amplitron

TEST(PortAudioDevices_SetDeviceSabotage) {
    PortAudioTestSaboteur::sabotage_and_test();
}

namespace Amplitron {
extern int pa_audio_callback(const void* input, void* output,
                             unsigned long frame_count,
                             const PaStreamCallbackTimeInfo* time_info,
                             PaStreamCallbackFlags status_flags,
                             void* user_data);
}

TEST(PortAudioLifecycle_CallbackCoverage) {
    AudioEngine engine;
    engine.initialize();
    
    float in_buf[64] = {0};
    float out_buf[128] = {0};
    
    // Normal processing
    pa_audio_callback(in_buf, out_buf, 64, nullptr, 0, &engine);
    
    // Missing input triggers silence fill
    out_buf[0] = 1.0f;
    pa_audio_callback(nullptr, out_buf, 64, nullptr, 0, &engine);
    ASSERT_EQ(out_buf[0], 0.0f); // Verify memset
    
    // Missing output skips processing
    pa_audio_callback(in_buf, nullptr, 64, nullptr, 0, &engine);
    
    engine.shutdown();
}
