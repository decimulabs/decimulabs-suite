cmake_minimum_required(VERSION 3.22)
project(DecimulFM VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared ${CMAKE_CURRENT_BINARY_DIR}/decimu-shared)

juce_add_plugin(DecimulFM
    COMPANY_NAME "DecimuLabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dfm1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs FM"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# El motor del chip (C puro) es el mismo que usa la página web: sims/fm/ym2612.c.
target_sources(DecimulFM PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Retro.cpp
    Source/engine/ym2612.c
)

target_compile_options(DecimulFM PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulFM PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulFM PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test del motor dentro del plugin (opcional): cmake -DDECIMU_FM_TESTS=ON
option(DECIMU_FM_TESTS "Build the FM engine integration test" OFF)
if(DECIMU_FM_TESTS)
    juce_add_console_app(FMEngineTest PRODUCT_NAME "FMEngineTest")
    target_sources(FMEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(FMEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(FMEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(FMEngineTest PRIVATE DecimulFM juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

# Captura de la UI sin DAW (opcional): cmake -DDECIMU_FM_UISHOT=ON
option(DECIMU_FM_UISHOT "Build the FM UI screenshot tool" OFF)
if(DECIMU_FM_UISHOT)
    juce_add_console_app(FMUiShot PRODUCT_NAME "FMUiShot")
    target_sources(FMUiShot PRIVATE tests/UiShot.cpp)
    target_compile_options(FMUiShot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(FMUiShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(FMUiShot PRIVATE DecimulFM decimu_shared_ui juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
