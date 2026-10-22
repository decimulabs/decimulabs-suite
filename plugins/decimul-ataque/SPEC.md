# DecimuLabs Ataque — Spec (nombre de trabajo)

Modulador de transientes: más o menos **pegada** (ATTACK) y más o menos **cola** (SUSTAIN) de
baterías, guitarras acústicas, bajos y percusión. Sin latencia, con los canales ligados (la imagen
estéreo no se mueve) y con el mismo efecto sin importar qué tan fuerte sea el golpe.

El nombre es provisorio ("Ataque"): se cambia en `CMakeLists.txt` (`PRODUCT_NAME`) y en los textos de la UI.

Inspirado en moduladores de transientes como *Transient Designer* (SPL), *Transient Master* (Native
Instruments) y *Smack Attack* (Waves) — nombres verificados con una búsqueda web el 22/10/2026.
Implementación propia con nombre, interfaz y código propios.

## Uso

1. Ponelo en la pista (mono o estéreo; la salida tiene el mismo formato que la entrada).
2. **ATTACK** `+`: cada golpe pega más. `-`: el golpe se redondea (al -100 % se aplana por completo el comienzo).
3. **SUSTAIN** `+`: la cola después del golpe se alarga (más sala, más resonancia). `-`: queda más seca y corta.
4. **SPEED**: qué tan rápido distingue el ataque de la cola. Más alto = golpes cortos y secos; más bajo =
   más suave (bajo, material lento). 50 % va bien para casi todo.
5. La pantalla muestra el nivel y, abajo, la ganancia que está aplicando en cada momento (hacia arriba
   en el color de acento, hacia abajo en rojo).

## Cómo funciona (`Source/TransientDsp.h`)

```
entrada ► nivel = el mayor de L y R ► potencia
            ├─ envolvente RÁPIDA (ataque 0.5 ms, caída 16 ms)
            └─ envolvente LENTA  (ataque 25 ms, caída 180 ms)
 diferencia (dB) = rápida - lenta
    > 0  (arranca un golpe)      -> ganancia = ATTACK  x diferencia
    < 0  (el golpe se apaga)     -> ganancia = SUSTAIN x (-diferencia)
 se limita a +/-12 dB, se anula por debajo de -75 dBFS, se suaviza ~0.8 ms y se aplica igual a L y R
```

- Como es una diferencia en dB, **no depende del nivel**: golpes de -6 dBFS y de -30 dBFS reciben el
  mismo cambio (medido en el test: 8.2 dB contra 8.2 dB).
- ATTACK -100 % lleva el arranque al nivel de la envolvente lenta (el golpe sale sin transiente);
  SUSTAIN +100 % mantiene la cola cerca del nivel de la envolvente lenta.
- **SPEED** multiplica todos los tiempos (de 0.5x a 2x).
- Cero latencia (no mira hacia adelante): los primeros ~1-2 ms de un golpe se procesan algo menos que el resto.

## Parámetros (APVTS)

| ID | Nombre | Rango | Default |
|---|---|---|---|
| `attack` | Attack | -100..+100 % | 0 |
| `sustain` | Sustain | -100..+100 % | 0 |
| `speed` | Speed | 0..100 % | 50 |
| `outputTrim` | Output | -24..+12 dB | 0 |
| `bypass` | Bypass | on/off | off |

Con Attack y Sustain en 0 la señal sale **idéntica, bit a bit** (comprobado en el test).

## Verificado (`tests/EngineTest.cpp`, `AtaqueEngineTest.exe`)

- Con un golpe de ruido y uno de 180 Hz: Attack +100 sube el ataque unos 8 dB, Attack -100 lo baja unos 7 dB;
  Sustain +100 sube la cola unos 8 dB, Sustain -100 la baja unos 6-7 dB; cada perilla no toca la zona de la otra.
- Mismo efecto con golpes suaves y fuertes; Speed cambia el resultado; los canales van ligados (el canal suave que
  solo no se tocaría recibe la ganancia del que pega).
- Tono continuo (40 a 1000 Hz) con Attack y Sustain al 100 %: la ganancia se mueve menos de 0.8 dB (graves
  incluidos; con Speed al 100 % hasta 1.4 dB). Por debajo de -75 dBFS no levanta el ruido.
- 8 s de ruido fuerte con parámetros al azar: sin NaN y la ganancia nunca pasa de +12 dB.
- Procesador: sin latencia, bypass intacto, Output exacto, mono, estado, cuadros para la pantalla.
- **No probado por oído ni en un DAW** (lo juzga Marcelo).

## Límites conocidos (honesto)

- Sin anticipación: el primer par de milisegundos de un golpe se procesa algo menos.
- SUSTAIN actúa sobre colas de menos de ~medio segundo; en colas muy largas (platos, reverbs) casi no hace efecto
  porque las dos envolventes ya se igualan.
- Un solo detector para todo el espectro (no hay bandas): si el bombo y la caja suenan juntos, la caja puede
  mover el golpe del bombo.

## Ideas no hechas

Modo por bandas (graves y agudos por separado), anticipación opcional, escuchar solo la diferencia, presets.

## Compilar y testear

```
cmake -S plugins/decimul-ataque -B plugins/decimul-ataque/build -DDECIMU_ATAQUE_TESTS=ON
cmake --build plugins/decimul-ataque/build --config Release
plugins/decimul-ataque/build/AtaqueEngineTest_artefacts/Release/AtaqueEngineTest.exe        # debe decir TODO OK
... AtaqueEngineTest.exe --screenshot ruta.png        # captura de la UI
```
