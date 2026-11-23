cmake_minimum_required(VERSION 3.22)
project(MaidFrog VERSION 0.3.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

option(AM_BUILD_CLAP "Also build a CLAP plugin (clap-juce-extensions)" ON)
option(AM_BUILD_STANDALONE "Build the standalone app" ON)
set(AM_FORMATS VST3)
if(AM_BUILD_STANDALONE)
    list(APPEND AM_FORMATS Standalone)
endif()
if(APPLE)
    list(APPEND AM_FORMATS AU)
endif()

juce_add_plugin(REF
    COMPANY_NAME                "Lismok"
    PLUGIN_MANUFACTURER_CODE    Lsmk
    PLUGIN_CODE                 Mfrg
    BUNDLE_ID                   com.lismok.maidfrog
    PRODUCT_NAME                "Maid Frog"
    FORMATS                     ${AM_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Restoration
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(REF PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(REF PRIVATE Source)

if(MSVC)
    target_compile_options(REF PRIVATE /bigobj /utf-8 /MP)
endif()

target_compile_definitions(REF PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Optional assets: Assets/Wordmark.ttf and Assets/RainFrog.png (a placeholder frog: replace it with the Maid Frog drawing, same file name)
set(AM_FONT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Wordmark.ttf")
set(AM_FROG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Assets/RainFrog.png")
set(AM_ASSETS "")
if(EXISTS "${AM_FONT_FILE}")
    list(APPEND AM_ASSETS "${AM_FONT_FILE}")
    target_compile_definitions(REF PUBLIC AHR1_HAS_FONT=1)
else()
    target_compile_definitions(REF PUBLIC AHR1_HAS_FONT=0)
endif()
if(EXISTS "${AM_FROG_FILE}")
    list(APPEND AM_ASSETS "${AM_FROG_FILE}")
    target_compile_definitions(REF PUBLIC AHR1_HAS_FROG=1)
else()
    target_compile_definitions(REF PUBLIC AHR1_HAS_FROG=0)
endif()
if(AM_ASSETS)
    juce_add_binary_data(REFAssets SOURCES ${AM_ASSETS})
    target_link_libraries(REF PRIVATE REFAssets)
endif()

if(AM_BUILD_CLAP)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        main
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(clap-juce-extensions)
    clap_juce_extensions_plugin(TARGET REF
        CLAP_ID "com.lismok.maidfrog"
        CLAP_FEATURES audio-effect restoration stereo mono)
endif()

target_link_libraries(REF
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(AM_BUILD_TESTS "Build the offline DSP tests" ON)
if(AM_BUILD_TESTS)
    add_executable(gf_tests Tests/gf_tests.cpp)
    target_include_directories(gf_tests PRIVATE Source)
endif()
