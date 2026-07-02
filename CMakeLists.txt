cmake_minimum_required(VERSION 3.22)

project(CHAOS_REALM VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHAOS_BUILD_PLUGIN "Build the JUCE VST3/AU/Standalone plugin (requires network + audio SDK deps)" ON)
option(CHAOS_BUILD_TESTS  "Build the framework-free DSP test suite" ON)

# ---------------------------------------------------------------------------
#  DSP unit tests — pure C++17, NO JUCE.  Always cheap to build, run in CI.
# ---------------------------------------------------------------------------
if(CHAOS_BUILD_TESTS)
    enable_testing()

    file(GLOB CHAOS_MODULE_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/modules/*.cpp")

    add_executable(chaos_core_tests
        tests/dsp_core_tests.cpp)
    target_compile_features(chaos_core_tests PRIVATE cxx_std_17)

    add_executable(chaos_stability_tests
        tests/module_stability_tests.cpp
        Source/dsp/ChaosEngine.cpp
        ${CHAOS_MODULE_SOURCES})
    target_compile_features(chaos_stability_tests PRIVATE cxx_std_17)
    target_include_directories(chaos_stability_tests PRIVATE Source)

    foreach(t chaos_core_tests chaos_stability_tests)
        if(NOT MSVC)
            target_compile_options(${t} PRIVATE -O2 -Wall -Wextra)
        endif()
    endforeach()

    add_test(NAME core_dsp      COMMAND chaos_core_tests)
    add_test(NAME module_safety COMMAND chaos_stability_tests)
endif()

# ---------------------------------------------------------------------------
#  The plugin.  JUCE is fetched automatically.
# ---------------------------------------------------------------------------
if(CHAOS_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(CHAOSRealm
        PRODUCT_NAME              "CHAOS REALM"
        COMPANY_NAME              "Chaos Realm Audio"
        BUNDLE_ID                 "com.chaosrealm.ultimateaudioprocessor"
        PLUGIN_MANUFACTURER_CODE  Chao
        PLUGIN_CODE               Chrl
        FORMATS                   VST3 AU Standalone
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        VST3_CATEGORIES           Fx Distortion Modulation
        AU_MAIN_TYPE              kAudioUnitType_Effect)

    file(GLOB PLUGIN_MODULE_SOURCES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/modules/*.cpp")

    # Embed the factory preset bank so the plugin ships with 500+ presets.
    juce_add_binary_data(CHAOSRealmData SOURCES presets/FactoryPresets.xml)

    target_sources(CHAOSRealm PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PresetManager.cpp
        Source/dsp/ChaosEngine.cpp
        ${PLUGIN_MODULE_SOURCES})

    target_compile_definitions(CHAOSRealm PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries(CHAOSRealm PRIVATE
        CHAOSRealmData
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()
