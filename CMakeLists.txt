cmake_minimum_required (VERSION 3.22)

project (PillowSaturator VERSION 0.2.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C/C++ runtime statically so the plug-in does not depend on the
# Visual C++ Redistributable being installed on the user's machine.
set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option (PILLOW_BUILD_PLUGIN "Build the JUCE plug-in (VST3 / AU / Standalone)" ON)
option (PILLOW_BUILD_TESTS  "Build the DSP engine tests (no JUCE needed)" ON)
set (PILLOW_JUCE_PATH "" CACHE PATH "Existing JUCE checkout to use (empty = download PILLOW_JUCE_TAG)")
set (PILLOW_JUCE_TAG "8.0.17-security" CACHE STRING "JUCE git tag to download when PILLOW_JUCE_PATH is empty")
set (PILLOW_FORMATS VST3 AU Standalone CACHE STRING "Plug-in formats to build (AU is skipped automatically off macOS)")

#===============================================================================
if (PILLOW_BUILD_PLUGIN)
    if (PILLOW_JUCE_PATH)
        add_subdirectory ("${PILLOW_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include (FetchContent)
        FetchContent_Declare (JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${PILLOW_JUCE_TAG}
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable (JUCE)
    endif()

    juce_add_plugin (PillowSaturator
        COMPANY_NAME                "Pillow Audio"
        BUNDLE_ID                   "com.pillowaudio.saturator"
        PLUGIN_MANUFACTURER_CODE    Pilw        # keep identical across all Pillow plug-ins
        PLUGIN_CODE                 PSat        # unique per plug-in, never change after release
        FORMATS                     ${PILLOW_FORMATS}
        PRODUCT_NAME                "Pillow Saturator"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Fx Distortion
        AU_MAIN_TYPE                kAudioUnitType_Effect)

    target_sources (PillowSaturator
        PRIVATE
            Source/Parameters.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp)

    target_include_directories (PillowSaturator PRIVATE Source)

    target_compile_definitions (PillowSaturator
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries (PillowSaturator
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

#===============================================================================
if (PILLOW_BUILD_TESTS)
    enable_testing()
    add_executable (PillowEngineTests tests/EngineTests.cpp)
    target_include_directories (PillowEngineTests PRIVATE Source)
    add_test (NAME PillowEngineTests COMMAND PillowEngineTests --quick)
endif()
