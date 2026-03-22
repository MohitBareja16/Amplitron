#pragma once

#include <string>

namespace GuitarAmp {

// Cross-platform native save file dialog
// Returns empty string if user cancelled
std::string show_save_dialog(const std::string& default_name = "recording.wav",
                             const std::string& filter_desc = "WAV Audio",
                             const std::string& filter_ext = "wav");

} // namespace GuitarAmp
