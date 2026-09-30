# DecimuLabs Guitars — Spec

Multiefecto "todo en uno" para guitarra eléctrica. Hermano de
[DecimuLabs Acoustic](../decimul-acoustic/SPEC.md): mismo esqueleto general y mismo
`shared/` (LookAndFeel, chrome, selectores, meters), pero con módulos que tienen
sentido específicamente para guitarra eléctrica (gate, drive, cab, modulación) en vez
de los que tenía Acoustic (pensados para voz/instrumento acústico). Tema de acento
**ámbar/dorado** (`DecimuAccentTheme::amberGold()`) en vez del cian/azul de Acoustic —
misma familia visual, plugin distinto.

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
para standalone. Echo Feedback al 100% no diverge ni decae (ganancia exactamente
unitaria en el loop) — es matemáticamente correcto, y sirve como efecto stutter.

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
| `driveAmount` | Drive | float 0-100% | 0..100 | 0 | pre-gain hacia el waveshaper (hasta +40dB) + makeup |
| `driveMode` | Drive Character | choice | Tube / Fuzz / Boost | Tube | curvas de saturación distintas, no solo ganancia |
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

**Tap Tempo** (`tapTempo()` + `manualBpm`): mismo patrón que Acoustic — método del
processor, no parámetro APVTS, para fallback de tempo sin host. El host (cuando hay
uno con tempo válido) tiene prioridad sobre el tap — comportamiento esperado.

**Buses**: acepta mono→estéreo (DI/mic de amp) y estéreo→estéreo, igual que Acoustic.

## DSP: qué es real y qué es "de referencia"

Igual que Acoustic, esto es una implementación de REFERENCIA — suena a compresor/
drive/reverb genéricos, no es la calidad final (esa viene después con Airwindows u
otro DSP curado). Detalle por módulo:

- **Gate**: implementación propia (no `juce::dsp::NoiseGate`, ver v0.2 arriba) —
  envelope follower + histéresis 6dB + hold configurable por `gateMode`.
- **Drive**: `juce::dsp::WaveShaper` con una curva distinta por `driveMode` (tanh
  simétrico para Tube, exponencial asimétrico para Fuzz, tanh suave para Boost),
  hasta +40dB de pre-gain, + makeup gain aproximado (no calibrado por oído).
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

Otra ronda de testing con la v0.2 para confirmar que el Gate/Drive/Cab quedaron bien, y
después seguir afinando por oído. A más largo plazo: reemplazar Drive/Cab/Gate por DSP
curado (Airwindows tiene saturadores; un cab sim real necesitaría impulse responses,
que es un proyecto aparte), y considerar exponer el feedback del Echo con saturación
propia arriba del 100% si se usa mucho como efecto stutter.
