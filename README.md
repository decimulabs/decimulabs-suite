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

## Plugins planeados

| Plugin | Estado | Carpeta |
|---|---|---|
| **Decimul Acoustic** — multiefecto para voz/instrumentos acústicos | 🟡 spec + esqueleto listo, falta UI y DSP real | [plugins/decimul-acoustic](plugins/decimul-acoustic) |
| **Decimul SF2** — reproductor de soundfonts (sampler) | ⚪ pendiente | [plugins/sf2-player](plugins/sf2-player) |
| *(a definir)* | ⚪ idea abierta | — |

## Estructura del repo

```
decimulabs-suite/
├── docs/
│   └── research/          → análisis de plugins de referencia y de módulos DSP libres
├── plugins/
│   ├── decimul-acoustic/  → primer plugin (spec + esqueleto JUCE)
│   └── sf2-player/        → segundo plugin (pendiente)
├── third_party/           → JUCE (submódulo) + DSP vendorizado de terceros con su licencia
└── branding/              → logo, paleta de color, assets de decimulabs
```

## Cómo compilar (cuando haya código real)

Cada plugin es un proyecto CMake independiente que descarga JUCE vía `FetchContent`.
Requisitos en Windows: **Visual Studio 2022** (Desktop development with C++) + **CMake**.

```bash
cd plugins/decimul-acoustic
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
