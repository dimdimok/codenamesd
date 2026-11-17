cmake_minimum_required(VERSION 3.22)
project(SyncDelay VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SyncDelay
    COMPANY_NAME "Home"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Hmst
    PLUGIN_CODE Sdl1
    FORMATS VST3
    PRODUCT_NAME "Sync Delay"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SyncDelay PRIVATE
    Source/PluginProcessor.cpp)

target_compile_definitions(SyncDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SyncDelay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
