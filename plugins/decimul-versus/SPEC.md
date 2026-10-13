# DecimuLabs Versus (AB) — Spec

Comparador A/B de mezclas y masterings contra canciones de referencia. Va al final de
la cadena del master: **A** = tu mezcla pasando tal cual, **B** = una referencia que
suena en su lugar, con el **volumen igualado por LUFS** para que la comparación sea
justa (lo más fuerte siempre suena "mejor"). No procesa ni "mejora" tu audio: solo te
deja escuchar y medir.

Inspirado en la categoría de plugins de referencia A/B: *Reference* (Mastering The
Mix), *Metric AB* (ADPTR Audio) y *Magic AB* (Sample Magic). Es una implementación
propia con nombre, interfaz y código propios; el aporte diferencial está en la sección
"Valor agregado" más abajo.

Nombre del producto: `DecimuLabs Versus (AB)` — lleva "(AB)" y no "A/B" porque la
barra no es válida en el nombre de carpeta del `.vst3` en Windows. En la UI sí
se puede escribir "A/B".

## Uso

1. Arrastrá hasta 8 canciones (wav, aiff, flac, mp3, ogg, m4a/wma en Windows) sobre la
   ventana, o clic en un slot vacío. Clic derecho sobre un slot: reemplazar / quitar.
2. Poné la mezcla a sonar en el DAW. El plugin va último en el master.
3. **A** / **B** (o las teclas `A` y `B`) cambian entre tu mezcla y la referencia
   elegida. `1`-`8` eligen el slot.
4. La referencia suena cuando el DAW está en play (**HOST SYNC**, por defecto) o con el
   botón **PLAY** del plugin. Clic en la forma de onda = saltar; arrastrar = marcar un
   loop; doble clic = borrarlo.

## Qué hace

| Pieza | Detalle |
|---|---|
| Volumen igualado | LUFS integrado (BS.1770-4 / EBU R128, con gating) de la mezcla en vivo contra el de la referencia, calculado al cargarla. Ramp de 0.4s para que no haya saltos. `Match: Auto / Off` + `Trim` manual ±12 dB. Si el boost haría pasar los 0 dBFS, se limita por pico y la UI lo avisa. |
| Crossfade A/B | 15 ms, potencia constante. Cambiar de slot en caliente hace un crossfade de 20 ms entre referencias (sin clicks). |
| Memoria por slot | Cada slot recuerda su posición y su loop: dejás cada referencia parada en su estribillo y al cambiar de slot saltás directo ahí. |
| Espectro promedio | 30 bandas de 1/3 de octava, promedio de largo plazo (FFT 8192). Mezcla en vivo vs. referencia con el volumen igualado aplicado. `RESET PROMEDIO` reinicia el de la mezcla. |
| Curva de diferencia | Botón `DIFF`: mezcla − referencia por banda, ±12 dB. Barra hacia arriba = tu mezcla tiene más energía en esa banda. |
| Tabla de medidas | LUFS-I, LUFS-S (de la referencia, en la posición actual), pico, PLR (pico − LUFS-I, indicador de dinámica), ancho estéreo (% de energía lateral) y correlación. |
| Monitoreo | `Stereo / Mono / Side`, y solo `Low` (<150 Hz) / `Mid` / `High` (>2.5 kHz), con filtros Linkwitz-Riley de 4º orden. |
| Render offline | Si el host renderea/bouncea (modo no-realtime), el plugin pasa **siempre A**, aunque haya quedado en B. |
| Estado | Las rutas de las referencias, el loop de cada slot y todos los parámetros se guardan con el proyecto del DAW y se recargan solos al abrirlo. |

## Parámetros (APVTS, automatizables)

