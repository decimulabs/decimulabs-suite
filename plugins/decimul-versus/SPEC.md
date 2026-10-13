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
   elegida. `1`-`8` eligen el slot (lado B).
4. La referencia suena cuando el DAW está en play (**HOST SYNC**, por defecto) o con el
   botón **PLAY** del plugin. Clic en la forma de onda = saltar; arrastrar = marcar un
   loop; doble clic = borrarlo.
5. **ALINEAR SECCION**: con la mezcla sonando unos 5 segundos, salta en la referencia al
   tramo que más se le parece (ver "Alineación" abajo).

## Qué hace

| Pieza | Detalle |
|---|---|
| Volumen igualado | LUFS integrado (BS.1770-4 / EBU R128, con gating) de la mezcla en vivo contra el de la referencia, calculado al cargarla. Ramp de 0.4s para que no haya saltos. `Match: Auto / Off` + `Trim` manual ±12 dB sobre B. Si el boost haría pasar el **true peak** de 0 dBFS, se limita y la UI lo avisa. |
| Crossfade A/B | 15 ms, potencia constante. Cambiar de slot en caliente hace un crossfade de 20 ms entre referencias; cambiar la fuente A hace un declick de 10 ms (sin clicks). |
| Fuente del lado A | `A = Mezcla del DAW` (por defecto) o cualquier slot. Con A y B como slots, los dos comparten el cabezal y se igualan al nivel del más bajo (nunca se sube): sirve para comparar dos **versiones** de un mismo tema, p. ej. tus bounces v1 y v2. |
| Test ciego ABX | `TEST CIEGO ABX`: aparece `X`, que es A o B según un sorteo que el plugin no muestra. Escuchás A, B y X, contestás `X ES A` / `X ES B`, y el plugin lleva la cuenta y el valor p (binomial, P(≥ aciertos por azar)). Con 8+ intentos y p < 0.05 avisa que la diferencia se nota. Va siempre con el volumen igualado, que es lo que lo hace válido. |
| Alineación de secciones | Compara el espectro (30 bandas, un frame cada ~85 ms) y la evolución de nivel de los últimos ~8 s de la mezcla contra toda la referencia, y salta al final del tramo más parecido. Reporta una confianza alta / media / baja. **Es una heurística de perfil sonoro**, no detecta estructura musical: funciona bien para "el estribillo con el estribillo" y mal si las dos canciones no se parecen. |
| Memoria por slot | Cada slot recuerda su posición y su loop: dejás cada referencia parada en su estribillo y al cambiar de slot saltás directo ahí. |
| Espectro | 30 bandas de 1/3 de octava. **Promedio** de largo plazo (FFT 8192) de A vs. B con el volumen igualado aplicado; **Tiempo real** (un frame cada ~85 ms, con caída suave); **Diferencia** por banda, ±12 dB (barra hacia arriba = A tiene más energía en esa banda). `RESET PROMEDIO` reinicia el promedio de la mezcla. |
| Tabla de medidas | LUFS-I, LUFS-S (de la referencia, en la posición actual), **true peak** (dBTP, 4x sobremuestreado), PLR (true peak − LUFS-I, indicador de dinámica), **LRA** (rango de loudness, EBU 3342), ancho estéreo (% de energía lateral) y correlación. |
| Monitoreo | `Stereo / Mono / Side`, y solo `Low` (<150 Hz) / `Mid` / `High` (>2.5 kHz), con filtros Linkwitz-Riley de 4º orden. |
| Render offline | Si el host renderea/bouncea (modo no-realtime), el plugin pasa **siempre tu mezcla**, aunque haya quedado en B, en ABX o con A apuntando a un slot. |
| Memoria RAM | El audio decodificado solo queda residente para los slots en uso (el B, el A y los más recientes, hasta 4). El resto conserva el análisis completo (forma de onda, medidas, espectro) y se recarga solo en ~1 s al elegirlo. |
| Estado | Las rutas de las referencias, el loop de cada slot y todos los parámetros se guardan con el proyecto del DAW y se recargan solos al abrirlo. |

## Parámetros (APVTS, automatizables)

