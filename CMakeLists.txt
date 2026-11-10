cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary on macOS")
project(DeltaAB VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: local checkout if JUCE_DIR is given, otherwise downloaded.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(DeltaAB
    COMPANY_NAME "Imagomusica"
    PLUGIN_MANUFACTURER_CODE Imgm
    PLUGIN_CODE Dlab
    FORMATS VST3 Standalone
    PRODUCT_NAME "Delta AB"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DeltaAB PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(DeltaAB PRIVATE Source)

target_compile_definitions(DeltaAB PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(DeltaAB PRIVATE /utf-8)
endif()

target_link_libraries(DeltaAB
    PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# DSP measurement tests (no JUCE)
add_executable(dsp_tests tests/dsp_tests.cpp)
enable_testing()
add_test(NAME dsp_tests COMMAND dsp_tests)

# Host test: loads the built VST3 like a DAW (optional)
option(DELTAAB_HOST_TEST "Build the VST3 host test" OFF)
if(DELTAAB_HOST_TEST)
    juce_add_console_app(host_test PRODUCT_NAME "host_test")
    target_sources(host_test PRIVATE tests/host_test.cpp)
    target_compile_definitions(host_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(host_test PRIVATE juce::juce_audio_processors juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()
