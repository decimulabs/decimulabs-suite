# DecimuLabs Flank — Spec (nombre de trabajo)

Ensanchador de voces. De una voz (en mono) saca **dos copias**, una un poco más grave
y otra un poco más aguda, con un retardo corto, y las **corre hacia los costados**: la
voz queda al medio y las copias la "flanquean". Suma un **ambiente** corto
(reflexiones tempranas) y una **diferencia de tiempo entre los lados** (efecto Haas),
que es el "espaciador". Un medidor de correlación y un chequeo en mono avisan si la
voz se cae al sumar.

El nombre es provisorio ("Flank"): se cambia en `CMakeLists.txt` (`PRODUCT_NAME`) y en
los textos de la UI.

Inspirado en los ensanchadores de voz por micro-pitch y en los dobles automáticos:
*MicroShift* (Soundtoys), *MicroPitch* (Eventide), *Doubler* (Waves) y *VoxDoubler*
(Sonnox). Implementación propia con nombre, interfaz y código propios.

## Uso

1. Ponelo en la pista de la voz (puede ser una pista mono: sale estéreo).
2. **Mix** y **Spread** primero: cuánto de las copias y cuánto se corren hacia los costados.
   Sutil suele andar mejor (Spread 40–60 %).
3. **Detune** (centésimos) y **Delay** (ms): lo clásico es 8–15 centésimos y 10–20 ms.
4. **Offset**: retardo extra de la copia derecha; separa un lado del otro sin tocar el centro.
5. **Low cut** saca los graves de lo que alimenta las copias (así el grave queda firme
   en el centro); **High cut** las oscurece para que no dupliquen sibilancias.
6. **Space**: un ambiente corto. **Mono check** suma L+R para ver si la voz se adelgaza.

## Qué hace

```
voz ──(L+R)/2──► pasa-altos ──┬──► copia L: detune - , retardo = Delay
                              ├──► copia R: detune + , retardo = Delay + Offset
                              └──► reflexiones tempranas (6 por lado + difusión)
   copias ► pasa-bajos ► pan (Spread) ┐
   seca (sin retardo, sin tocar) ──────┼──► Mix ──► Output ──► (Mono check)
   ambiente ► Space ───────────────────┘
```

| Pieza | Detalle |
|---|---|
| Copias | Salen de la suma a mono de la entrada, filtrada con el pasa-altos. Una baja y otra sube `Detune` centésimos. La señal seca no se retrasa ni se procesa: **latencia cero**. |
| Static | Desafinación fija: pitch shifter de línea de retardo (dos lecturas con ventana Hann y barrido en diente de sierra, lectura Hermite). Exacto en centésimos. |
| Drift | La desafinación va y viene alrededor del tono original: retardo modulado por dos LFO lentos e inconmensurables (0.83 y 1.7 Hz a la izquierda; 1.07 y 2.3 Hz a la derecha). `Detune` es el desvío máximo. Más suave, suena a doble de cinta. |
| Delay / Offset | Retardo de la copia izquierda (3–40 ms) y retardo extra de la derecha (0–25 ms, efecto Haas). |
| Spread | 0 % = las dos copias al centro; 100 % = una a cada lado. Ley de pan de potencia constante. |
| Mix | Potencia constante por canal: la seca baja (coseno) lo que suben las copias (seno), así el nivel casi no cambia. |
| Low cut / High cut | Pasa-altos de 2º orden sobre lo que entra a copias y ambiente; pasa-bajos de 2º orden sobre cada copia. |
| Space | Seis reflexiones tempranas por lado (5 a 42 ms, polaridades alternadas), dos allpass de difusión por lado y amortiguación en 6 kHz. Sin cola larga. |
| Mono check | Suma la salida a mono (con fundido de 20 ms) para escuchar si se cae. |
| Medidores | Goniómetro de la salida, posiciones de la voz y las copias, correlación (-1..+1) y cuánto baja el nivel al sumar L+R. |

Los dos modos de desafinación corren siempre y el modo es una mezcla entre ambos con
un fundido de 30 ms: cambiar de modo en caliente no hace saltos. Todos los parámetros
se suavizan (50 ms) para que mover un knob no haga zippers ni clicks.

## Parámetros (APVTS, automatizables)

| ID | Nombre UI | Tipo | Rango | Default |
|---|---|---|---|---|
| `detune` | Detune | float, centésimos | 0..50 | 10 |
| `delay` | Delay | float, ms | 3..40 | 14 |
| `offset` | Offset | float, ms | 0..25 | 4 |
| `spread` | Spread | float, % | 0..100 | 55 |
| `mix` | Mix | float, % | 0..100 | 50 |
| `lowCut` | Low Cut | float, Hz | 20..600 | 150 |
| `highCut` | High Cut | float, Hz | 2k..20k | 10k |
| `space` | Space | float, % | 0..100 | 20 |
| `mode` | Mode | choice | Static / Drift | Drift |
| `monoCheck` | Mono Check | bool | — | off |
| `outputTrim` | Output | float, dB | -24..+12 | 0 |
| `bypass` | Bypass | bool | — | off |

## Límites conocidos (honesto)

- **El detune por línea de retardo no es un pitch shifter de calidad.** Para desvíos
  chicos (hasta ~20 centésimos) es lo que hacían los pitch shifters de hardware y suena
  bien; con más desvío o con material muy tonal pueden aparecer una modulación lenta
  del color (las dos lecturas se combinan como un peine de filtros). El modo Drift lo
  evita porque usa una sola lectura.
- **En mono, las copias desafinadas suenan a chorus.** Es inherente al efecto. Para
  vigilarlo están la correlación, la caída en mono y el botón Mono check.
- **No se puede juzgar "a oído" desde el test**: el motor se valida con mediciones
  (centésimos exactos, retardos al muestreo, pan, nivel, cortes, correlación). Que suene
  natural es cosa de probarlo con voces reales.
- **Una sola voz por instancia**: las copias salen de la suma a mono de la entrada; una
  entrada estéreo se conserva en la seca pero las copias no son estéreo "real".
- Sin presets ni estilos todavía.
- No probado todavía dentro de un DAW real.

## Ideas anotadas, todavía no construidas

- *Humanize*: variación aleatoria del tiempo y del tono de cada copia.
- Más voces (4 copias, con su propio detune y pan) y estilos (preajustes).
- Ancho por bandas (más apertura en los agudos que en los graves).
- Un pitch shifter de verdad (síncrono con el período) para desvíos grandes.
- Presets de usuario.

## Compilar y testear

```
cmake -S plugins/decimul-flank -B plugins/decimul-flank/build
cmake --build plugins/decimul-flank/build --config Release

# test de integración del motor (opcional)
cmake -S plugins/decimul-flank -B plugins/decimul-flank/build -DDECIMU_FLANK_TESTS=ON
cmake --build plugins/decimul-flank/build --config Release --target FlankEngineTest
plugins/decimul-flank/build/FlankEngineTest_artefacts/Release/FlankEngineTest.exe
# captura de la UI sin DAW
FlankEngineTest.exe --screenshot ruta.png
```
