# Módulos DSP libres relevados

## Compresión / saturación → Airwindows

- Repo: https://github.com/airwindows/airwindows (MIT)
- Estructura: `plugins/<Plugin>/` con DSP puro en `.cpp`/`.h`, separable del wrapper.
- Candidatos para las 3 "carácter" de compresión de Decimul Acoustic:
  - **Pop!** → carácter suave/óptico, para "Glue"
  - **Pulse** → carácter más rápido/punchy, para "Snap"
  - **Volt** o **Velvet** → carácter más agresivo/saturado, para "Squash"
- Colección con GUI ya hecha para inspiración de comportamiento (no de código):
  [Air-G Plugin Collection](https://www.kvraudio.com/product/air-g-plugin-collection-by-air-g-audio)
  (18 plugins VST3, gratis/pay-what-you-like, cubre compresión, EQ, saturación y reverb).

## EQ

- Opción A: extraer **EaseQ** (4 bandas, curvas Bezier) o **Prism** (6 bandas gráfico) de
  Airwindows.
- Opción B (elegida para v1): implementar shelving/peak simple con `juce::dsp::IIR`,
  parametrizando 3 "modos" por banda con distinta frecuencia/Q/pendiente fija. Más liviano,
  más fácil de mantener, y alcanza para el caso de uso (2 bandas anchas tipo "color").

## Reverb

- **Airwindows Spaces**: 16 reverbs K-series en una sola interfaz (rooms, halls, plates,
  chambers, y algunos experimentales) — buena fuente para Space A / Space B.
- Referencia de calidad externa: [Dragonfly Reverb](https://github.com/michaelwillis/dragonfly-reverb)
  (LGPL, basado en freeverb3), muy usado como reverb "de estudio" gratuito — no se
  vendoriza en v1, queda como benchmark de calidad de decay/densidad.
- JUCE trae `juce::dsp::Reverb` (algoritmo tipo Freeverb) como fallback rápido para
  prototipar la UI antes de integrar Airwindows.

## Delay tempo-sync

- No hace falta librería externa: línea de delay circular + feedback + filtro en el lazo
  (para el "carácter" tipo tape/analógico, inspirado en el tratamiento que Airwindows le
  da a sus plugins de delay/ADT, pero implementación propia).
- Sync a host vía `AudioPlayHead::CurrentPositionInfo` (BPM); fallback a **tap tempo**
  manual cuando corre standalone sin host (valor agregado sobre el original).

## Reproductor de SoundFonts (SF2/SF3)

- Motor: **libfluidsynth** (LGPL v2.1) — https://github.com/FluidSynth/fluidsynth
- Referencia de arquitectura (no se fork­ea, es GPLv3 y no necesitamos su UI):
  [juicysfplugin](https://github.com/Birch-san/juicysfplugin) — sirve para ver cómo
  integran fluidsynth + JUCE (manejo de buffers de audio, carga de presets SF2, exposición
  de parámetros de reverb/chorus del propio fluidsynth).
- Plan: wrapper JUCE propio (`AudioProcessor` + `MidiKeyboardState`) llamando a
  `fluid_synth_process()` por bloque, con browser de presets embebido en la UI.

## Frameworks de plugin considerados

| Framework | Licencia | Elegido |
|---|---|---|
| **JUCE** | Gratis si el proyecto es GPLv3 (o pago para cerrado) | ✅ (encaja con donationware/open source) |
| DPF (DISTRHO) | ISC/MIT, siempre gratis | Alternativa si algún día se quiere código 100% permisivo |
| iPlug2 | Permisiva, gratis | Alternativa, comunidad más chica |
