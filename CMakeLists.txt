cmake_minimum_required(VERSION 3.16)
project(GuitarAmp VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /fp:fast)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
else()
    add_compile_options(-Wall -Wextra -ffast-math)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    endif()
endif()

# --- Dependencies ---

# PortAudio
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PORTAUDIO portaudio-2.0)
endif()
if(NOT PORTAUDIO_FOUND)
    find_path(PORTAUDIO_INCLUDE_DIRS portaudio.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
              "C:/Program Files/portaudio/include"
              "${CMAKE_SOURCE_DIR}/external/portaudio/include")
    find_library(PORTAUDIO_LIBRARIES NAMES portaudio portaudio_x64
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
              "C:/Program Files/portaudio/lib"
              "${CMAKE_SOURCE_DIR}/external/portaudio/lib")
endif()

# SDL2
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
endif()
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
        set(SDL2_LIBRARIES SDL2::SDL2 SDL2::SDL2main)
    else()
        find_path(SDL2_INCLUDE_DIRS SDL.h
            PATH_SUFFIXES SDL2
            PATHS /usr/include /usr/local/include /opt/homebrew/include
                  "C:/Program Files/SDL2/include"
                  "${CMAKE_SOURCE_DIR}/external/SDL2/include")
        find_library(SDL2_LIBRARIES NAMES SDL2
            PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
                  "C:/Program Files/SDL2/lib"
                  "${CMAKE_SOURCE_DIR}/external/SDL2/lib")
    endif()
endif()

# OpenGL
find_package(OpenGL REQUIRED)

# --- Dear ImGui (bundled) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Suppress warnings on vendored imgui sources
if(NOT MSVC)
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
endif()

# --- Application Sources ---
set(APP_SOURCES
    src/main.cpp
    src/preset_manager.cpp
    src/audio/audio_engine.cpp
    src/audio/recorder.cpp
    src/audio/effects/distortion.cpp
    src/audio/effects/overdrive.cpp
    src/audio/effects/delay.cpp
    src/audio/effects/reverb.cpp
    src/audio/effects/chorus.cpp
    src/audio/effects/equalizer.cpp
    src/audio/effects/noise_gate.cpp
    src/audio/effects/compressor.cpp
    src/audio/effects/cabinet_sim.cpp
    src/gui/gui_manager.cpp
    src/gui/pedal_board.cpp
    src/gui/pedal_widget.cpp
    src/gui/file_dialog.cpp
)

# Windows resource file for app icon
if(WIN32 AND NOT MSVC)
    set(WIN_RC ${CMAKE_SOURCE_DIR}/assets/app.rc)
    set(CMAKE_RC_COMPILER_INIT windres)
    enable_language(RC)
endif()

add_executable(guitar-amp ${APP_SOURCES} ${IMGUI_SOURCES} $<$<PLATFORM_ID:Windows>:${WIN_RC}>)

target_include_directories(guitar-amp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PORTAUDIO_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)

target_link_libraries(guitar-amp PRIVATE
    ${PORTAUDIO_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
)

if(WIN32)
    target_link_libraries(guitar-amp PRIVATE winmm ole32 setupapi comdlg32 shell32)
    if(NOT MSVC)
        target_link_options(guitar-amp PRIVATE -mwindows)
    else()
        set_target_properties(guitar-amp PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(guitar-amp PRIVATE dl pthread)
endif()

# --- Test Target ---
set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_common.cpp
    tests/test_effects.cpp
    tests/test_preset_manager.cpp
    tests/test_recorder.cpp
    tests/test_theme.cpp
)

# Effect + core sources needed by tests (no GUI, no main)
set(CORE_SOURCES
    src/preset_manager.cpp
    src/audio/audio_engine.cpp
    src/audio/recorder.cpp
    src/audio/effects/distortion.cpp
    src/audio/effects/overdrive.cpp
    src/audio/effects/delay.cpp
    src/audio/effects/reverb.cpp
    src/audio/effects/chorus.cpp
    src/audio/effects/equalizer.cpp
    src/audio/effects/noise_gate.cpp
    src/audio/effects/compressor.cpp
    src/audio/effects/cabinet_sim.cpp
)

add_executable(amplitron-tests ${TEST_SOURCES} ${CORE_SOURCES})

target_include_directories(amplitron-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PORTAUDIO_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)

target_link_libraries(amplitron-tests PRIVATE
    ${PORTAUDIO_LIBRARIES}
)

if(WIN32)
    target_link_libraries(amplitron-tests PRIVATE winmm ole32 setupapi)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(amplitron-tests PRIVATE dl pthread)
endif()

# Install
install(TARGETS guitar-amp RUNTIME DESTINATION bin)
