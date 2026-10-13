cmake_minimum_required(VERSION 3.22)
project(DecimulVersus VERSION 0.0.1)

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

# El nombre del producto lleva "(AB)" y no "A/B": la barra no es válida en el nombre de
# carpeta del .vst3 en Windows. En la UI sí se puede escribir "A/B".
juce_add_plugin(DecimulVersus
    COMPANY_NAME "decimulabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dvs1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Versus (AB)"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulVersus PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/VersusComponents.cpp
    Source/LoudnessAnalyzer.cpp
    Source/BandAverager.cpp
    Source/ReferenceTrack.cpp
)

# Ídem Acoustic/Guitars: sin esto, MSVC corrompe literales no-ASCII del código fuente.
target_compile_options(DecimulVersus PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# MP3 activado a propósito: la mayoría de las referencias que la gente tiene a mano
# son mp3. (WAV/AIFF/FLAC/OGG vienen siempre; en Windows también WMA/M4A por Media Foundation.)
target_compile_definitions(DecimulVersus PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(DecimulVersus PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test de integración del motor (opcional): cmake -DDECIMU_VERSUS_TESTS=ON
option(DECIMU_VERSUS_TESTS "Build the Versus engine integration test" OFF)
if(DECIMU_VERSUS_TESTS)
    juce_add_console_app(VersusEngineTest PRODUCT_NAME "VersusEngineTest")
    target_sources(VersusEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(VersusEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(VersusEngineTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(VersusEngineTest PRIVATE
        DecimulVersus
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
