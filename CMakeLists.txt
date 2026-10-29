cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel). Must be set before project().
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

project(StudioFrosMegaphone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MEGAPHONE_BUILD_TESTS "Build the DSP unit tests" ON)
option(MEGAPHONE_BUILD_PLUGIN "Build the VST3 / Standalone plugin (needs JUCE)" ON)

# Copy the VST3 to the user's plugin folder after building (~/.vst3 on Linux,
# ~/Library/Audio/Plug-Ins/VST3 on macOS) so the DAW finds it right away.
# Off by default on Windows (system folder, needs admin) and on CI servers.
if(WIN32 OR DEFINED ENV{CI})
    set(_megaphone_install_default OFF)
else()
    set(_megaphone_install_default ON)
endif()
option(MEGAPHONE_INSTALL_AFTER_BUILD "Copy the VST3 to the user's plugin folder after building" ${_megaphone_install_default})

# ---------------------------------------------------------------------------
# DSP core: plain C++, no JUCE. Used by the plugin and by the tests.
# ---------------------------------------------------------------------------
add_library(megaphone_dsp STATIC Source/MegaphoneDSP.cpp)
target_include_directories(megaphone_dsp PUBLIC Source)
if(NOT MSVC)
    target_compile_options(megaphone_dsp PRIVATE -Wall -Wextra)
endif()

if(MEGAPHONE_BUILD_TESTS)
    enable_testing()
    add_executable(megaphone_dsp_tests Tests/dsp_test.cpp)
    target_link_libraries(megaphone_dsp_tests PRIVATE megaphone_dsp)
    add_test(NAME dsp COMMAND megaphone_dsp_tests)
endif()

# ---------------------------------------------------------------------------
# Plugin (JUCE 8, downloaded automatically the first time)
# ---------------------------------------------------------------------------
if(MEGAPHONE_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    # Artwork (panel + sprites) compiled into the plugin. Regenerate it from the
    # designer's picture with:  python3 tools/make_assets.py
    juce_add_binary_data(StudioAssets
        HEADER_NAME StudioAssets.h
        NAMESPACE   StudioAssets
        SOURCES
            Assets/panel.jpg
            Assets/comp_on.png
            Assets/comp_off.png
            Assets/mode_1.png
            Assets/mode_2.png
            Assets/noise_click.png
            Assets/noise_both.png
            Assets/noise_burst.png
            Assets/bypass_active.png
            Assets/bypass_off.png
            Assets/led_on.png
            Assets/led_off.png
    )

    juce_add_plugin(StudioFrosMegaphone
        COMPANY_NAME                "GuitarFros"
        COMPANY_WEBSITE             "https://github.com/GuitarFros"
        PRODUCT_NAME                "StudioFros - Megaphone"
        BUNDLE_ID                   com.guitarfros.studiofros-megaphone
        VERSION                     ${PROJECT_VERSION}
        PLUGIN_MANUFACTURER_CODE    Gtfr
        PLUGIN_CODE                 Sfmp
        FORMATS                     VST3 Standalone
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES             Fx Distortion
        COPY_PLUGIN_AFTER_BUILD     ${MEGAPHONE_INSTALL_AFTER_BUILD}
    )

    # The DSP is compiled straight into the plugin (not linked as a separate
    # static library) so it always shares the plugin's compiler settings.
    target_sources(StudioFrosMegaphone PRIVATE
        Source/MegaphoneDSP.cpp
        Source/MegaphoneDSP.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PresetManager.cpp
        Source/PresetManager.h
        Source/ParameterSpecs.h
        Source/FactoryPresets.h
        Source/PanelLayout.h
    )

    target_include_directories(StudioFrosMegaphone PRIVATE Source)

    target_compile_definitions(StudioFrosMegaphone PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
    )

    target_link_libraries(StudioFrosMegaphone
        PRIVATE
            juce::juce_audio_utils
            StudioAssets
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()
