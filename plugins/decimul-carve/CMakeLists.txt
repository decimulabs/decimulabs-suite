cmake_minimum_required(VERSION 3.22)
project(DecimulCarve VERSION 0.1.0)

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

juce_add_plugin(DecimulCarve
    COMPANY_NAME "DecimuLabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dcv1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Carve"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulCarve PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectrumDisplay.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulCarve PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulCarve PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulCarve PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test del motor (C++ puro, sin JUCE): cmake -DDECIMU_CARVE_TESTS=ON
option(DECIMU_CARVE_TESTS "Build the Carve engine test" OFF)
if(DECIMU_CARVE_TESTS)
    add_executable(CarveEngineTest tests/EngineTest.cpp)
    target_compile_options(CarveEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

# Captura de la UI sin DAW (opcional): cmake -DDECIMU_CARVE_UISHOT=ON
option(DECIMU_CARVE_UISHOT "Build the Carve UI screenshot tool" OFF)
if(DECIMU_CARVE_UISHOT)
    juce_add_console_app(CarveUiShot PRODUCT_NAME "CarveUiShot")
    target_sources(CarveUiShot PRIVATE tests/UiShot.cpp)
    target_compile_options(CarveUiShot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(CarveUiShot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(CarveUiShot PRIVATE
        DecimulCarve
        decimu_shared_ui
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
