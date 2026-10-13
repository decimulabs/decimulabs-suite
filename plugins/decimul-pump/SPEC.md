# DecimuLabs Pump — Spec

Nombre de trabajo: **Pump** (a confirmar antes de publicar; "Duck" ya existe, es un
plugin de Devious Machines con el mismo concepto).

Bombeo de volumen sincronizado al tempo: un "sidechain de mentira" que no necesita
rutear ningún kick. Una **curva** de volumen, que dibujás vos o elegís de los presets,
se repite siguiendo el tiempo del DAW, o se dispara una vez por cada nota MIDI o por
cada golpe de kick detectado en el audio. Es la herramienta típica de EDM/house para que
bajos, pads y sintes "respiren" con el bombo.

Inspirado en plugins de la categoría: *Kickstart* (Nicky Romero y Cableguys), *Duck*
(Devious Machines), *Side Chainer* (HoRNet) y *OneKnob Pumper* (Waves). Implementación
propia con nombre, interfaz y código propios.

## Uso

1. Ponelo en la pista que querés que "respire" (bajo, pad, acordes, el bus de música).
2. **SYNC**: la curva sigue el tempo y la posición del DAW. Elegí el **Rate** (cuánto dura
   un ciclo: 1/4 = un beat, 1/8, 1/16, 1 compás…, con puntillos y tresillos).
3. La **curva**: arrastrá los puntos; arrastrá el rombo del medio de un tramo para
   curvarlo; doble clic en un lugar vacío agrega un punto; doble clic (o clic derecho)
   sobre un punto lo borra. Con `Shift`, los puntos se pegan a la grilla.
4. **DEPTH** es cuánto se hunde el volumen; **MIX** es dry/wet; **NUDGE** adelanta o atrasa
   la curva en milisegundos para que caiga justo con el kick.
5. **MIDI**: cada nota dispara la curva una sola vez (elegí cuál nota o "Any").
6. **AUDIO**: detecta el kick en el sidechain del plugin (o en la propia entrada si el
   sidechain no está activo) y dispara la curva con cada golpe.
7. **BANDA**: `Full` agacha todo; `Low` solo los graves (por debajo del crossover), así el
   bajo "respira" y los agudos del mismo sonido no; `High` al revés.

## Qué hace

| Pieza | Detalle |
|---|---|
| Curva | Poligonal de hasta 24 puntos con una curvatura por tramo. Tabla de 1024 entradas con interpolación lineal. 10 presets: Classic Pump, Hard Pump, Soft Pump, Tight Kick, Long Swell, Bounce, Smooth Sine, Trance Gate, Offbeat Duck, Reverse Pump. |
| Sync | Fase = posición en negras del host / duración del ciclo, así queda **anclada al compás** aunque saltes en el timeline o loopees. Con el transporte parado sigue un reloj interno al tempo (así se escucha mientras editás). Sin host (standalone) usa el parámetro `Tempo`. |
| Rate | 2 bars, 1 bar, 1/2, 1/2T, 1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16, 1/16T, 1/32. |
| Trigger MIDI | Disparo con precisión de muestra, filtro por nota o cualquiera. El ciclo corre una vez; entre disparos queda sin atenuar. |
| Trigger audio | Pasa-bajos (200 Hz, 2 polos) + seguidor de envolvente + umbral con histéresis (re-arma 6 dB abajo) y 60 ms de holdoff, para que un mismo kick no dispare dos veces. Usa el bus de sidechain si está activo. |
| Nudge | ±50 ms. En MIDI/Audio solo el lado positivo aplica (no se puede disparar antes del evento). |
| Smooth | 0–50 ms de suavizado de la ganancia, para que un corte brusco de la curva no haga click. |
| Stereo spread | Desfasa el canal R respecto del L hasta medio ciclo (bombeo ping-pong). Solo en Sync. |
| Banda | Full / Low / High con crossover Linkwitz-Riley 4º orden (40–400 Hz). |
| Depth | Mezcla entre la curva y "sin efecto": la curva realmente aplicada se dibuja punteada. |
| Estado | La curva a medida, el preset y todos los parámetros se guardan con el proyecto. |

## Parámetros (APVTS, automatizables)

| ID | Nombre UI | Tipo | Rango | Default |
|---|---|---|---|---|
| `rate` | Rate | choice | 13 duraciones | 1/4 |
| `depth` | Depth | float % | 0..100 | 100 |
| `mix` | Mix | float % | 0..100 | 100 |
| `smooth` | Smooth | float ms | 0..50 | 3 |
| `nudge` | Nudge | float ms | -50..+50 | 0 |
| `spread` | Stereo Spread | float % de ciclo | -50..+50 | 0 |
| `trigger` | Trigger | choice | Sync / MIDI / Audio | Sync |
| `midiNote` | MIDI Note | int | -1 (Any)..127 | -1 |
| `threshold` | Audio Threshold | float dB | -60..0 | -24 |
| `band` | Band | choice | Full / Low / High | Full |
| `crossover` | Crossover | float Hz | 40..400 | 150 |
| `bpm` | Tempo (sin host) | float BPM | 40..240 | 128 |
| `outputTrim` | Output | float dB | -24..+12 | 0 |
| `bypass` | Bypass | bool | — | off |

La curva en sí (los puntos) **no** es un parámetro automatizable: es parte del estado
del plugin. Para cambiar de forma durante una canción, usá dos instancias o la
automatización de `Depth`/`Rate`/`Mix`.

## Arquitectura

- **Audio thread**: lee una copia local de la tabla de la curva y la refresca con un
  try-lock solo cuando el message thread cambió la versión (nunca espera, nunca aloca).
- **Busses**: entrada principal estéreo, **sidechain estéreo opcional** (apagado por
  defecto), salida estéreo. MIDI de entrada para el trigger.
- Latencia: cero.

## Límites conocidos (honesto)

- Sidechain por audio detecta *golpes*, no es un compresor: no responde a la dinámica
  fina del kick, solo al instante en que cruza el umbral. Para bajos y bombos
  eso es lo que hace falta; para material muy denso conviene subir el umbral o usar MIDI.
- En MIDI/Audio no hay "ciclo" que se repita: si querés que siga el compás sin kick,
  usá Sync.
- Hosts que no entregan `ppqPosition` caen al reloj interno (Sync deja de estar anclado
  al compás).
- La curva no se automatiza (ver arriba).
- Validado con un test de integración (`tests/EngineTest.cpp`) con un host simulado:
  forma de la curva, anclaje al beat, rate, nudge, depth, mix, transporte parado, MIDI,
  audio, bandas, spread, estado y bypass. **Lo que ese test no cubre**: el plugin
  corriendo dentro de un DAW real, y cómo se comporta el bus de sidechain según el host.

## Ideas anotadas, todavía no construidas

- Curva automatizable (varias curvas y cambiar entre ellas).
- Humanize / swing del ciclo.
- Presets de usuario guardados en archivo.
- Visualización de la entrada/salida detrás de la curva.
- Lookahead (retrasar el audio para que la caída caiga *antes* del kick).

## Compilar y testear

```
cmake -S plugins/decimul-pump -B plugins/decimul-pump/build
cmake --build plugins/decimul-pump/build --config Release

# test de integración del motor (opcional)
cmake -S plugins/decimul-pump -B plugins/decimul-pump/build -DDECIMU_PUMP_TESTS=ON
cmake --build plugins/decimul-pump/build --config Release --target PumpEngineTest
plugins/decimul-pump/build/PumpEngineTest_artefacts/Release/PumpEngineTest.exe
# captura de la UI sin DAW
PumpEngineTest.exe --screenshot ruta.png
```
