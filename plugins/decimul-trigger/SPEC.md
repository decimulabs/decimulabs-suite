# DecimuLabs Trigger — Spec (nombre de trabajo)

Trigger de batería: detecta los golpes del audio de entrada, **mide su intensidad** y los
convierte en notas MIDI con velocity; opcionalmente reproduce muestras propias (capas
por velocity y round-robin) para reforzar o reemplazar el golpe original.

El nombre es provisorio ("Trigger"): se cambia en `CMakeLists.txt` (`PRODUCT_NAME`) y en
los textos de la UI.

Inspirado en la categoría de triggers/replacers de batería: *Retrig* (Isindi Sound),
*Slate Trigger*, *Drumagog*, *KTDrumTrigger* y los métodos de Reaper (ReaGate +
control de velocity, JSFX "Audio to MIDI Drum Trigger"). Implementación propia con
nombre, interfaz y código propios.

## Qué hace

```
audio (o sidechain) → mono → pasa-altos / pasa-bajos → detector de golpes
                                                         │
                       ┌─────────────────────────────────┤
                       ▼                                 ▼
          MIDI: nota + velocity                  reproductor propio (opcional)
                                                         │
audio original ──(retraso opcional)──► mezcla ◄──────────┘
```

| Pieza | Detalle |
|---|---|
| Detector | Dos seguidores de envolvente (rápido 0.05 ms / lento 8-60 ms). Hay golpe cuando el rápido pasa el umbral, supera al lento por +5 dB (un golpe sube de golpe, una cola no) y supera la **máscara**: el pico del golpe anterior decayendo con la constante `Retrigger`. Eso evita retriggers por la cola, el ring o el bleed, pero deja pasar un golpe nuevo más fuerte (rolls, flams). Además hay un bloqueo duro de `Retrigger` ms. |
| Intensidad | El pico de la señal de detección durante la ventana `Scan` (0.5–10 ms, por defecto 2) después del ataque. Es lo que da la velocity. |
| Velocity | `Threshold` (dB) = `Vel Min`, `Max Level` (dB) = `Vel Max`, con una curva (`Curva`: negativa = los suaves suenan más fuertes, positiva = más dinámica). Opción de velocity fija. |
| Filtros | `Low Cut` / `High Cut` de la señal de detección, para que el detector mire solo la banda del instrumento (ayuda con el bleed). `Oír detección` deja escuchar esa señal. |
| MIDI | Nota, canal y largo de nota configurables. El note-on sale en el instante en que termina la medición; el note-off a los `Largo nota` ms. |
| Sidechain | Si se activa el bus de sidechain del plugin en el DAW, la detección mira esa señal (para disparar desde otra pista); si no, mira la propia entrada. |
| Reproductor | 8 muestras (wav/aiff/flac/mp3/ogg, hasta 10 s), cada una con su rango de velocity. Entre las que incluyen la velocity del golpe, se turnan en round-robin (la que hace más tiempo que no suena). 16 voces. `Vel > Volumen` define cuánto sigue el volumen a la velocity. `Cortar anterior` apaga las voces previas con un fade de 4 ms. |
| Modos | **Solo MIDI** (el audio pasa como está), **Reforzar** (original + muestra, con `Original` como nivel del original) o **Reemplazar** (se silencia el original). |
| Alinear original | Retrasa el audio original lo que dura la medición y lo informa como latencia al host (PDC), así el original coincide con el MIDI y con la muestra (importante para reforzar sin que se "peine"). |
| Monitor | Cuatro segundos de historia del nivel con cada golpe marcado por una barra de su velocity. Las líneas de umbral y de nivel máximo se **arrastran con el mouse**. |

## Parámetros (APVTS, automatizables)

