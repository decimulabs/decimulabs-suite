cmake_minimum_required(VERSION 3.22)
project(DecimulAtaque VERSION 0.0.1)

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

# Nombre de trabajo "Ataque": se cambia acá (PRODUCT_NAME) y en los textos de la UI.
juce_add_plugin(DecimulAtaque
    COMPANY_NAME "DecimuLabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dat1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Ataque"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulAtaque PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TransientView.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulAtaque PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulAtaque PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulAtaque PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test de integración del motor (opcional): cmake -DDECIMU_ATAQUE_TESTS=ON
option(DECIMU_ATAQUE_TESTS "Build the Ataque engine integration test" OFF)
if(DECIMU_ATAQUE_TESTS)
    juce_add_console_app(AtaqueEngineTest PRODUCT_NAME "AtaqueEngineTest")
    target_sources(AtaqueEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(AtaqueEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(AtaqueEngineTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AtaqueEngineTest PRIVATE
        DecimulAtaque
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# Captura de la UI sin DAW (opcional): cmake -DDECIMU_ATAQUE_UISHOT=ON
option(DECIMU_ATAQUE_UISHOT "Build the Ataque UI screenshot tool" OFF)
if(DECIMU_ATAQUE_UISHOT)
    juce_add_console_app(AtaqueUiShot PRODUCT_NAME "AtaqueUiShot")
    target_sources(AtaqueUiShot PRIVATE tests/UiShot.cpp)
    target_compile_options(AtaqueUiShot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(AtaqueUiShot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AtaqueUiShot PRIVATE
        DecimulAtaque
        decimu_shared_ui
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
