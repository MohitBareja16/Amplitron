#include "test_framework.h"
#include "audio/audio_engine.h"
#include "audio/effects/distortion.h"
#include "audio/effects/overdrive.h"
#include <vector>
#include <memory>

using namespace Amplitron;

// ---------------------------------------------------------
// audio_engine_process.cpp & audio_engine_api.cpp Tests
// ---------------------------------------------------------

TEST(AudioEngineProcess_ProcessSilenceGivesZeroOutput) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(64);
    std::vector<float> in(64, 0.0f), out(128, 0.0f);
    engine.process_audio(in.data(), out.data(), 64);
    for (auto s : out) ASSERT_NEAR(s, 0.0f, 1e-6f);
}

TEST(AudioEngineProcess_InputGainScalesOutput) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(64);
    engine.set_input_gain(0.5f);
    engine.set_output_gain(1.0f);
    std::vector<float> in(64, 1.0f), out(128, 0.0f);
    engine.process_audio(in.data(), out.data(), 64);
    ASSERT_NEAR(out[0], 0.5f, 0.01f);
}

TEST(AudioEngineProcess_OutputGainScalesOutput) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(64);
    // Input gain defaults to 1.0f. Output defaults to 0.8f? Wait, let's explicitly set it.
    engine.set_input_gain(1.0f);
    engine.set_output_gain(0.25f);
    std::vector<float> in(64, 1.0f), out(128, 0.0f);
    engine.process_audio(in.data(), out.data(), 64);
    ASSERT_NEAR(out[0], 0.25f, 0.01f);
    ASSERT_NEAR(out[1], 0.25f, 0.01f);
}

TEST(AudioEngineProcess_OutputIsClampedToSafetyLimit) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(64);
    engine.set_input_gain(10.0f); // Massive gain to exceed +/- 1.0
    engine.set_output_gain(1.0f);
    std::vector<float> in(64, 1.0f), out(128, 0.0f);
    engine.process_audio(in.data(), out.data(), 64);
    // Should be clamped to 1.0
    ASSERT_NEAR(out[0], 1.0f, 1e-6f);
    
    std::vector<float> in_neg(64, -1.0f);
    engine.process_audio(in_neg.data(), out.data(), 64);
    // Should be clamped to -1.0
    ASSERT_NEAR(out[0], -1.0f, 1e-6f);
}

TEST(AudioEngineProcess_RMSCalculationSilenceVsTone) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(64);
    engine.set_analyzer_enabled(true);
    
    // Silence
    std::vector<float> in_silence(64, 0.0f), out(128, 0.0f);
    engine.process_audio(in_silence.data(), out.data(), 64);
    ASSERT_NEAR(engine.get_input_rms(), 0.0f, 1e-6f);
    ASSERT_NEAR(engine.get_output_rms(), 0.0f, 1e-6f);

    // DC Tone (1.0)
    std::vector<float> in_tone(64, 1.0f);
    engine.process_audio(in_tone.data(), out.data(), 64);
    
    // RMS is smoothed, so it won't be exactly 1.0 immediately, but should be > 0
    ASSERT_TRUE(engine.get_input_rms() > 0.0f);
    ASSERT_TRUE(engine.get_output_rms() > 0.0f);
}

// ---------------------------------------------------------
// audio_engine_chain.cpp Tests
// ---------------------------------------------------------

TEST(AudioEngineChain_AddAndRemoveEffect) {
    AudioEngine engine;
    ASSERT_EQ(engine.effects().size(), 0u);
    
    auto dist = std::make_shared<Distortion>();
    engine.add_effect(dist);
    ASSERT_EQ(engine.effects().size(), 1u);

    engine.remove_effect(0);
    ASSERT_EQ(engine.effects().size(), 0u);
}

TEST(AudioEngineChain_InsertEffect) {
    AudioEngine engine;
    auto dist = std::make_shared<Distortion>();
    auto od = std::make_shared<Overdrive>();
    
    engine.add_effect(dist);
    engine.insert_effect(0, od); // Insert Overdrive at the beginning
    
    ASSERT_EQ(engine.effects().size(), 2u);
    ASSERT_EQ(engine.effects()[0], od);
    ASSERT_EQ(engine.effects()[1], dist);
}

TEST(AudioEngineChain_ClearEffects) {
    AudioEngine engine;
    engine.add_effect(std::make_shared<Distortion>());
    engine.add_effect(std::make_shared<Overdrive>());
    ASSERT_EQ(engine.effects().size(), 2u);
    
    engine.clear_effects();
    ASSERT_EQ(engine.effects().size(), 0u);
}

