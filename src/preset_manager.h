#pragma once

#include "common.h"
#include "audio/audio_engine.h"
#include <fstream>
#include <sstream>

namespace GuitarAmp {

struct PresetData {
    std::string name;
    std::string description;
    float input_gain = 0.7f;
    float output_gain = 0.8f;

    struct EffectData {
        std::string type;
        bool enabled = false;
        float mix = 1.0f;
        std::vector<std::pair<std::string, float>> params;
    };
    std::vector<EffectData> effects;
};

class PresetManager {
public:
    // Save provided preset data to JSON file
    static bool save_preset_data(const std::string& filepath,
                                 const PresetData& preset);

    // Save current engine state to JSON file
    static bool save_preset(const std::string& filepath,
                            const std::string& preset_name,
                            const std::string& description,
                            AudioEngine& engine);

    // Load preset from JSON file and apply to engine
    static bool load_preset(const std::string& filepath,
                            AudioEngine& engine);

    // Get the default presets directory (creates if needed)
    static std::string get_presets_dir();

    // List available preset files in the presets directory
    static std::vector<std::string> list_presets();

    // Get last error message
    static const std::string& last_error() { return last_error_; }

private:
    static std::string last_error_;

    // Minimal JSON helpers (no external dependency)
    static std::string to_json(const PresetData& preset);
    static bool from_json(const std::string& json, PresetData& preset);

    // Escape/unescape strings for JSON
    static std::string escape_json_string(const std::string& s);
    static std::string unescape_json_string(const std::string& s);
};

} // namespace GuitarAmp
