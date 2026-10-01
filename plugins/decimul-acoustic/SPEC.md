# DecimuLabs Acoustic — Spec

Multiefecto "todo en uno" para voz e instrumentos acústicos (guitarra, piano, cuerdas,
nylon). Inspirado en la estructura del Waves CLA Unplugged (ver
[docs/research/cla-unplugged-analysis.md](../../docs/research/cla-unplugged-analysis.md)),
reinterpretado con nombres propios y funciones nuevas.

Estado: **spec cerrada, parámetros 100% cableados, DSP de referencia funcionando, y UI
completa** (chasis de metal cepillado, compartimentos, knobs, faders, selectores tipo
LED, VU en arco — implementación en JUCE del mockup 3, ver
[docs/mockups/acoustic-panel-realistic-mockup.html](../../docs/mockups/acoustic-panel-realistic-mockup.html)).
El DSP sigue siendo una implementación de REFERENCIA con clases built-in de
`juce::dsp` — no es todavía la calidad final basada en Airwindows, ver limitaciones
conocidas al inicio de `PluginProcessor.h`. El "look" (LookAndFeel + chrome +
componentes) vive en [shared/](../../shared), no en este plugin — ver el README de la
raíz del repo.

## v0.3 — generalizado desde la 2da ronda de testing real de Guitars (2026-10-01)

Guitars tuvo una segunda ronda de testing real; dos de los fixes generalizan directo
acá (ver el changelog completo en
[plugins/decimul-guitars/SPEC.md](../decimul-guitars/SPEC.md)):

- **LEDs más grandes**: área clickeable subida de 10px a 22px (LED visible de 8px a
  18px) — en Guitars costaba apuntar justo con el mouse en el tamaño anterior.
- **Toggle de Host Sync** (`hostSyncButton`, UI, no parámetro APVTS): en Guitars, el
  tester reportó que el BPM manual/Tap "no hacía nada" en Echo — es por diseño (el
  host tiene prioridad si da un tempo válido), pero confuso. Ahora hay un switch "SYNC"
  junto al BPM: on (default) = comportamiento de antes; off = fuerza el manual/Tap
  aunque haya host. Se persiste en el estado del plugin.

Los demás fixes de la 2da ronda de Guitars (Drive oversampled, `driveEnabled`, Phaser)
no aplican acá: Acoustic no tiene Drive ni Phaser.

## v0.2 — generalizado desde el testing real de Guitars (2026-09-30)

Guitars pasó por una ronda de testing real con guitarrista y salieron fixes que
también aplican acá (mismo `shared/`, mismo patrón de cadena de señal). Ver el
changelog completo en [plugins/decimul-guitars/SPEC.md](../decimul-guitars/SPEC.md).

- **Blend reubicado**: Low/High/Dynamics pasan a ser "core", SIEMPRE 100% activos — el
  dry tap se toma DESPUÉS del core (no antes, como en v0.1). Blend ya no diluye el
  EQ/compresor, solo mezcla dry vs. los retornos de Space A/Space B/Echo.
- **Cada uno de los 6 módulos principales suma un booleano `...Enabled`** con LED
  clickeable (`DecimuLedButton`, en `shared/`) — apaga el módulo entero sin bajarle el
  fader a cero.
- **Reverbs con headroom interno** (`wetLevel` tope 0.85 en vez de 1.0) — mismo
  problema que tenía Guitars con Hall: con el fader alto se iba a rojo con facilidad.
- **Output: piso bajado** de -24dB a -60dB.
- **Casillero de texto para el BPM manual** (`bpmLabel` editable, doble click) —
  alternativa al botón TAP, mismo uso (fallback sin host).

## Cadena de señal

```
Input Trim ─► Low ─► High ─► Dynamics (= "core", siempre 100% activo)
                                 │
                                 ├─► Space A ──────┐
                                 ├─► Space B ──────┼─► Echo ─► Blend ─► Output Trim
                                 └─(dry tap) ───────┘
```

`core` = Low→High→Dynamics en serie, sin diluir por Blend (cada uno puede apagarse
individual con su LED). Space A y Space B corren en paralelo alimentados por el core,
se suman, y esa suma entra a Echo. El dry tap para Blend se toma DESPUÉS del core.
Blend cruza entre ese dry y ese wet (core + retornos) — o sea, solo decide cuánto de
Space A/Space B/Echo se escucha, nunca cuánto de Low/High/Dynamics.

## Parámetros (para `AudioProcessorValueTreeState`)

