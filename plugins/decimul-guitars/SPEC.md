# DecimuLabs Guitars — Spec

Multiefecto "todo en uno" para guitarra eléctrica. Hermano de
[DecimuLabs Acoustic](../decimul-acoustic/SPEC.md): mismo esqueleto general y mismo
`shared/` (LookAndFeel, chrome, selectores, meters), pero con módulos que tienen
sentido específicamente para guitarra eléctrica (gate, drive, cab, modulación) en vez
de los que tenía Acoustic (pensados para voz/instrumento acústico). Tema de acento
**ámbar/dorado** (`DecimuAccentTheme::amberGold()`) en vez del cian/azul de Acoustic —
misma familia visual, plugin distinto.

## v0.4 — segunda ronda de testing real (2026-10-01)

Mismo tester, segunda pasada sobre la v0.2. Confirmó que el Gate ya no hace pop.
Fixes de esta ronda:

- **Drive suma `driveEnabled`** con LED clickeable: era el único módulo del "core" sin
  apagador propio — reportado directo en el testing.
- **LEDs más grandes**: área clickeable subida de 10px a 22px (el LED visible de 8px a
  18px) — costaba apuntar justo con el mouse en el tamaño anterior.
- **Drive: oversampling 4x para el pre-gain+waveshaper** (`juce::dsp::Oversampling`,
  half-band polyphase IIR). Esta es la causa real de lo que el tester describió como
  "un efecto fuzz con ring mod y sintetizador" al combinar Tube/Fuzz con Cab en Combo o
  Stack: con hasta +40dB de pre-gain antes del waveshaper, procesar a la sample rate
  original generaba armónicos por encima de Nyquist que se doblaban (aliasing) — más
  notorio en Tube/Fuzz (curvas de saturación más duras) que en Boost (tanh suave), y
  más audible a través del peak filter de "presencia" de Cab. El Cab en sí no tenía el
  bug: una vez que Drive deja de aliasear, Combo/Stack filtran correctamente con
  cualquier modo de Drive. Se sube/baja siempre (no solo con Drive activo) para que la
  latencia reportada del plugin sea constante — el host compensa automáticamente.
- **Toggle de Host Sync** (`hostSyncButton`, UI, no parámetro APVTS): el tester
  reportó que ni el TAP ni el BPM manual (nuevo en v0.2) tenían ningún efecto en Echo.
  Es por diseño — el host (Reaper) casi siempre da un tempo válido, y ese tenía
  prioridad absoluta sobre el manual — pero confirmado dos rondas seguidas que es
  confuso. Ahora hay un switch "SYNC" al lado del BPM: on (default) = se comporta como
  antes, el host manda si da un tempo válido; off = el manual/Tap mandan siempre,
  ignora el host. Se persiste en el estado del plugin (como `manualBpm`).
- **Phaser: depth/feedback/centerFrequency subidos de nuevo** (0.95→1.0 / 0.6→0.75,
  centerFrequency 700→1200Hz): seguía sin notarse sobre la guitarra (sí sobre ruido de
  fondo) — a 700Hz las muescas barrían por debajo de donde vive la mayoría de la
  energía armónica de una guitarra. Límite conocido: `juce::dsp::Phaser` tiene
  cantidad de etapas fija (no configurable), así que el margen de mejora por este
  camino es limitado — candidato a reemplazar por un phaser propio si sigue sin
  convencer.

Confirmado que **no es un bug** (documentado para no perseguir algo que no se puede
arreglar con un noise gate): el ruido de fondo vuelve cuando el gate se abre de nuevo.
Un noise gate silencia por debajo de un umbral, no elimina el ruido que está mezclado
con la señal real — eso es reducción de ruido (denoising), una herramienta distinta
(típicamente un noise print + spectral subtraction), no algo que un gate pueda hacer
por diseño. El tester lo marcó como "no es necesario", así que queda documentado acá y
sin construir.

## v0.2 — revisado tras el primer testing real (2026-09-30)

Un amigo de decimulabs probó la v0.1 con guitarra de verdad y mandó feedback punto por
punto. Cambios que salieron de ahí:

- **La cadena de señal se reordenó**: Gate/Drive/Tone/Cab pasan a ser el "core",
  SIEMPRE 100% activos — antes Blend los diluía como a todo lo demás, y en la práctica
  eso era confuso/poco útil para guitarra (sí tiene sentido para bajo, pero no lo
  pedimos acá). Blend ahora solo mezcla dry (post-core) contra core+retornos de
  Space/Modulation/Echo.
- **Cab pasó de fader de mezcla a on/off puro** (`cabEnabled`, reemplaza a
  `cabAmount`): mezclar "un poco" de color de gabinete no tenía sentido sonoro — o se
  quiere el cab entero, o no se quiere nada (para poder usar un cargador de IR aparte).
