cmake_minimum_required(VERSION 3.22)
project(DecimulCincel VERSION 0.0.1)

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

# Nombre definitivo "Cincel" (elegido por Marcelo).
juce_add_plugin(DecimulCincel
    COMPANY_NAME "DecimuLabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dpr1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Cincel EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulCincel PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EqDisplay.cpp
    Source/BandPanel.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulCincel PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulCincel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulCincel PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test de integración del plugin (opcional): cmake -DDECIMU_CINCEL_TESTS=ON
# El motor de audio tiene además su test rápido sin JUCE: tests/build_dsp.bat.
option(DECIMU_CINCEL_TESTS "Build the Cincel plugin integration test" OFF)
if(DECIMU_CINCEL_TESTS)
    juce_add_console_app(CincelEngineTest PRODUCT_NAME "CincelEngineTest")
    target_sources(CincelEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(CincelEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(CincelEngineTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(CincelEngineTest PRIVATE
        DecimulCincel
        decimu_shared_ui
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