| ID | Nombre UI | Tipo | Rango | Default | Notas |
|---|---|---|---|---|---|
| `inputTrim` | Input | float dB | -24..+24 | 0 | |
| `lowGain` | Low | float dB | -12..+12 | 0 | |
| `lowMode` | Low Character | choice | Warm / Deep / Air-Cut | Warm | 3 filtros distintos, no solo ganancia |
| `lowEnabled` | — | bool (LED) | on/off | **on** | apaga Low entero |
| `highGain` | High | float dB | -12..+12 | 0 | |
| `highMode` | High Character | choice | Silk / Sheen / Crisp | Silk | |
| `highEnabled` | — | bool (LED) | on/off | **on** | apaga High entero |
| `compAmount` | Dynamics | float 0-100% | 0..100 | 0 | |
| `compMode` | Dynamics Character | choice | Glue / Snap / Squash | Glue | |
| `compOversample` | Oversample x2/x4 | choice | Off / 2x / 4x | Off | declarado, todavía sin efecto (ver limitaciones en el .h) |
| `compEnabled` | — | bool (LED) | on/off | **on** | apaga Dynamics entero |
| `spaceAMix` | Space A | float 0-100% | 0..100 | 0 | |
| `spaceAType` | Space A Type | choice | Room / Hall / Cathedral | Room | |
| `spaceAPreDelay` | Space A Pre-Delay | knob, ms | 0-500ms | 0 | delay real antes del reverb |
| `spaceAPreDelaySync` | Space A Pre-Delay Sync | bool | on/off | off | declarado, todavía sin efecto |
| `spaceAEnabled` | — | bool (LED) | on/off | **on** | apaga Space A entero |
| `spaceBMix` | Space B | float 0-100% | 0..100 | 0 | |
| `spaceBType` | Space B Type | choice | Close / Wide / Vast | Close | |
| `spaceBPreDelay` | Space B Pre-Delay | knob, ms | 0-500ms | 0 | |
| `spaceBPreDelaySync` | Space B Pre-Delay Sync | bool | on/off | off | declarado, todavía sin efecto |
| `spaceBEnabled` | — | bool (LED) | on/off | **on** | apaga Space B entero |
| `echoMix` | Echo | float 0-100% | 0..100 | 0 | |
| `echoDivision` | Echo Division | choice | Slap / 8th / Quarter | Slap | tempo-synced |
| `echoFeedback` | Echo Feedback | knob, % | 0..100 | 35 | 100% = repite sin decaer (no diverge) |
| `echoEnabled` | — | bool (LED) | on/off | **on** | apaga Echo entero |
| `blend` | Blend (Dry/Wet) | float % | 0..100 | 100 | solo mezcla Space A+Space B+Echo, ver arriba |
| `outputTrim` | Output | float dB | -60..+24 | 0 | |
| `bypass` | Bypass general | bool | — | off | se dibuja como el indicador "REC" |

Meters (no automatizables, solo lectura para la UI): input peak, output peak, gain
reduction del compresor, correlación estéreo post Space A+B (calculada, sin widget
propio en la UI todavía).

**Tap Tempo** (`tapTempo()` + `manualBpm`) y **BPM manual por texto**
(`setManualBpm()` + `bpmLabel` editable) quedan afuera del APVTS a propósito: son
gestos transitorios de UI (click o tipeo), no valores con sentido para automatizar
desde una DAW. Viven como métodos públicos en `DecimulAcousticProcessor`; el BPM
calculado se guarda igual en el estado del plugin (XML) para que sobreviva a cerrar/
abrir la sesión. Por default, cuando hay host con tempo válido, el host tiene
prioridad sobre ambos — comportamiento esperado, confirmado en testing con Guitars.
El toggle **Host Sync** (`hostSyncEnabled`, v0.3, también UI no-APVTS) deja apagar esa
prioridad para forzar el manual/Tap aunque haya host — generalizado desde Guitars,
donde en dos rondas de testing seguidas esto generó confusión real.

**Buses de entrada**: acepta mono→estéreo (mic/DI directo) además de estéreo→estéreo,
igual que los dos componentes del CLA Unplugged original — ver `isBusesLayoutSupported`.

## Valor agregado sobre el original (para que no sea "una copia")

Construido: bypass real por módulo (1), blend continuo (2, redefinido en v0.2), tap
tempo + BPM manual (4), UI vectorial escalable (8), licencia GPLv3 (10). Los demás
(auto-gain, oversampling activo, overlay de espectro, medidor de correlación con
widget propio, presets de fábrica) son ideas anotadas, todavía no construidas — se
dejan documentadas acá para no perderlas, pero no describen el estado actual del
plugin.

## Próximo paso

Otra ronda de testing (ahora con las voces/acústicos que es el target real de este
plugin) para confirmar que el reordenamiento de Blend y los LEDs se sienten bien acá
también. A más largo plazo: reemplazar el DSP de referencia por Airwindows (ver
docs/research/dsp-references.md) para pasar de "suena a compresor/reverb genéricos" a
la calidad final — la UI no debería necesitar cambios para eso, solo cambia qué hay
detrás de cada parámetro.
