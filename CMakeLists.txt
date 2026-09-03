cmake_minimum_required(VERSION 3.22)

project(EMAXArp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/EHsc)
endif()

include(FetchContent)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)

FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13   
)
FetchContent_MakeAvailable(juce)


juce_add_plugin(EMAXArp
    PLUGIN_MANUFACTURER_CODE Vuil
    PLUGIN_CODE Emax
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "EMAXArp"
    VST3_CATEGORIES "Fx|MIDI"
    COMPANY_NAME "Vuil"
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_SYNTH FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(EMAXArp PRIVATE
    src/ArpEngine.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)
target_include_directories(EMAXArp PRIVATE
    src
)
target_compile_definitions(EMAXArp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(EMAXArp
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Enable useful warnings
target_compile_definitions(EMAXArp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST2_ENABLED=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)



# Disable VST2 build globally
set(JUCE_BUILD_VST2_CLIENT FALSE CACHE BOOL "" FORCE)
