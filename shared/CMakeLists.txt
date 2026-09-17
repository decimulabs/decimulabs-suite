# "Look" compartido de decimulabs: LookAndFeel + helpers de dibujo (chasis, tornillos,
# manijas de rack, compartimentos con bisel, knobs con LED, VU en arco) + assets de marca
# (logo, tipografías). Pensado para que cada plugin de la suite (Acoustic, y a futuro
# Guitars/Bass/Drums/etc.) linkee esto en vez de reimplementar la estética desde cero.
#
# Este archivo NO define su propio project() ni hace FetchContent de JUCE: asume que el
# CMakeLists.txt del plugin que lo incluye (vía add_subdirectory) ya hizo
# FetchContent_MakeAvailable(JUCE) antes, así las funciones juce_add_binary_data() etc.
# ya están disponibles.

juce_add_binary_data(DecimuBrandAssets SOURCES
    Assets/wordmark.png
    Assets/dog.png
    Assets/ChakraPetch-Regular.ttf
    Assets/ChakraPetch-Bold.ttf
    Assets/VT323-Regular.ttf
)
set_target_properties(DecimuBrandAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

add_library(decimu_shared_ui STATIC
    Source/DecimuLookAndFeel.cpp
    Source/DecimuChrome.cpp
    Source/DecimuLedStackSelector.cpp
    Source/DecimuLadderMeter.cpp
    Source/DecimuVuArc.cpp
)

target_include_directories(decimu_shared_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_link_libraries(decimu_shared_ui PUBLIC
    juce::juce_gui_basics
    juce::juce_audio_processors
    DecimuBrandAssets
)

# Ídem CMakeLists.txt de cada plugin: sin esto, MSVC corrompe literales no-ASCII.
target_compile_options(decimu_shared_ui PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
