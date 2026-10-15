# DecimuLabs Trigger — Spec (nombre de trabajo)

Trigger de batería: detecta los golpes del audio de entrada, **mide su intensidad** y los
convierte en notas MIDI con velocity; opcionalmente reproduce muestras propias (capas
por velocity y round-robin) para reforzar o reemplazar el golpe original. Puede
**rechazar el bleed** por parecido con una huella aprendida, **grabar tus golpes
reales** para armar las muestras, **calibrarse solo** escuchando unos segundos, tocar
el reproductor con **MIDI de entrada** y **exportar los golpes como archivo .mid**.

El nombre es provisorio ("Trigger"): se cambia en `CMakeLists.txt` (`PRODUCT_NAME`) y en
los textos de la UI.

Inspirado en la categoría de triggers/replacers de batería: *Retrig* y *Remiq* (Isindi
Sound), *Slate Trigger*, *Drumagog*, *KTDrumTrigger* y los métodos de Reaper (ReaGate +
control de velocity, JSFX "Audio to MIDI Drum Trigger"). Implementación propia con
nombre, interfaz y código propios.

## Versiones

| | Qué suma |
|---|---|
| **v0.1** | Audio → MIDI con velocity: detector, intensidad medida en una ventana de scan, curva, umbral, anti-rebote, filtros de detección, sidechain, monitor de golpes. |
| **v0.2** | Reproductor propio: 8 muestras con rango de velocity, round-robin, modos Reforzar/Reemplazar, cortar anterior y alinear el original (latencia informada al host). |
| **v0.3** | **Huella del golpe**: aprende la forma espectral del instrumento y rechaza los golpes que no se parecen (bleed). |
| **v0.4** | **Captura**: graba cada golpe aceptado y arma muestras repartidas en capas por velocity y alternancias, ya cargadas en el reproductor. |
| **v0.5** | **Ajuste fino y MIDI**: sensibilidad del ataque, botón *Calibrar*, polaridad y desfase de las muestras contra el original, ganancia y afinación por muestra, MIDI de entrada que toca el reproductor y exportación de los golpes a .mid. |

## Qué hace

```
audio (o sidechain) → mono → pasa-altos / pasa-bajos → detector de golpes ──┐
                                   │                                         │
                                   └── banco de 12 bandas (huella) ──────────┤ ¿se parece?
                                                                             ▼
                          MIDI: nota + velocity    reproductor propio    captura de tomas
                                                         │
audio original ──(retraso opcional)──► mezcla ◄──────────┘
```

