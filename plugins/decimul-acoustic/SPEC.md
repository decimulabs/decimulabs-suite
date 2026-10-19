# DecimuLabs Acoustic — Spec

Multiefecto "todo en uno" para voz e instrumentos acústicos (guitarra, piano, cuerdas,
nylon). Inspirado en la estructura del Waves CLA Unplugged (ver
[docs/research/cla-unplugged-analysis.md](../../docs/research/cla-unplugged-analysis.md)),
reinterpretado con nombres propios y funciones nuevas.

Estado: **spec cerrada, parámetros 100% cableados, DSP propio (v0.5) funcionando, y UI
completa** (chasis de metal cepillado, compartimentos, knobs, faders, selectores tipo
LED, VU en arco — implementación en JUCE del mockup 3, ver
[docs/mockups/acoustic-panel-realistic-mockup.html](../../docs/mockups/acoustic-panel-realistic-mockup.html)).
Desde la v0.5 el DSP ya no es el de "referencia" de `juce::dsp`: reverb, compresor, EQ y
echo son propios (ver la sección v0.5 más abajo y `shared/Source/DecimuFx.h`). El "look" (LookAndFeel + chrome +
componentes) vive en [shared/](../../shared), no en este plugin — ver el README de la
raíz del repo.

## v0.5 — ronda de calidad del DSP (2026-10-18)

Sin cambiar ningún control visible, se reemplazaron los bloques genéricos de `juce::dsp` por DSP propio
(compartido con Guitars en `shared/Source/DecimuFx.h`, probado con `tests/EngineTest.cpp`):

- **Low / High (EQ)**: mismas fórmulas de shelving que JUCE (el test compara contra los coeficientes
  de JUCE: mismo color), pero con frecuencia y ganancia **suavizadas** (se recalculan cada 16 muestras,
  sin escalones al mover la perilla y sin crear objetos en el hilo de audio).
- **Dynamics**: compresor propio con detector **ligado** entre canales (mismo recorte en L y R, la
  imagen no se inclina), rodilla suave (Glue 10 dB, Snap 3, Squash 6), medidor de reducción **real**
  (antes incluía el makeup). Mapeo Amount/Character igual que antes (umbral hasta -24 dB, makeup hasta +6).
- **Space A / Space B**: reverb de red de retardos realimentados (FDN de 8 líneas, matriz de Householder),
  difusión de entrada con allpass, retardos modulados (sin sonido metálico), reflexiones tempranas y
  caída de agudos medida a 5 kHz. Space A y B usan dos variantes distintas (retardos y LFO propios).
  Medido: RT60 de graves dentro de ~±5 % de lo pedido y a 5 kHz dentro de ~±15 %. Siempre corre: al
  apagar el módulo o bajar el mix la cola se drena sola (antes podía volver audio viejo al re-prender).
  Pre-delay con tiempo suavizado.
- **Echo**: filtro (pasa-altos 140 Hz y pasa-bajos 4.5 kHz) y saturación suave dentro del lazo (las
  repeticiones se oscurecen y con feedback 100 % nunca explota), tiempo suavizado (un cambio de tempo
  se desliza en vez de pegar un salto) y vaciado al apagar.
- **Blend / mezcla de cada módulo**: suavizados.
- **Input / Output**: arrancan en 0 dB (antes un fade-in de 20 ms al empezar).
- **Quitados** los 3 parámetros que no hacían nada (`compOversample`, `spaceAPreDelaySync`,
  `spaceBPreDelaySync`): nunca estuvieron en la UI, solo aparecían en la lista del host.
- Nivel del retorno de reverb: ~3 dB por debajo del Freeverb anterior en Hall (más aire de headroom).

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
| `compEnabled` | — | bool (LED) | on/off | **on** | apaga Dynamics entero |
| `spaceAMix` | Space A | float 0-100% | 0..100 | 0 | |
| `spaceAType` | Space A Type | choice | Room / Hall / Cathedral | Room | |
| `spaceAPreDelay` | Space A Pre-Delay | knob, ms | 0-500ms | 0 | delay real antes del reverb |
| `spaceAEnabled` | — | bool (LED) | on/off | **on** | apaga Space A entero |
| `spaceBMix` | Space B | float 0-100% | 0..100 | 0 | |
| `spaceBType` | Space B Type | choice | Close / Wide / Vast | Close | |
| `spaceBPreDelay` | Space B Pre-Delay | knob, ms | 0-500ms | 0 | |
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
también. Que Marcelo oiga la v0.5 (reverb, compresor y echo suenan distinto: el reverb ya no es el Freeverb).
Posibles mejoras de sonido que dependen del oído: saturación/color (Airwindows, MIT, es una opción
para eso), más tipos de reverb, ping-pong en el echo.