TEST(AudioEngineChain_MoveEffect) {
    AudioEngine engine;
    auto dist = std::make_shared<Distortion>();
    auto od = std::make_shared<Overdrive>();
    
    engine.add_effect(dist);
    engine.add_effect(od);
    
    ASSERT_EQ(engine.effects()[0], dist);
    ASSERT_EQ(engine.effects()[1], od);
    
    engine.move_effect(0, 1);
    
    ASSERT_EQ(engine.effects()[0], od);
    ASSERT_EQ(engine.effects()[1], dist);
}

TEST(AudioEngineApi_MetronomeState) {
    AudioEngine engine;
    ASSERT_EQ(engine.get_metronome_enabled(), false);
    engine.toggle_metronome();
    ASSERT_EQ(engine.get_metronome_enabled(), true);
    
    engine.set_metronome_bpm(150);
    ASSERT_EQ(engine.get_metronome_bpm(), 150);
    
    // Bounds check
    engine.set_metronome_bpm(10); // min 40
    ASSERT_EQ(engine.get_metronome_bpm(), 40);
    
    engine.set_metronome_volume(0.8f);
    ASSERT_NEAR(engine.get_metronome_volume(), 0.8f, 1e-6f);
}

TEST(AudioEngineApi_SuggestedBufferSize) {
    AudioEngine engine;
    engine.set_buffer_size(512); // load is 0, so it should suggest half
    int suggested = engine.get_suggested_buffer_size();
    ASSERT_EQ(suggested, 256);
}

TEST(AudioEngineApi_CopyAnalyzerSnapshot) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(1024);
    engine.set_analyzer_enabled(true);
    std::vector<float> in(1024, 0.5f), out(2048, 0.0f);
    // Process twice to fill the 2048-sample analyzer ring buffer completely
    engine.process_audio(in.data(), out.data(), 1024);
    engine.process_audio(in.data(), out.data(), 1024);
    
    std::vector<float> snap_in(2048, 0.0f), snap_out(2048, 0.0f);
    bool success = engine.copy_analyzer_snapshot(snap_in.data(), snap_out.data(), 1024);
    ASSERT_EQ(success, true);
    ASSERT_NEAR(snap_in[0], 0.5f, 0.01f);
}

TEST(AudioEngineChain_TunerTap) {
    AudioEngine engine;
    ASSERT_EQ(engine.has_tuner_tap(), false);
    
    auto tap = std::make_shared<Distortion>();
    engine.set_tuner_tap(tap);
    ASSERT_EQ(engine.has_tuner_tap(), true);
    
    engine.clear_tuner_tap();
    ASSERT_EQ(engine.has_tuner_tap(), false);
}

TEST(AudioEngineChain_RestoreEffectsState) {
    AudioEngine engine;
    auto dist = std::make_shared<Distortion>();
    auto od = std::make_shared<Overdrive>();
    
    std::vector<std::shared_ptr<Effect>> state = {dist, od};
    engine.restore_effects_state(state);
    
    ASSERT_EQ(engine.effects().size(), 2u);
    ASSERT_EQ(engine.effects()[0], dist);
}

TEST(AudioEngineApi_CommandQueuePushes) {
    AudioEngine engine;
    engine.initialize();
    engine.set_buffer_size(64);
    
    auto dist = std::make_shared<Distortion>();
    engine.add_effect(dist);
    
    // Interleave gain commands and effect commands to trigger both drain loops
    engine.set_input_gain(1.0f); 
    engine.push_effect_enabled(0, 0.0f); // disable
    engine.push_effect_mix(0, 0.25f);
    engine.set_input_gain(0.5f); // this gets stuck behind effect commands and handled by drain_commands
    engine.push_param_change(0, 0, 0.8f); // Param 0 is probably Gain or Drive
    
    // Process audio to drain commands
    std::vector<float> in(64, 0.0f), out(128, 0.0f);
    engine.process_audio(in.data(), out.data(), 64);
    
    // Check if dist state changed
    ASSERT_EQ(dist->is_enabled(), false);
    ASSERT_NEAR(dist->get_mix(), 0.25f, 1e-6f);
    ASSERT_NEAR(dist->params()[0].value, 0.8f, 1e-6f);
    ASSERT_NEAR(engine.get_input_gain(), 0.5f, 1e-6f);
}
