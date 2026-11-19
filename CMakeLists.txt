cmake_minimum_required(VERSION 3.22)
project(ParticulaDub VERSION 0.1.0)

# JUCE: si existe la carpeta JUCE/ (la descarga el workflow) se usa esa; si no, se descarga sola.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG master
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ParticulaDub
    COMPANY_NAME "Particula Primordial"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Ppri
    PLUGIN_CODE Pdub
    FORMATS VST3
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Particula Dub")

target_sources(ParticulaDub PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp)

target_compile_definitions(ParticulaDub PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ParticulaDub
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lib_flags
        juce::juce_recommended_warning_flags)
