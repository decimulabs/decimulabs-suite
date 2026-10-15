# decimulabs suite

Suite de plugins de audio (VST3) de [decimulabs](https://www.decimulabs.com), pensada como
**freeware / donationware**: una alternativa gratuita y de código abierto a colecciones tipo
Waves CLA. No son copias — cada plugin toma la *idea* de un tipo de procesador clásico
("channel strip todo-en-uno para X") y la reinterpreta con su propia cadena de señal,
su propio nombre de controles, y funciones que las originales no tienen (ver "valor
agregado" en cada spec).

## Por qué freeware/donationware cambia las decisiones técnicas

- **Framework: [JUCE](https://juce.com/)**, bajo su licencia **GPLv3 gratuita** (no la
  paga "Indie/Pro"). Esa licencia es gratis solo si el proyecto también se libera como
  open source — lo cual encaja perfecto con el modelo donationware. Ver
  [docs/licensing-notes.md](docs/licensing-notes.md).
- Todo el DSP de terceros que usemos tiene que ser compatible con GPLv3 (Airwindows es
  MIT, fluidsynth y Dragonfly Reverb son LGPL — todos compatibles).
- Botón de "donar" (Ko-fi/Patreon/Buy Me a Coffee) en el about-box de cada plugin, nada de
  paywall ni trial.

## `shared/`: la base visual de toda la suite

Todos los plugins de la línea "channel strip" (Acoustic, y a futuro Guitars/Bass/Drums/
Vocals, siguiendo la idea de colección tipo CLA Artist Series) comparten la misma
identidad de hardware — chasis de metal cepillado, compartimentos con bisel, knobs con
relieve real, tornillos Phillips, manijas de rack, selectores tipo LED-stack, meters
segmentados, VU en arco — para que la suite se sienta como una familia de productos, no
como plugins sueltos con estilos distintos.

Esa estética vive **una sola vez**, en [`shared/`](shared), no copiada en cada plugin:

- `shared/Source/DecimuLookAndFeel.h/.cpp` — paleta, tipografías (Chakra Petch + VT323,
  las mismas del sitio), y el dibujo de knobs/faders/combos/botones.
- `shared/Source/DecimuChrome.h/.cpp` — funciones de dibujo del "chasis" (fondo,
  tornillos, manijas de rack, compartimentos con bisel, LED de knob). Son funciones
  libres, no Componentes — cada plugin arma su propio layout de módulos y llama a estas
  funciones desde su `PluginEditor::paint()`.
- `shared/Source/DecimuLedStackSelector.h/.cpp`, `DecimuLadderMeter.h/.cpp`,
  `DecimuVuArc.h/.cpp` — Componentes reutilizables (selector de 2-4 opciones, meter
  segmentado, VU en arco).
- `shared/Assets/` — logo (`wordmark.png`, `dog.png`), tipografías OFL, foto del estudio.
  Ver [shared/Assets/README.md](shared/Assets/README.md) para origen y licencias.

Un plugin nuevo linkea `shared/` desde su `CMakeLists.txt` (`add_subdirectory` +
`target_link_libraries(... decimu_shared_ui)`, ver el de
[plugins/decimul-acoustic](plugins/decimul-acoustic/CMakeLists.txt)) y solo tiene que
escribir su propio DSP (`PluginProcessor`) y componer su propio layout de módulos
(`PluginEditor`) — la identidad visual sale gratis.

## Plugins planeados

| Plugin | Estado | Carpeta |
|---|---|---|
| **DecimuLabs Acoustic** — multiefecto para voz/instrumentos acústicos | 🟢 DSP de referencia + UI completa | [plugins/decimul-acoustic](plugins/decimul-acoustic) |
| **DecimuLabs Guitars** — multiefecto para guitarra eléctrica (Gate/Drive/Tone/Cab/Modulation/Space/Echo) | 🟢 DSP de referencia + UI completa, tema ámbar | [plugins/decimul-guitars](plugins/decimul-guitars) |
| **DecimuLabs Versus (AB)** — comparador A/B de mezclas y masterings contra referencias, con volumen igualado por LUFS | 🟢 MVP funcionando + test de motor, tema violeta | [plugins/decimul-versus](plugins/decimul-versus) |
| **DecimuLabs Gap** — bombeo de volumen sincronizado (sidechain "de mentira") con curva dibujable, trigger MIDI/audio y banda baja | 🟢 MVP funcionando + test de motor, tema lima | [plugins/decimul-gap](plugins/decimul-gap) |
| **DecimuLabs Trigger** (nombre de trabajo) — trigger de batería: detecta golpes y su intensidad, sale MIDI con velocity y reproductor propio con capas y round-robin | 🟢 v0.5 funcionando (calibrar, polaridad/desfase/ganancia/afinación por muestra, MIDI de entrada y exportación a .mid, además de la huella contra bleed y la captura de muestras) + test de motor, tema coral | [plugins/decimul-trigger](plugins/decimul-trigger) |
| **Decimul SF2** — reproductor de soundfonts (sampler) | ⚪ pendiente | [plugins/sf2-player](plugins/sf2-player) |
| **Bass / Drums / Vocals** (a definir) | ⚪ ideas abiertas, reusarían `shared/` | — |

## Estructura del repo

```
decimulabs-suite/
├── docs/
│   ├── research/           → análisis de plugins de referencia y de módulos DSP libres
│   └── mockups/            → mockups HTML de UI (previos a implementar en JUCE)
├── shared/                 → LookAndFeel + chrome + componentes + assets de marca,
│                              comunes a toda la suite (ver sección de arriba)
├── plugins/
│   ├── decimul-acoustic/   → primer plugin (DSP + UI completos)
│   └── sf2-player/         → segundo plugin (pendiente)
└── third_party/            → DSP vendorizado de terceros con su licencia (Airwindows, etc.)
```

## Cómo compilar

Cada plugin es un proyecto CMake independiente que descarga JUCE vía `FetchContent` y
linkea `shared/`. Requisitos en Windows: **Visual Studio 2022** (Desktop development with
C++) + **CMake**.

```bash
cd plugins/decimul-acoustic
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