| Pieza | Detalle |
|---|---|
| Detector | Dos seguidores de envolvente (rápido 0.05 ms / lento 8-60 ms). Hay golpe cuando el rápido pasa el umbral, supera al lento por `Ataque` (5.1 dB por defecto: un golpe sube de golpe, una cola no) y supera la **máscara**: el pico del golpe anterior decayendo con la constante `Retrigger`. Eso evita retriggers por la cola, el ring o el bleed, pero deja pasar un golpe nuevo más fuerte (rolls, flams). Además hay un bloqueo duro de `Retrigger` ms. |
| Intensidad | El pico de la señal de detección durante la ventana `Scan` (0.5–10 ms, por defecto 2) después del ataque. Es lo que da la velocity. |
| Velocity | `Threshold` (dB) = `Vel Min`, `Max Level` (dB) = `Vel Max`, con una curva (`Curva`: negativa = los suaves suenan más fuertes, positiva = más dinámica). Opción de velocity fija. |
| **Ataque** | Cuánto tiene que subir de golpe el nivel (en dB, 1–12) sobre lo que venía sonando para contar como golpe nuevo. Más bajo = detecta golpes suaves o pegados a una cola (con más falsos); más alto = solo ataques secos. Con un tono sostenido, por debajo de ~3 dB el propio tono se retriguerea. |
| **Calibrar** | Escucha 6 s con un detector propio (umbral a -60 dB, así encuentra los golpes aunque el umbral actual esté mal puesto) y ajusta **solo** `Threshold` y `Max Level`: *Max Level* = el golpe más fuerte que oyó menos 1 dB; *Threshold* = 9 dB sobre el piso de ruido (percentil 10 de los picos por cuadro de 5 ms), sin pasar de 36 dB bajo el Max Level ni de 6 dB bajo él. Si no oye golpes que se despeguen al menos 12 dB del piso (por ejemplo, solo ruido), no toca nada. En el monitor se ve un cartel con el avance; se puede cancelar. |
| Filtros | `Low Cut` / `High Cut` de la señal de detección, para que el detector mire solo la banda del instrumento (ayuda con el bleed). `Oír detección` deja escuchar esa señal. |
| **Huella** | Banco de 12 filtros pasa-banda (90 Hz - 11 kHz, geométricos). En la ventana de análisis que sigue al ataque acumula la energía de cada banda y la convierte en un vector unitario de forma espectral (energía^0.25 por banda, para que las bandas débiles pesen). La comparación con la huella aprendida es el coseno: 1 = misma forma. **Aprender** promedia los golpes que lleguen (hasta 24; se cierra a mano o sola); **Rechazar bleed** deja pasar solo los que superan `Parecido mín.` (por defecto 0.80). Como el vector se normaliza, un golpe suave del mismo instrumento pasa igual: cuenta la forma, no el volumen. Los rechazados se ven en gris en el monitor y se cuentan. |
| Ventana de la huella | `Ventana` (4–30 ms, por defecto 12). Con el rechazo activado (o aprendiendo) el golpe se decide recién cuando termina esa ventana, así que el MIDI sale ese tiempo después del ataque; con `Alinear original` el audio se retrasa lo mismo y la latencia se informa al host. **Si cambiás la ventana, volvé a aprender la huella.** |
| MIDI | Nota, canal y largo de nota configurables. El note-on sale en el instante en que termina la decisión; el note-off a los `Largo nota` ms. |
| Sidechain | Si se activa el bus de sidechain del plugin en el DAW, la detección mira esa señal (para disparar desde otra pista); si no, mira la propia entrada. |
| Reproductor | 8 muestras (wav/aiff/flac/mp3/ogg, hasta 10 s), cada una con su rango de velocity. Entre las que incluyen la velocity del golpe, se turnan en round-robin (la que hace más tiempo que no suena). 16 voces. `Vel > Volumen` define cuánto sigue el volumen a la velocity. `Cortar anterior` apaga las voces previas con un fade de 4 ms. |
| Polaridad y desfase | `Invertir fase` da vuelta la polaridad de **todas** las muestras (para cuando refuerzan al original y se anulan). `Desfase` (-10..+10 ms): positivo atrasa la muestra; negativo **se salta el comienzo** de la muestra (no se puede sonar antes de la decisión). Las muestras que crea la captura traen 3 ms de pre-roll: con *Alinear original* y desfase -3 ms el ataque de la muestra cae donde estaba el golpe. |
| Ganancia y afinación | Cada una de las 8 muestras tiene `Gain` (-24..+12 dB) y `Tune` (-12..+12 semitonos, interpolación cúbica). Se escriben con doble click en la fila. Afinar cambia el largo de la muestra (como un sampler común). |
| **MIDI de entrada** | `MIDI in` rota entre **NO** (se ignora, como antes), **NOTA** (solo la nota configurada, para mandar un clip con toda la batería a varias instancias) y **TODAS**. Cada nota toca el reproductor en la muestra exacta del evento, con su velocity (capas, round-robin, ganancia, afinación y desfase valen igual). Necesita *Reforzar* o *Reemplazar*; las notas que toca se consumen y **todo lo demás pasa de largo** (con MIDI in en NO, o el reproductor apagado, no se toca nada). |
| Modos | **Solo MIDI** (el audio pasa como está), **Reforzar** (original + muestra, con `Original` como nivel del original) o **Reemplazar** (se silencia el original). |
| Alinear original | Retrasa el audio original lo que dura la decisión y lo informa como latencia al host (PDC), así el original coincide con el MIDI y con la muestra (importante para reforzar sin que se "peine"). |
| **Captura** | Con `Grabar golpes` activado, cada golpe aceptado (pasó el umbral y, si está activada, la huella) se guarda con 3 ms de lo que había antes del ataque, hasta `Largo` (100–1000 ms), cortado donde empieza el golpe siguiente o cuando la señal cae a -66 dBFS por 40 ms. Hasta 64 tomas por sesión. |
| **Crear muestras** | Ordena las tomas por velocity, las reparte en `Capas` (1–4) por cuantiles, elige en cada capa las `Alternan.` (1–4) más cercanas a la mediana (capas × alternancias ≤ 8), recorta la cola de silencio, aplica fades de 1 ms y 5 ms y las escribe como WAV de 24 bits en `Documentos/DecimuLabs Trigger/<Nombre>_<fecha>/` (`Nombre_L2_RR1_v87.wav`). Las carga en el reproductor con rangos de velocity que cubren 1–127 sin huecos ni solapes y pasa a modo Reemplazar. |
| **MIDI grabado** | `Grabar golpes` también anota **dónde** cayó cada golpe aceptado (hasta 16384): en el tiempo del proyecto si el host manda posición, o con un reloj propio desde la posición del cursor si el transporte está parado o no hay host. La posición es la del **ataque**, no la de la decisión. `Exportar MIDI` escribe un .mid (960 ppq, un tempo, un compás) en `Documentos/DecimuLabs Trigger/`, que empieza al comienzo del compás del primer golpe: el mensaje dice en qué compás soltarlo. Las notas duran `Largo nota` (mínimo una fusa) y se cortan antes del golpe siguiente de la misma altura. |
| Monitor | Cuatro segundos de historia del nivel con cada golpe marcado por una barra de su velocity (gris y tachado si lo rechazó la huella). Las líneas de umbral y de nivel máximo se **arrastran con el mouse**. |

