cmake_minimum_required(VERSION 3.22)
project(DecimulEmery VERSION 0.0.1)

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

# "Look" compartido de toda la suite — ver shared/CMakeLists.txt y el README de la raíz.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared ${CMAKE_CURRENT_BINARY_DIR}/decimu-shared)

# Nombre de trabajo "Emery": se cambia acá (PRODUCT_NAME) y en los textos de la UI.
juce_add_plugin(DecimulEmery
    COMPANY_NAME "decimulabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dem1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Emery"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulEmery PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BandView.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulEmery PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulEmery PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulEmery PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test de integración del motor (opcional): cmake -DDECIMU_EMERY_TESTS=ON
option(DECIMU_EMERY_TESTS "Build the Emery engine integration test" OFF)
if(DECIMU_EMERY_TESTS)
    juce_add_console_app(EmeryEngineTest PRODUCT_NAME "EmeryEngineTest")
    target_sources(EmeryEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(EmeryEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(EmeryEngineTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EmeryEngineTest PRIVATE
        DecimulEmery
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
