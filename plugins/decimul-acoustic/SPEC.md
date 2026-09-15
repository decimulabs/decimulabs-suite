# DecimuLabs Acoustic — Spec

Multiefecto "todo en uno" para voz e instrumentos acústicos (guitarra, piano, cuerdas,
nylon). Inspirado en la estructura del Waves CLA Unplugged (ver
[docs/research/cla-unplugged-analysis.md](../../docs/research/cla-unplugged-analysis.md)),
reinterpretado con nombres propios y funciones nuevas.

Estado: **spec cerrada, parámetros 100% cableados, y DSP de referencia funcionando**
(Tone/Dynamics/Space A+B/Echo/Blend con clases built-in de `juce::dsp` — no es todavía
la calidad final basada en Airwindows, ver limitaciones conocidas al inicio de
`PluginProcessor.h`). El editor hoy es el `juce::GenericAudioProcessorEditor` (perillas
automáticas) — falta el diseño visual custom.

## Cadena de señal

```
Input Trim ─► Tone (Low + High, con carácter) ─► Dynamics (compresor, con carácter)
   ─► Space A (reverb 1) ─┐
   ─► Space B (reverb 2) ─┼─► suma ─► Echo (delay tempo-sync) ─► Blend (dry/wet) ─► Output Trim
   (dry tap) ─────────────┘
```

Space A y Space B corren en paralelo alimentados por la señal post-Dynamics (como el
original: dos reverbs independientes, no en serie), se suman, y ahí entra el Echo.

## Parámetros (para `AudioProcessorValueTreeState`)

| ID | Nombre UI | Tipo | Rango | Default | Notas |
|---|---|---|---|---|---|
| `inputTrim` | Input | float dB | -24..+24 | 0 | con auto-gain sugerido (botón "Auto", ver valor agregado) |
| `lowGain` | Low | float dB | -12..+12 | 0 | bypass real en 0 |
| `lowMode` | Low Character | choice | Warm / Deep / Air-Cut | Warm | 3 filtros distintos, no solo ganancia |
| `highGain` | High | float dB | -12..+12 | 0 | bypass real en 0 |
| `highMode` | High Character | choice | Silk / Sheen / Crisp | Silk | |
| `compAmount` | Dynamics | float 0-100% | 0..100 | 0 | 0% = bypass real |
| `compMode` | Dynamics Character | choice | Glue / Snap / Squash | Glue | basado en Airwindows Pop!/Pulse/Volt |
| `compOversample` | Oversample x2/x4 | choice | Off / 2x / 4x | Off | valor agregado |
| `spaceAMix` | Space A | float 0-100% | 0..100 | 0 | |
| `spaceAType` | Space A Type | choice | Room / Hall / Cathedral | Room | de Airwindows Spaces |
| `spaceAPreDelay` | Space A Pre-Delay | float ms | 0-500ms | 0 | valor libre en ms |
| `spaceAPreDelaySync` | Space A Pre-Delay Sync | bool | on/off | off | on = usa división rítmica (tempo host o `manualBpm`) en vez de ms libres |
| `spaceBMix` | Space B | float 0-100% | 0..100 | 0 | |
| `spaceBType` | Space B Type | choice | Close / Wide / Vast | Close | |
| `spaceBPreDelay` | Space B Pre-Delay | float ms | 0-500ms | 0 | |
| `spaceBPreDelaySync` | Space B Pre-Delay Sync | bool | on/off | off | |
| `echoMix` | Echo | float 0-100% | 0..100 | 0 | |
| `echoDivision` | Echo Division | choice | Slap / 8th / Quarter | Slap | tempo-synced |
| `blend` | Blend (Dry/Wet) | float % | 0..100 | 100 | reemplaza el "Direct on/off" binario por mezcla continua |
| `outputTrim` | Output | float dB | -24..+24 | 0 | |
| `bypass` | Bypass general | bool | — | off | |

Meters (no automatizables, solo lectura para la UI): input peak, output peak, gain
reduction del compresor, correlación estéreo post Space A+B.

**Tap Tempo** (`tapTempo()` + `manualBpm`) queda afuera del APVTS a propósito: es un
gesto transitorio de UI (click de un botón), no un valor con sentido para automatizar
desde una DAW. Vive como método público en `DecimulAcousticProcessor` que la UI llama en
cada click; el BPM calculado se guarda igual en el estado del plugin (XML) para que
sobreviva a cerrar/abrir la sesión.

**Buses de entrada**: acepta mono→estéreo (mic/DI directo) además de estéreo→estéreo,
igual que los dos componentes del CLA Unplugged original — ver `isBusesLayoutSupported`.

## Valor agregado sobre el original (para que no sea "una copia")

1. **Bypass real** en cada módulo cuando su mix/ganancia está en 0 (el original aplica
   procesamiento fijo incluso en 0).
2. **Blend continuo dry/wet** (0-100%) en vez de un simple on/off de "Direct".
3. **Auto-gain**: botón que analiza picos de los últimos ~5s y sugiere `inputTrim`.
4. **Tap tempo** manual para cuando corre standalone sin BPM de host.
5. **Oversampling opcional** (2x/4x) en Dynamics para reducir aliasing en material con
   armónicos altos (voz, nylon).
6. **Overlay de espectro** en tiempo real detrás de los controles de Tone (feedback visual
   de qué está haciendo el EQ).
7. **Medidor de correlación estéreo** post reverbs (evita sorpresas de fase en mono).
8. **UI vectorial escalable** (HiDPI) con tema claro/oscuro, en vez de skin bitmap fijo.
9. **Presets de fábrica por instrumento** (Vocal, Guitarra Acústica, Piano, Cuerdas, Nylon)
   con import/export de `.xml`, pensado para compartir presets en la comunidad
   (donationware = ecosistema abierto).
10. Licencia **GPLv3**, código fuente completo público.

## Próximo paso

Con este spec + los parámetros ya definidos en `PluginProcessor` (ver esqueleto en
`Source/`), lo que falta es puramente **UI**: layout de los 9 módulos, estilo visual
(paleta/branding de decimulabs), y componentes custom (faders, selectores de
carácter tipo segmented-button, meters).
