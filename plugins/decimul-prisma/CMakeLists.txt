cmake_minimum_required(VERSION 3.22)
project(DecimulPrisma VERSION 0.0.1)

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

# Nombre de trabajo "Prisma": se cambia acá (PRODUCT_NAME) y en los textos de la UI.
juce_add_plugin(DecimulPrisma
    COMPANY_NAME "decimulabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dpr1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Prisma"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulPrisma PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EqDisplay.cpp
    Source/BandPanel.cpp
)

# Ídem el resto de la suite: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(DecimulPrisma PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulPrisma PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulPrisma PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    decimu_shared_ui
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Test de integración del plugin (opcional): cmake -DDECIMU_PRISMA_TESTS=ON
# El motor de audio tiene además su test rápido sin JUCE: tests/build_dsp.bat.
option(DECIMU_PRISMA_TESTS "Build the Prisma plugin integration test" OFF)
if(DECIMU_PRISMA_TESTS)
    juce_add_console_app(PrismaEngineTest PRODUCT_NAME "PrismaEngineTest")
    target_sources(PrismaEngineTest PRIVATE tests/EngineTest.cpp)
    target_compile_options(PrismaEngineTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(PrismaEngineTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(PrismaEngineTest PRIVATE
        DecimulPrisma
        decimu_shared_ui
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