| ID | Nombre UI | Tipo | Rango | Default |
|---|---|---|---|---|
| `threshold` | Threshold | dB | -60..0 | -30 |
| `maxLevel` | Max Level | dB | -40..0 | -6 |
| `scan` | Scan | ms | 0.5..10 | 2 |
| `retrigger` | Retrigger | ms | 5..250 | 35 |
| `lowCut` / `highCut` | Low Cut / High Cut | Hz | 20..500 / 1k..20k | 60 / 14k |
| `listen` | Oír detección | bool | — | off |
| `minVel` / `maxVel` | Vel Min / Vel Max | int | 1..127 | 20 / 127 |
| `curve` | Curva | float | -1..+1 | 0 |
| `fixedVel` | Velocity fija | bool | — | off |
| `note` | Nota | int | 0..127 | 38 |
| `channel` | Canal | int | 1..16 | 10 |
| `noteLen` | Largo nota | ms | 5..500 | 40 |
| `sampler` | Reproductor | choice | Off / Reinforce / Replace | Off |
| `sampleLevel` | Muestras | dB | -24..+12 | 0 |
| `origLevel` | Original | dB | -60..0 | 0 |
| `velSens` | Vel > Volumen | % | 0..100 | 60 |
| `align` | Alinear original | bool | — | off |
| `choke` | Cortar anterior | bool | — | off |
| `outputTrim` | Output | dB | -24..+12 | 0 |
| `bypass` | Bypass | bool | — | off |

Las muestras (archivos y rangos de velocity) se guardan en el estado del plugin.

## Cómo se usa (Reaper)

1. Poné el plugin en la pista de la batería (o de un solo instrumento: caja, bombo…).
2. Mirá el monitor mientras tocás y arrastrá la línea **Threshold** hasta que quede justo
   arriba del ruido/bleed y debajo de los golpes más suaves. **Max Level** es el nivel
   del golpe más fuerte (el que quiere ser velocity 127).
3. **MIDI**: en la misma pista, a continuación del plugin, poné un sampler o un drum
   plugin: recibe las notas. O con sends de MIDI a otra pista.
4. **Sin otro plugin**: elegí Reforzar o Reemplazar, arrastrá muestras a las filas y
   repartilas por rango de velocity.

## Límites conocidos (honesto)

- **El MIDI sale después del ataque**: la intensidad se mide durante `Scan` ms, así que la
  nota llega ~2 ms después del golpe (el valor por defecto). Con `Alinear original` el
  audio se retrasa lo mismo y todo queda coincidente a costa de esa latencia.
- **El bleed no se resuelve solo**: los filtros Low/High Cut y el umbral ayudan, pero si
  el hi-hat entra fuerte en el micrófono de la caja, hay que subir el umbral. Detectar por
  "huella" (rechazar lo que no se parece al golpe buscado) es la próxima mejora.
- **Un solo canal de detección por instancia** (mono): una instancia por instrumento.
- **La salida MIDI de un plugin de audio** funciona en Reaper (pasa al plugin que sigue en
  la cadena o por sends); otros DAW (Ableton, FL) no la rutean igual. Para eso está el
  reproductor propio.
- La velocity depende de la calidad del audio de entrada: un micrófono con mucho bleed
  da velocities menos confiables que un disparador de pad.
- Sin tests dentro de un DAW real todavía; el motor se valida con
  `tests/EngineTest.cpp` (golpes sintéticos).

## Próximos pasos

- **v0.3 — huella del golpe**: modo "aprender" (guarda el espectro de unos golpes) para
  rechazar bleed por parecido.
- **v0.4 — captura**: grabar los golpes reales de tu batería y partirlos en muestras
  por velocity y round-robin, listas para el reproductor.
- Detección estéreo con selección de canal, y por banda (bombo + toms en la misma pista).

## Compilar y testear

```
cmake -S plugins/decimul-trigger -B plugins/decimul-trigger/build
cmake --build plugins/decimul-trigger/build --config Release

# test de integración del motor (opcional)
cmake -S plugins/decimul-trigger -B plugins/decimul-trigger/build -DDECIMU_TRIGGER_TESTS=ON
cmake --build plugins/decimul-trigger/build --config Release --target TriggerEngineTest
plugins/decimul-trigger/build/TriggerEngineTest_artefacts/Release/TriggerEngineTest.exe
# captura de la UI sin DAW
TriggerEngineTest.exe --screenshot ruta.png
```
