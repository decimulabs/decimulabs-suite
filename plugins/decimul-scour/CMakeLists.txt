cmake_minimum_required(VERSION 3.22)
project(DecimulScour VERSION 0.0.1)

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

# Nombre de trabajo "Scour": se cambia acá (PRODUCT_NAME) y en los textos de la UI.
juce_add_plugin(DecimulScour
    COMPANY_NAME "decimulabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dsc1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Scour"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulScour PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ScopeView.cpp
    Source/ModuleViz.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulScour PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulScour PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulScour PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test de integración del motor (opcional): cmake -DDECIMU_SCOUR_TESTS=ON
option(DECIMU_SCOUR_TESTS "Build the Scour engine integration test" OFF)
if(DECIMU_SCOUR_TESTS)
    juce_add_console_app(ScourEngineTest PRODUCT_NAME "ScourEngineTest")
    target_sources(ScourEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(ScourEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(ScourEngineTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ScourEngineTest PRIVATE
        DecimulScour
        decimu_shared_ui
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# Test del motor sin JUCE (solo biblioteca estándar): cmake -DDECIMU_SCOUR_TESTS=ON
if(DECIMU_SCOUR_TESTS)
    add_executable(ScourDspTest tests/ScourDspTest.cpp)
    target_include_directories(ScourDspTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Source)
    target_compile_features(ScourDspTest PRIVATE cxx_std_20)
    target_compile_options(ScourDspTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /O2>)
endif()