| ID | Nombre UI | Tipo | Rango | Default |
|---|---|---|---|---|
| `refActive` | A / B | bool | A (false) / B (true) | A |
| `slot` | Slot de referencia | int | 1..8 | 1 |
| `matchMode` | Match | choice | Auto / Off | Auto |
| `trim` | Trim ref | float dB | -12..+12 | 0 |
| `monitor` | Monitor | choice | Stereo / Mono / Side | Stereo |
| `band` | Banda | choice | Full / Low / Mid / High | Full |
| `followHost` | Host sync | bool | — | on |
| `loopEnabled` | Loop | bool | — | off |
| `outputTrim` | Output | float dB | -24..+12 | 0 |
| `bypass` | Bypass | bool | — | off |

Fuera del APVTS (a propósito, son gestos de UI): `PLAY`, la posición y la región de
loop de cada slot, y la lista de archivos (esto último sí se guarda en el estado).

## Arquitectura

- **Carga**: decodifica y analiza en un hilo de fondo (`juce::ThreadPool` de 1 hilo, en
  `ReferenceTrack::load`): lectura, resampleo (Lagrange) al sample rate del host,
  loudness integrado + curva de short-term cada 100 ms, pico, ancho, correlación,
  espectro promedio y picos de la forma de onda. El audio thread solo reproduce por
  índice entero y **nunca libera memoria**: el intercambio de slot se hace bajo un
  `SpinLock` y el audio thread usa try-lock (si no lo consigue, ese bloque suena A).
- **Si cambia el sample rate del host**, todas las referencias se recargan solas.
- **Análisis de la mezcla** en el audio thread (`LoudnessAnalyzer`, `BandAverager`),
  publicado a la UI por atómicos cada 250 ms.
- Latencia: cero.

## Límites conocidos (honesto)

- **Memoria**: cada referencia se guarda decodificada en float estéreo al rate del
  host: ~115 MB para 5 min a 48 kHz, o sea hasta ~1 GB con los 8 slots llenos. Se
  recorta a 15 min por archivo. Si hace falta, se puede pasar a streaming desde disco.
- **Resampleo** Lagrange al cargar: bueno para escuchar y comparar, no es calidad de
  mastering. Si la referencia ya está al rate del proyecto no se toca.
- **Pico** = sample peak, no true peak (no hay oversampling en el medidor).
- **Espectro** = promedio de largo plazo, no un analizador en tiempo real. Los dB son
  relativos (no una calibración absoluta de dBFS): lo que vale es la diferencia entre
  mezcla y referencia, que pasa por el mismo camino.
- Las dos medidas "reales" que esto necesita se validaron con un test de integración
  (`tests/EngineTest.cpp`): LUFS contra valores conocidos, igualado A/B exacto,
  crossfade sin saltos, restauración de estado.

## Valor agregado sobre las herramientas de la categoría

Construido: igualado por LUFS con tope de pico, memoria de posición/loop por slot,
curva de diferencia por banda, escucha por banda y mid/side, render offline seguro,
**gratis y de código abierto (GPLv3)**, y comparar *tus propias versiones*: cargá los
bounces v1, v2, v3 de tu mezcla como si fueran referencias y alternás entre ellos con
el volumen igualado (sirve para saber si la v3 "suena mejor" o solo "suena más fuerte").

Ideas anotadas, todavía **no** construidas:
- **Modo ciego (ABX)**: esconde cuál es cuál y da estadística de aciertos.
- **Alineación automática de secciones** (estribillo con estribillo).
- Analizador de espectro en tiempo real, true peak, LRA.
- Streaming desde disco para no cargar todo en memoria.
- Integración con Sala (decimulabs-mixtool) para versiones de una mezcla.

## Compilar y testear

```
cmake -S plugins/decimul-versus -B plugins/decimul-versus/build
cmake --build plugins/decimul-versus/build --config Release

# test de integración del motor (opcional)
cmake -S plugins/decimul-versus -B plugins/decimul-versus/build -DDECIMU_VERSUS_TESTS=ON
cmake --build plugins/decimul-versus/build --config Release --target VersusEngineTest
plugins/decimul-versus/build/VersusEngineTest_artefacts/Release/VersusEngineTest.exe
```