### Sacar el MIDI de una pista grabada

1. Poné el plugin en la pista de la caja, ajustá `Threshold` o apretá **Calibrar** y tocá
   (o reproducí) unos segundos de suave a fuerte.
2. **Grabar golpes**, reproducí la canción desde el compás que quieras y **Parar**.
3. **Exportar MIDI**: arrastrá el .mid al DAW y soltalo en el compás que dice el mensaje.
   Si el plugin va a otra pista, ese .mid dispara cualquier sampler.

### Flujo típico para armar muestras desde una pista grabada

1. Poné el plugin en la pista de la batería (o en la de la caja) y mirá el monitor:
   arrastrá `Threshold` justo sobre el ruido.
2. **Aprender** la caja: dejá que suenen unos 8–12 golpes solos y cerrá.
3. **Rechazar bleed** activado: en el monitor se ven en gris los golpes de hi-hat y bombo.
4. **Grabar golpes** y reproducí la canción entera.
5. **Crear muestras**: ya quedan armadas, cargadas y listas para Reforzar/Reemplazar.

## Parámetros (APVTS, automatizables)

| ID | Nombre UI | Tipo | Rango | Default |
|---|---|---|---|---|
| `threshold` | Threshold | dB | -60..0 | -30 |
| `maxLevel` | Max Level | dB | -40..0 | -6 |
| `scan` | Scan | ms | 0.5..10 | 2 |
| `retrigger` | Retrigger | ms | 5..250 | 35 |
| `lowCut` / `highCut` | Low Cut / High Cut | Hz | 20..500 / 1k..20k | 60 / 14k |
| `listen` | Oír detección | bool | — | off |
| `onset` | Ataque | dB | 1..12 | 5.1 |
| `fingerprint` | Rechazar bleed | bool | — | off |
| `match` | Parecido mín. | float | 0.30..0.99 | 0.80 |
| `fpWindow` | Ventana | ms | 4..30 | 12 |
| `minVel` / `maxVel` | Vel Min / Vel Max | int | 1..127 | 20 / 127 |
| `curve` | Curva | float | -1..+1 | 0 |
| `fixedVel` | Velocity fija | bool | — | off |
| `note` | Nota | int | 0..127 | 38 |
| `channel` | Canal | int | 1..16 | 10 |
| `noteLen` | Largo nota | ms | 5..500 | 40 |
| `midiIn` | MIDI in | choice | Off / Note / Any | Off |
| `sampler` | Reproductor | choice | Off / Reinforce / Replace | Off |
| `sampleLevel` | Muestras | dB | -24..+12 | 0 |
| `origLevel` | Original | dB | -60..0 | 0 |
| `velSens` | Vel > Volumen | % | 0..100 | 60 |
| `align` | Alinear original | bool | — | off |
| `choke` | Cortar anterior | bool | — | off |
| `polarity` | Invertir fase | bool | — | off |
| `sampleOffset` | Desfase | ms | -10..+10 | 0 |
| `capLength` | Largo (captura) | ms | 100..1000 | 500 |
| `capLayers` | Capas | int | 1..4 | 3 |
| `capRR` | Alternan. | int | 1..4 | 2 |
| `outputTrim` | Output | dB | -24..+12 | 0 |
| `bypass` | Bypass | bool | — | off |