- **Tone/Mod/Space/Echo/Cab suman un booleano `...Enabled`** con LED clickeable en la
  UI (`DecimuLedButton`, en `shared/`): apaga/prende el módulo entero sin tener que
  bajarle el fader a cero — pedido explícito del testing, no era intuitivo.
- **Gate reescrito** (dejó de usar `juce::dsp::NoiseGate`): el modo Fast metía un "pop"
  en cada ataque de cuerda por chattering (abría/cerraba varias veces seguidas). La
  versión nueva es propia, con histéresis (umbral de cierre 6dB más bajo que el de
  apertura) y un hold mínimo antes de poder volver a cerrar.
- **Drive: rango de ganancia subido** de +23dB a +40dB tope — con niveles típicos de
  guitarra (bien por debajo de 0dBFS), +23dB no alcanzaba para que Fuzz fuzzeara de
  verdad.
- **Cab: filtros en cascada** (doble pasada, 4º orden / ~24dB por octava en vez de 2º
  orden / ~12dB) — pendiente más parecida a la caída real de un speaker.
- **Phaser: depth/feedback subidos** (0.7→0.95 / 0.3→0.6) — sonaba muy sutil, poco
  distinguible de Chorus/Flanger.
- **Output: piso bajado** de -24dB a -60dB — al mínimo todavía se escuchaba señal.
- **Space: headroom interno** (`wetLevel` tope 0.85 en vez de 1.0) — con Hall y el
  fader alto, el output se iba a rojo con facilidad.

Confirmado que **no son bugs** (documentado para no "arreglar" algo que ya funciona
bien): Tap Tempo no afecta nada en Echo cuando el host (DAW) da un tempo válido — el
plugin prioriza correctamente el BPM del host sobre el manual, el tap es solo fallback
para standalone. (Esto generó confusión real en dos rondas de testing seguidas — ver
el toggle Host Sync en v0.4, que deja forzar el manual aunque haya host). Echo Feedback
al 100% no diverge ni decae (ganancia exactamente unitaria en el loop) — es
matemáticamente correcto, y sirve como efecto stutter.

## Cadena de señal

```
Input Trim → Gate → Drive → Tone → Cab (= "core", siempre 100% activo)
                                      │
                                      ├─► Space ──────┐
                                      ├─► Modulation ──┼─► Echo ─► Blend ─► Output Trim
                                      └─(dry tap) ─────┘
```

`core` = Gate→Drive→Tone→Cab en serie, sin diluir por Blend. El dry tap para Blend se
toma DESPUÉS del core (no antes, como en la v0.1). `wet` = core + retornos de
Space/Modulation/Echo. Blend cruza entre ese dry y ese wet — o sea, solo decide cuánto
de Space/Mod/Echo se escucha, nunca cuánto de Drive/Tone.

## Parámetros

| ID | Nombre UI | Tipo | Rango | Default | Notas |
|---|---|---|---|---|---|
| `inputTrim` | Input | float dB | -24..+24 | 0 | |
| `gateAmount` | Gate | float 0-100% | 0..100 | 0 | 0% = sin gate; a más %, threshold más profundo |
| `gateMode` | Gate Character | choice | Fast / Med / Slow | Fast | attack/release/hold del gate propio |
| `driveAmount` | Drive | float 0-100% | 0..100 | 0 | pre-gain hacia el waveshaper (hasta +40dB) + makeup, oversampled 4x |
| `driveMode` | Drive Character | choice | Tube / Fuzz / Boost | Tube | curvas de saturación distintas, no solo ganancia |
| `driveEnabled` | — | bool (LED) | on/off | **on** | apaga Drive entero |
| `toneTilt` | Tone | float dB | -12..+12 | 0 | tilt EQ (corta graves/sube agudos o viceversa) |
| `toneMode` | Tone Character | choice | Scoop / Flat / Bright | Flat | pivote del tilt |
| `toneMidFreq` | Mid Sweep | knob, Hz | 300..3000 | 800 | frecuencia de un peak filter fijo (+4dB) |
| `toneEnabled` | — | bool (LED) | on/off | **on** | apaga Tone entero |
| `cabMode` | Cab Character | choice | Combo / Stack / DI | Combo | DI = sin color de cab aunque `cabEnabled` esté on |
| `cabEnabled` | — | bool (LED) | on/off | **off** | Cab es 100% wet cuando está on, nunca blend parcial |
| `modAmount` | Modulation | float 0-100% | 0..100 | 0 | |
| `modMode` | Modulation Character | choice | Chorus / Flanger / Phaser | Chorus | Chorus/Flanger = `juce::dsp::Chorus` con distinto centreDelay/feedback; Phaser = `juce::dsp::Phaser` |
| `modRate` | Mod Rate | knob, Hz | 0.1..5 | 0.8 | |
| `modEnabled` | — | bool (LED) | on/off | **on** | apaga Modulation entera |
| `spaceAmount` | Space | float 0-100% | 0..100 | 0 | un solo reverb (no dos como Acoustic) |
| `spaceMode` | Space Character | choice | Room / Plate / Hall | Room | |
| `spacePreDelay` | Space Pre-Delay | knob, ms | 0..500 | 0 | delay real antes del reverb |
| `spaceEnabled` | — | bool (LED) | on/off | **on** | apaga Space entero |
| `echoAmount` | Echo | float 0-100% | 0..100 | 0 | mismo módulo que Acoustic |
| `echoDivision` | Echo Division | choice | Slap / 8th / Quarter | Slap | tempo-synced |
| `echoFeedback` | Echo Feedback | knob, % | 0..100 | 35 | 100% = repite sin decaer (no diverge) |
| `echoEnabled` | — | bool (LED) | on/off | **on** | apaga Echo entero |
| `blend` | Blend | float % | 0..100 | 100 | solo mezcla Space+Mod+Echo, ver arriba |
| `outputTrim` | Output | float dB | -60..+24 | 0 | |
| `bypass` | Bypass | bool | — | off | se dibuja como el indicador "REC" |