| ID | Nombre UI | Tipo | Rango | Default |
|---|---|---|---|---|
| `refActive` | A / B | bool | A (false) / B (true) | A |
| `slot` | Slot de referencia (lado B) | int | 1..8 | 1 |
| `sourceA` | Fuente del lado A | choice | Mix / Slot 1..8 | Mix |
| `abxMode` | Test ciego ABX | bool | — | off |
| `matchMode` | Match | choice | Auto / Off | Auto |
| `trim` | Trim B | float dB | -12..+12 | 0 |
| `monitor` | Monitor | choice | Stereo / Mono / Side | Stereo |
| `band` | Banda | choice | Full / Low / Mid / High | Full |
| `followHost` | Host sync | bool | — | on |
| `loopEnabled` | Loop | bool | — | off |
| `outputTrim` | Output | float dB | -24..+12 | 0 |
| `bypass` | Bypass | bool | — | off |

Fuera del APVTS (a propósito, son gestos de UI): `PLAY`, qué escuchás en ABX (A / B /
X), el sorteo y la cuenta de aciertos, la posición y la región de loop de cada slot, y
la lista de archivos (esto último sí se guarda en el estado).

## Arquitectura

- **Carga**: decodifica y analiza en un hilo de fondo (`juce::ThreadPool` de 1 hilo, en
  `ReferenceTrack::load`): lectura, resampleo (Lagrange) al sample rate del host,
  loudness integrado + LRA + curva de short-term cada 100 ms, pico y true peak, ancho,
  correlación, espectro promedio, espectro por frame y picos de la forma de onda. El
  audio thread solo reproduce por índice entero y **nunca libera memoria**: el
  intercambio de slot se hace bajo un `SpinLock` y el audio thread usa try-lock (si no
  lo consigue, ese bloque suena A).
- **Dos voces** con un cabezal compartido: lado A (mezcla o slot) y lado B (slot). El
  ABX usa las mismas dos voces.
- **Si cambia el sample rate del host**, todas las referencias se recargan solas.
- **Análisis de la mezcla** en el audio thread (`LoudnessAnalyzer`, `BandAverager`,
  `TruePeakMeter`), publicado a la UI por atómicos cada 250 ms; los frames de espectro
  alimentan el analizador en tiempo real y el anillo de la alineación.
- Latencia: cero.

## Límites conocidos (honesto)

- **Resampleo** Lagrange al cargar: bueno para escuchar y comparar, no es calidad de
  mastering. Si la referencia ya está al rate del proyecto no se toca.
- **Espectro en tiempo real** de 1/3 de octava: es resolución de analizador clásico
  (RTA), no un FFT fino. Los dB son relativos (no una calibración absoluta de dBFS): lo
  que vale es la diferencia entre A y B, que pasa por el mismo camino.
- **Alineación**: heurística por perfil sonoro (ver arriba). Para canciones muy
  distintas, o una mezcla muy plana, la confianza baja y conviene marcar a mano.
- **Recarga de slots sueltos**: elegir un slot que se había soltado de memoria tarda
  ~1 s (se avisa en la forma de onda); durante ese segundo suena A.
- **Mono/estéreo**: las referencias mono se duplican a estéreo; más de 2 canales usa
  los dos primeros.
- Se recorta a 15 min por archivo.
- Validado con un test de integración (`tests/EngineTest.cpp`): LUFS contra valores
  conocidos, true peak y LRA, igualado A/B y slot contra slot exactos, crossfades sin
  saltos, ABX, alineación sobre una canción sintética, residencia de memoria y
  restauración de estado. **Lo que ese test no cubre**: el plugin corriendo dentro de
  un DAW con audio de verdad.

## Valor agregado sobre las herramientas de la categoría

Gratis y de código abierto (GPLv3), y además:
- Igualado por LUFS con tope por true peak.
- **Comparar tus propias versiones** (slot contra slot, con el cabezal compartido).
- **Test ciego ABX** con valor p, incluido en el plugin.
- **Alinear secciones** automáticamente.
- Curva de **diferencia por banda** y analizador en tiempo real.
- Memoria por slot (posición y loop) y escucha por banda y mid/side.
- Render offline que nunca exporta la referencia por error.

Ideas anotadas, todavía **no** construidas:
- Integración con Sala (decimulabs-mixtool) para versiones de una mezcla.
- Alineación por estructura musical real (detección de secciones), en vez de perfil sonoro.
- Streaming real desde disco (hoy se resuelve soltando el audio de los slots sin uso).

## Compilar y testear

```
cmake -S plugins/decimul-versus -B plugins/decimul-versus/build
cmake --build plugins/decimul-versus/build --config Release

# test de integración del motor (opcional)
cmake -S plugins/decimul-versus -B plugins/decimul-versus/build -DDECIMU_VERSUS_TESTS=ON
cmake --build plugins/decimul-versus/build --config Release --target VersusEngineTest
plugins/decimul-versus/build/VersusEngineTest_artefacts/Release/VersusEngineTest.exe
```
