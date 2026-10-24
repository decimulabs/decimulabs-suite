# DecimuLabs Prisma (nombre de trabajo)

EQ paramétrico de 24 bandas en la línea del FabFilter Pro-Q (3 y 4). VST3 + Standalone, Windows.
Pedido de Marcelo (24/10/2026): "el clon del Pro-Q completo, con filtros para aislar las bandas y
todo lo que el Pro-Q tiene". El nombre lo elige Marcelo; "Prisma" es de trabajo.

## Qué hace

**Bandas** (hasta 24, se crean con doble clic):
- Formas: campana, low shelf, low cut, high shelf, high cut, notch, pasabanda, tilt shelf y tilt
  plano (pendiente constante en todo el espectro; la ganancia es lo que sube entre 20 Hz y 20 kHz).
- Pendientes: 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct y brickwall (cortes, shelves, notch, pasabanda).
- Frecuencia 10 Hz a 30 kHz, ganancia ±30 dB, Q 0.025 a 40 (en cortes y shelves, Q > 0.7 = resonancia).
- Ubicación estéreo por banda: estéreo, izquierda, derecha, centro (Mid) o lados (Side).
- EQ dinámico por banda: rango (±30 dB), umbral (o automático: sigue el promedio + 4 dB), sidechain
  externo; ataque y release globales. Ley: rodilla de 6 dB y 2:1 hasta llegar al rango.
- Dinámica espectral (como el Pro-Q 4): dentro de la banda cada bin de FFT se comprime solo.
- Solo de banda ("aislar"): se escucha solo la zona de la banda. En bell/shelf/pasabanda se escucha
  la zona ya ecualizada; en cortes y notch se escucha lo que se está sacando. Botón SOLO o Alt
  mientras se arrastra.

**Modos de proceso**:
- Latencia cero: biquads de "magnitud igualada" (Vicanek 2016 ampliado: polos por invarianza al
  impulso, numerador por mínimos cuadrados del error en dB, ajuste conjunto de la cascada). Sigue a
  la curva analógica dentro de 0.25 dB hasta ~0.3 fs (13 kHz a 44.1 kHz) y 1 dB más arriba.
- Fase natural: el mismo motor a 4x (2x a 88.2/96 kHz) con filtros de media banda de fase lineal:
  magnitud y fase analógicas en todo el rango audible. Latencia 64 muestras a 44.1/48 kHz.
- Fase lineal: FIR simétrico por FFT particionada (matriz 2x2 si hay Mid/Side). Resoluciones Low a
  Maximum = FIR de 2048 a 32768 muestras a 48 kHz (x2/x4 a 96/192). Latencia 512 + FIR/2.
  El FIR se arma en un hilo aparte; al exportar (offline) se arma en el hilo de audio (determinístico).

**Global**: carácter (Clean / Subtle / Warm: satura solo lo que el EQ cambia), Gain-Q, escala de
ganancia (-100 a 200 %), auto gain (por la curva, no por el audio), salida, balance, fase, bypass con
la latencia compensada.

**Pantalla**: analizador pre/post/sidechain (resolución, velocidad, inclinación 0-6 dB/oct, rango,
retener picos, congelar), spectrum grab (agarrar un pico crea una banda ahí), zoom ±3/6/12/30 dB,
teclado de piano, curvas por canal (L/R/S punteadas), dinámica en vivo, lectura con nota musical.
Selección múltiple (arrastre, Shift+clic, Ctrl+A) con edición relativa desde el panel.

**Herramientas**: EQ Match (contra el sidechain u otra pista con Prisma), EQ Sketch (se dibuja la
curva y se arman las bandas), lista de instancias con detección de choques (rojo donde dos pistas
suenan fuerte en las mismas frecuencias), presets de fábrica y de usuario
(Documentos/decimulabs/Prisma Presets), A/B, deshacer/rehacer.

## Archivos

- `Source/dsp/` — motor en C++ puro (sin JUCE): `PrismaFilters.h` (diseño), `PrismaIir.h`
  (latencia cero + dinámica), `PrismaOversampler.h` (fase natural), `PrismaLinear.h` (fase
  lineal), `PrismaSpectral.h`, `PrismaEngine.h` (todo junto), `PrismaFit.h` (Match/Sketch),
  `PrismaFft.h`.
- `Source/PluginProcessor.*` parámetros (14 por banda + 12 globales), estado, presets, A/B.
- `Source/EqDisplay.*` la pantalla; `BandPanel.*` el panel de banda; `PluginEditor.*` el resto.
- `Source/Analyzer.h`, `InstanceRegistry.h`, `CurveMath.h`.

## Pruebas

- `tests/build_dsp.bat`: test del motor sin JUCE (segundos). Curvas contra la analógica en 4
  frecuencias de muestreo, mediciones con tonos en los 3 modos, latencias exactas, simetría de fase
  lineal, dinámica, sidechain, solo, Mid/Side, espectral, salida, carácter, 4000 filtros al azar
  (estabilidad), parámetros al azar por bloque, barridos sin clics, Match/Sketch y CPU.
- `PrismaEngineTest.exe` (JUCE): parámetros, latencia informada, estado, presets, A/B, deshacer,
  mono, sidechain, instancias. `--screenshot ruta.png [match|piano|ms|dyn]`.

## Limitaciones conocidas

- Sin probar en un DAW real todavía (ni por oído): lo prueba Marcelo en Reaper.
- Latencia cero: cortes muy empinados pegados a Nyquist se apartan hasta ~1 dB (fase natural no).
- La lista de instancias muestra a las otras pistas; no las edita (el Pro-Q sí).
- Dinámica en fase lineal: el FIR se rehace cuando la ganancia se mueve más de 0.1 dB; en tiempo
  real va un poco detrás (unos ms), al exportar es exacto.
