cmake_minimum_required(VERSION 3.22)
project(Rezalica VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional: point to a local JUCE checkout with -DREZALICA_JUCE_PATH=...,
# otherwise JUCE is downloaded automatically.
set(REZALICA_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(REZALICA_JUCE_PATH)
    add_subdirectory(${REZALICA_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Rezalica
    COMPANY_NAME "Rezalica"
    PLUGIN_MANUFACTURER_CODE Rzlc
    PLUGIN_CODE Rz16
    FORMATS VST3 Standalone
    PRODUCT_NAME "Rezalica"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler)

target_sources(Rezalica PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SamplerEngine.cpp
    Source/WaveformView.cpp
    Source/SequencerWindow.cpp
    Source/StretchWindow.cpp
    Source/PianoRollWindow.cpp
    Source/SceneWindow.cpp)

target_compile_definitions(Rezalica PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

# ASIO for the standalone app (Windows): point REZALICA_ASIO_SDK at the Steinberg ASIO SDK folder
# (the one that contains common/iasiodrv.h). Without it the app still has Windows Audio / DirectSound.
set(REZALICA_ASIO_SDK "" CACHE PATH "Steinberg ASIO SDK folder (optional, Windows)")
if(WIN32 AND REZALICA_ASIO_SDK AND EXISTS "${REZALICA_ASIO_SDK}/common/iasiodrv.h")
    message(STATUS "Rezalica: ASIO support enabled (${REZALICA_ASIO_SDK})")
    target_compile_definitions(Rezalica PUBLIC JUCE_ASIO=1)
    target_include_directories(Rezalica PUBLIC "${REZALICA_ASIO_SDK}/common")
else()
    message(STATUS "Rezalica: ASIO support not enabled (no ASIO SDK given)")
endif()

target_link_libraries(Rezalica
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(REZALICA_BUILD_TESTS "Build the headless engine tests" OFF)
if(REZALICA_BUILD_TESTS)
    add_executable(RezalicaTests Tests/EngineTests.cpp)
    # reuse the plugin's compiled code (including JUCE) instead of compiling JUCE twice
    target_include_directories(RezalicaTests PRIVATE $<TARGET_PROPERTY:Rezalica,INCLUDE_DIRECTORIES>)
    target_compile_definitions(RezalicaTests PRIVATE $<TARGET_PROPERTY:Rezalica,COMPILE_DEFINITIONS>)
    target_link_libraries(RezalicaTests PRIVATE Rezalica juce::juce_recommended_config_flags)
endif()