**Tap Tempo** (`tapTempo()` + `manualBpm`) y **BPM manual por texto** (`setManualBpm()`
+ `bpmLabel` editable): mismo patrón que Acoustic — métodos del processor, no
parámetros APVTS, para fallback de tempo sin host. Por default el host (cuando da un
tempo válido) tiene prioridad sobre ambos; el toggle **Host Sync** (`hostSyncEnabled`,
también UI no-APVTS, ver v0.4 arriba) deja apagar esa prioridad para forzar el manual/
Tap aunque haya host.

**Buses**: acepta mono→estéreo (DI/mic de amp) y estéreo→estéreo, igual que Acoustic.

## DSP: qué es real y qué es "de referencia"

Igual que Acoustic, esto es una implementación de REFERENCIA — suena a compresor/
drive/reverb genéricos, no es la calidad final (esa viene después con Airwindows u
otro DSP curado). Detalle por módulo:

- **Gate**: implementación propia (no `juce::dsp::NoiseGate`, ver v0.2 arriba) —
  envelope follower + histéresis 6dB + hold configurable por `gateMode`.
- **Drive**: `juce::dsp::WaveShaper` con una curva distinta por `driveMode` (tanh
  simétrico para Tube, exponencial asimétrico para Fuzz, tanh suave para Boost),
  hasta +40dB de pre-gain, + makeup gain aproximado (no calibrado por oído). Todo el
  pre-gain+shaping corre sobremuestreado 4x (`juce::dsp::Oversampling`, half-band
  polyphase IIR) para evitar aliasing a pre-gain alto — ver v0.4. Agrega latencia
  reportada al plugin (compensada por el host via PDC).
- **Tone**: tilt EQ (low-shelf + high-shelf opuestos) vía `juce::dsp::IIR` + un peak
  filter fijo para el mid-sweep.
- **Cab**: `juce::dsp::IIR` en cadena, doble pasada (4º orden) — **sigue sin ser un
  simulador de gabinete real** (eso sería convolución con impulse responses, mucho más
  pesado); es una aproximación con EQ, ahora con pendiente más pronunciada.
- **Modulation**: `juce::dsp::Chorus` (Chorus/Flanger, cambiando centreDelay/feedback)
  o `juce::dsp::Phaser` (Phaser) según `modMode`.
- **Space / Echo**: mismo código que Acoustic (`juce::dsp::Reverb` + pre-delay real;
  `DelayLine` con feedback tempo-synced), con headroom agregado en Space.
- Sin smoothing de parámetros salvo Input/Output trim (mismo trade-off documentado que
  Acoustic — mover controles rápido en reproducción puede sonar a zipper leve).

## Próximo paso

Otra ronda de testing con la v0.4 para confirmar que el aliasing de Drive/Cab quedó
resuelto y que Host Sync resuelve la confusión del BPM. A más largo plazo: reemplazar
Drive/Cab/Gate por DSP curado (Airwindows tiene saturadores; un cab sim real
necesitaría impulse responses, que es un proyecto aparte), considerar un phaser propio
si el de `juce::dsp` sigue sin convencer, y exponer el feedback del Echo con saturación
propia arriba del 100% si se usa mucho como efecto stutter.
