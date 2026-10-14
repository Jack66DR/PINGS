cmake_minimum_required(VERSION 3.22)

project(PINGS VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

option(PNG_BUILD_TESTS "Build the offline DSP test, demo render and UI snapshot" OFF)

# Mutable Instruments' Rings DSP, by Emilie Gillet, MIT licence (ThirdParty/mutable/LICENSE).
# Built as-is; TEST selects stmlib's portable code instead of the module's ARM instructions.
set(MI ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/mutable)
add_library(rings_dsp STATIC
    ${MI}/rings/dsp/part.cc
    ${MI}/rings/dsp/string_synth_part.cc
    ${MI}/rings/dsp/string.cc
    ${MI}/rings/dsp/resonator.cc
    ${MI}/rings/dsp/fm_voice.cc
    ${MI}/rings/resources.cc
    ${MI}/stmlib/dsp/units.cc
    ${MI}/stmlib/utils/random.cc)
target_include_directories(rings_dsp PUBLIC ${MI})
target_compile_definitions(rings_dsp PUBLIC TEST)
set_target_properties(rings_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(rings_dsp PRIVATE -w -O2)
endif()

set(PNG_FORMATS AU VST3 Standalone)
if(NOT APPLE)
    set(PNG_FORMATS VST3 Standalone)
endif()

juce_add_plugin(PINGS
    COMPANY_NAME                "manners! DSP"
    BUNDLE_ID                   com.mannersdsp.pings
    PLUGIN_MANUFACTURER_CODE    Mnrs
    PLUGIN_CODE                 Pngs
    PRODUCT_NAME                "PINGS"
    DESCRIPTION                 "Resonator after Rings: strike it from the keys, or feed it"
    FORMATS                     ${PNG_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    VST3_CATEGORIES             Fx Synth)

juce_add_binary_data(PINGS_Assets SOURCES Resources/ShareTechMono-Regular.ttf)

target_sources(PINGS PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PINGS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PINGS
    PRIVATE
        PINGS_Assets
        rings_dsp
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PNG_BUILD_TESTS)
    # Engine-only checks: no GUI, runs on the CI machine.
    add_executable(PINGS_Test Tests/OfflineTest.cpp)
    target_include_directories(PINGS_Test PRIVATE Source)
    target_link_libraries(PINGS_Test PRIVATE rings_dsp)
    if(NOT MSVC)
        target_compile_options(PINGS_Test PRIVATE -O2)
    endif()

    # Full plugin: renders a demo WAV and a PNG of the editor.
    juce_add_console_app(PINGS_Tool PRODUCT_NAME "PINGS_Tool")
    target_sources(PINGS_Tool PRIVATE
        Tests/Tool.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(PINGS_Tool PRIVATE Source)
    target_compile_definitions(PINGS_Tool PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PINGS_Tool PRIVATE
        PINGS_Assets
        rings_dsp
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
