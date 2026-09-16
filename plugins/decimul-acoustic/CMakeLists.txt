cmake_minimum_required(VERSION 3.22)
project(DecimulAcoustic VERSION 0.0.1)

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

juce_add_plugin(DecimulAcoustic
    COMPANY_NAME "decimulabs"
    COMPANY_WEBSITE "https://www.decimulabs.com"
    COMPANY_COPYRIGHT "decimulabs"
    PLUGIN_MANUFACTURER_CODE Dcml
    PLUGIN_CODE Dac1
    FORMATS VST3 Standalone
    PRODUCT_NAME "DecimuLabs Acoustic"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(DecimulAcoustic PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DecimuLookAndFeel.cpp
)

# Assets de marca (logo + tipografías OFL), ver Assets/README.md. Quedan compilados
# adentro del binario del plugin, no se cargan desde ningún lado en runtime.
juce_add_binary_data(DecimulAcousticAssets SOURCES
    Assets/wordmark.png
    Assets/dog.png
    Assets/studio-bg.jpg
    Assets/ChakraPetch-Regular.ttf
    Assets/ChakraPetch-Bold.ttf
    Assets/VT323-Regular.ttf
)
set_target_properties(DecimulAcousticAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Sin esto, MSVC reinterpreta los literales de string no-ASCII del código fuente con
# el codepage ANSI local en vez de UTF-8, y corrompe cualquier caracter especial (por
# ejemplo "·" termina mostrándose como "Â·"). /utf-8 fija tanto el charset de lectura
# del fuente como el de ejecución.
target_compile_options(DecimulAcoustic PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(DecimulAcoustic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(DecimulAcoustic PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    DecimulAcousticAssets
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
