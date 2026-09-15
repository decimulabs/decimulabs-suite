# Decimul SF2 (pendiente)

Reproductor de soundfonts (SF2/SF3) para decimulabs. Motor propuesto: **libfluidsynth**
(LGPL v2.1) envuelto en un `AudioProcessor` de JUCE propio — ver
[docs/research/dsp-references.md](../../docs/research/dsp-references.md) para el detalle
de por qué no se fork­ea `juicysfplugin` (es GPLv3 pero con arquitectura/UI que no
necesitamos heredar; igual sirve como referencia de integración).

Todavía no tiene spec ni esqueleto — se arma después de terminar DecimuLabs Acoustic.

Ideas sueltas para cuando se especifique:

- Browser de presets embebido (navegar bancos/instrumentos del SF2 cargado).
- Reverb/chorus propios de fluidsynth expuestos como parámetros automatizables.
- Carga de múltiples SF2 con key-splits/layers (valor agregado sobre un reproductor SF2 básico).
- Salida multicanal opcional (un canal por instrumento del banco, útil para reamplificar por separado).
