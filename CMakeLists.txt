cmake_minimum_required(VERSION 3.22)
project(Rezalica VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# static MSVC runtime: Rezalica.exe runs on a fresh Windows without the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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
    ICON_BIG Resources/RezalicaAppIcon.png       # program icon (exe, desktop shortcut, taskbar)
    ICON_SMALL Resources/RezalicaAppIcon.png
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
    Source/SceneWindow.cpp
    Source/ToolWindows.cpp
    Source/LoopbackCapture.cpp
    Source/PluginHost.cpp
    Source/MixerPanel.cpp
    Source/RezalicaApp.cpp
    Source/Mp3Writer.cpp
    Source/ExportDialog.cpp
    Source/DeckPanel.cpp)

target_compile_definitions(Rezalica PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1    # third-party VST3 effects in the mixer
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)  # Source/RezalicaApp.cpp (adds the out-of-process plug-in scan)

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

# fonts: Press Start 2P (logo) and Great Vibes (DECK), both SIL Open Font License - see Resources/*-OFL.txt
juce_add_binary_data(RezalicaData SOURCES Resources/PressStart2P-Regular.ttf Resources/GreatVibes-Regular.ttf Resources/RezalicaIcon.png)

target_link_libraries(Rezalica
    PRIVATE
        RezalicaData
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
