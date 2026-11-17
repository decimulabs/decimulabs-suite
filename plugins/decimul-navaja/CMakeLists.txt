cmake_minimum_required(VERSION 3.22)
project(DecimulNavaja VERSION 0.1.0)

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

juce_add_plugin(DecimulNavaja
    COMPANY_NAME "DecimuLabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dnv1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Navaja"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulNavaja PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulNavaja PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulNavaja PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulNavaja PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test del motor (C++ puro, sin JUCE): cmake -DDECIMU_NAVAJA_TESTS=ON
option(DECIMU_NAVAJA_TESTS "Build the Navaja engine test" OFF)
if(DECIMU_NAVAJA_TESTS)
    add_executable(NavajaEngineTest tests/EngineTest.cpp)
    target_compile_options(NavajaEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

# Captura de la UI sin DAW (opcional): cmake -DDECIMU_NAVAJA_UISHOT=ON
option(DECIMU_NAVAJA_UISHOT "Build the Navaja UI screenshot tool" OFF)
if(DECIMU_NAVAJA_UISHOT)
    juce_add_console_app(NavajaUiShot PRODUCT_NAME "NavajaUiShot")
    target_sources(NavajaUiShot PRIVATE tests/UiShot.cpp)
    target_compile_options(NavajaUiShot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(NavajaUiShot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(NavajaUiShot PRIVATE
        DecimulNavaja
        decimu_shared_ui
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
