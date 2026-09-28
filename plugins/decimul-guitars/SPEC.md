# DecimuLabs Guitars — Spec

Multiefecto "todo en uno" para guitarra eléctrica. Hermano de
[DecimuLabs Acoustic](../decimul-acoustic/SPEC.md): mismo esqueleto de
"channel strip con retornos" y mismo `shared/` (LookAndFeel, chrome, selectores,
meters), pero con módulos que tienen sentido específicamente para guitarra eléctrica
(gate, drive, cab, modulación) en vez de los que tenía Acoustic (pensados para voz/
instrumento acústico). Tema de acento **ámbar/dorado** (`DecimuAccentTheme::amberGold()`)
en vez del cian/azul de Acoustic — misma familia visual, plugin distinto.

## Cadena de señal

```
Input Trim → Gate → Drive → Tone → Cab ─┬─► Space ─┐
                                          ├─► Modulation ─┼─► Echo ─► Blend ─► Output Trim
                                          └─(dry tap) ────┘
```

Mismo patrón que Acoustic: `core` = todo lo serie (Gate→Drive→Tone→Cab), `dry` = señal
post Input Trim (antes del core), `wet` = core + retornos de Space/Modulation/Echo,
Blend cruza entre dry y wet.

## Parámetros

| ID | Nombre UI | Tipo | Rango | Default | Notas |
|---|---|---|---|---|---|
| `inputTrim` | Input | float dB | -24..+24 | 0 | |
| `gateAmount` | Gate | float 0-100% | 0..100 | 0 | 0% = sin gate; a más %, threshold más profundo |
| `gateMode` | Gate Character | choice | Fast / Med / Slow | Fast | velocidad de release |
| `driveAmount` | Drive | float 0-100% | 0..100 | 0 | pre-gain hacia el waveshaper + makeup |
| `driveMode` | Drive Character | choice | Tube / Fuzz / Boost | Tube | curvas de saturación distintas, no solo ganancia |
| `toneTilt` | Tone | float dB | -12..+12 | 0 | tilt EQ (corta graves/sube agudos o viceversa) |
| `toneMode` | Tone Character | choice | Scoop / Flat / Bright | Flat | pivote del tilt |
| `toneMidFreq` | Mid Sweep | knob, Hz | 300..3000 | 800 | frecuencia de un peak filter fijo (+4dB) |
| `cabAmount` | Cab | float 0-100% | 0..100 | 0 | mezcla de la coloración de gabinete |
| `cabMode` | Cab Character | choice | Combo / Stack / DI | Combo | DI ignora `cabAmount` (sin color de cab) |
| `modAmount` | Modulation | float 0-100% | 0..100 | 0 | |
| `modMode` | Modulation Character | choice | Chorus / Flanger / Phaser | Chorus | Chorus/Flanger = `juce::dsp::Chorus` con distinto centreDelay/feedback; Phaser = `juce::dsp::Phaser` |
| `modRate` | Mod Rate | knob, Hz | 0.1..5 | 0.8 | |
| `spaceAmount` | Space | float 0-100% | 0..100 | 0 | un solo reverb (no dos como Acoustic) |
| `spaceMode` | Space Character | choice | Room / Plate / Hall | Room | |
| `spacePreDelay` | Space Pre-Delay | knob, ms | 0..500 | 0 | delay real antes del reverb |
| `echoAmount` | Echo | float 0-100% | 0..100 | 0 | mismo módulo que Acoustic |
| `echoDivision` | Echo Division | choice | Slap / 8th / Quarter | Slap | tempo-synced |
| `echoFeedback` | Echo Feedback | knob, % | 0..100 | 35 | |
| `blend` | Blend | float % | 0..100 | 100 | |
| `outputTrim` | Output | float dB | -24..+24 | 0 | |
| `bypass` | Bypass | bool | — | off | se dibuja como el indicador "REC" |

**Tap Tempo** (`tapTempo()` + `manualBpm`): mismo patrón que Acoustic — método del
processor, no parámetro APVTS, para fallback de tempo sin host.

**Buses**: acepta mono→estéreo (DI/mic de amp) y estéreo→estéreo, igual que Acoustic.

## DSP: qué es real y qué es "de referencia"

Igual que Acoustic, esto es una implementación de REFERENCIA con clases built-in de
`juce::dsp` — suena a compresor/drive/reverb genéricos, no es la calidad final (esa
viene después con Airwindows u otro DSP curado). Detalle por módulo:

- **Gate**: `juce::dsp::NoiseGate` real, threshold desde `gateAmount`, attack/release
  desde `gateMode`.
- **Drive**: `juce::dsp::WaveShaper` con una curva distinta por `driveMode` (tanh
  simétrico para Tube, exponencial asimétrico para Fuzz, tanh suave para Boost) +
  makeup gain aproximado (no calibrado por oído, es una primera pasada).
- **Tone**: tilt EQ (low-shelf + high-shelf opuestos) vía `juce::dsp::IIR` + un peak
  filter fijo para el mid-sweep.
- **Cab**: 2-3 `juce::dsp::IIR` en cadena (corte grave, corte agudo, presencia) con
  frecuencias fijas por `cabMode` — **no es un simulador de gabinete real** (eso sería
  convolución con impulse responses, mucho más pesado); es una aproximación con EQ.
- **Modulation**: `juce::dsp::Chorus` (Chorus/Flanger, cambiando centreDelay/feedback)
  o `juce::dsp::Phaser` (Phaser) según `modMode`.
- **Space / Echo**: mismo código que Acoustic (`juce::dsp::Reverb` + pre-delay real;
  `DelayLine` con feedback tempo-synced).
- Sin smoothing de parámetros salvo Input/Output trim (mismo trade-off documentado que
  Acoustic — mover controles rápido en reproducción puede sonar a zipper leve).

## Próximo paso

Con esto compilando y sonando, el resto es afinar por oído las curvas de Drive/Cab (hoy
son una primera pasada razonable, no calibrada contra un amp real) y eventualmente
reemplazar Drive/Cab/Gate por DSP curado (Airwindows tiene saturadores; un cab sim real
necesitaría impulse responses, que es un proyecto aparte).