Fuera del APVTS: las muestras (archivos, rangos de velocity, ganancia y afinación) y la
huella aprendida, que se guardan en el estado del plugin; y las tomas de audio y los
golpes MIDI grabados, que viven solo en memoria (las muestras creadas y el .mid quedan en
la carpeta).

## Límites conocidos (honesto)

- **El MIDI sale después del ataque**: la intensidad se mide durante `Scan` ms (2 ms por
  defecto); con el rechazo de bleed activado, durante la ventana de la huella (12 ms por
  defecto). Con `Alinear original` el audio se retrasa lo mismo y todo queda coincidente
  a costa de esa latencia.
- **La huella distingue instrumentos de forma espectral distinta**, no golpes parecidos:
  caja contra hi-hat o contra bombo sí; caja contra un tom de timbre similar, no. Para
  eso hay que subir el umbral o mandar cada instrumento a su instancia.
- **Un solo canal de detección por instancia** (mono): una instancia por instrumento.
- **La salida MIDI de un plugin de audio** funciona en Reaper (pasa al plugin que sigue en
  la cadena o por sends); otros DAW (Ableton, FL) no la rutean igual. Para eso está el
  reproductor propio.
- **La captura** guarda lo que entra: con bleed fuerte detrás del golpe, las muestras
  lo llevan. La huella ayuda a no grabar golpes de otro instrumento, pero no limpia el
  bleed que acompaña al golpe correcto.
- La velocity depende de la calidad del audio de entrada: un micrófono con mucho bleed
  da velocities menos confiables que un disparador de pad.
- **Calibrar** asume que tocás con pausas entre golpes y de suave a fuerte; en un roll
  continuo el "piso" sale más alto de lo real. No separa bleed de golpes suaves (para eso
  está la huella) y no sabe cuál es el golpe más suave que querés conservar: el umbral es
  conservador (36 dB bajo el máximo como mucho), se retoca a mano en el monitor.
- **El desfase negativo recorta** el principio de la muestra: sirve para saltar silencio o
  pre-roll, no para adelantar un ataque que está pegado al comienzo del archivo.
- **El MIDI exportado usa un solo tempo y un solo compás** (los del final de la
  grabación). Las posiciones están en negras, así que si el proyecto tiene cambios de
  tempo igual caen en su lugar al soltarlo en el compás indicado. Con el transporte
  detenido (o en el Standalone) el reloj corre a mano desde la posición del cursor.
- Máximo 64 tomas por sesión y 1 s por toma (memoria reservada la primera vez que se
  activa la captura, ~25 MB a 48 kHz).
- Sin tests dentro de un DAW real todavía; el motor se valida con
  `tests/EngineTest.cpp` (golpes sintéticos).

## Descartado a propósito (para que siga liviano)

Hi-hat abierto/cerrado con choke entre dos notas, modo offline (analizar un archivo sin
reproducirlo), presets / A-B y ventana redimensionable con zoom: es una herramienta de
otra escala. Tampoco hay varios instrumentos por instancia ni salida de audio separada
para las muestras (una instancia por instrumento).

## Próximos pasos

- Detección estéreo con selección de canal, y por banda (bombo + toms en la misma pista).
- Normalizar niveles dentro de cada capa al crear las muestras (opcional).
- Huella por golpe individual (varios instrumentos en una instancia, cada uno a su nota).

## Compilar y testear

```
cmake -S plugins/decimul-trigger -B plugins/decimul-trigger/build
cmake --build plugins/decimul-trigger/build --config Release

# test de integración del motor (opcional)
cmake -S plugins/decimul-trigger -B plugins/decimul-trigger/build -DDECIMU_TRIGGER_TESTS=ON
cmake --build plugins/decimul-trigger/build --config Release --target TriggerEngineTest
plugins/decimul-trigger/build/TriggerEngineTest_artefacts/Release/TriggerEngineTest.exe
# captura de la UI sin DAW
TriggerEngineTest.exe --screenshot ruta.png            # (con --detalles al final suma lo nuevo de v0.5)
```
