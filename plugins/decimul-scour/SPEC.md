# DecimuLabs Scour — Spec (nombre de trabajo)

Efecto lo-fi de seis módulos para ensuciar y envejecer el sonido: **NOISE** (ruido), **WOBBLE**
(temblor de cinta), **DISTORT** (distorsión), **DIGITAL** (bitcrusher), **SPACE** (reverb lo-fi) y
**MAGNETIC** (cinta gastada). Cada módulo se prende y apaga, tiene su cantidad y un control FLUX
(deriva aleatoria lenta); MAGNITUDE sube o baja todos juntos. Latencia cero, mono o estéreo.

"Scour" es "fregar hasta desgastar": lo que le hace el plugin al sonido. El nombre es de trabajo:
se cambia en `CMakeLists.txt` (`PRODUCT_NAME`) y en los textos de la UI. Búsqueda web de choques el
23/10/2026: no apareció ningún plugin con ese nombre (Oxide, Corrosion y Erode ya existen, se descartaron).

Inspirado en efectos lo-fi de módulos en fila como *RC-20 Retro Color* (XLN Audio), *Dirty Tape*
(Softube) y *Retro Fi* (Waves). Implementación propia, con nombre, interfaz y código propios.

## Cadena

```
entrada ► ganancia ► [NOISE pre] ► DISTORT ► DIGITAL ► WOBBLE ► MAGNETIC ► SPACE ► [NOISE post] ►
EQ ► ancho ► MIX con la entrada ► ganancia de salida
```

## Módulos

| Módulo | Perilla grande | Controles | Qué hace |
|---|---|---|---|
| NOISE | LEVEL | tipo (Vinyl, Tape, Hum, Static, VHS, Dust), TONE, FOLLOW, DUCK, ROUTING pre/post, FLUX | Ruido a nivel previsto (-70 a -20 dB con LEVEL). FOLLOW lo hace seguir tu señal, DUCK lo baja cuando suena |
| WOBBLE | DEPTH | WOW-FLUT, WOW RATE, FLUT RATE, STEREO, MIX, FLUX | Retardo variable: wow lento (hasta 45 centésimos) y flutter rápido (hasta 14), con deriva aleatoria |
| DISTORT | DRIVE | tipo (Tube, Tape, Diode, Fold), FOCUS, MIX, FLUX | Saturación con nivel compensado; FOCUS distorsiona solo lo que está por encima de una frecuencia |
| DIGITAL | AMOUNT | RATE-BITS, FOCUS, SMOOTH, MIX, FLUX | Baja la frecuencia de muestreo (hasta 2.2 kHz, con aliasing) y los bits (hasta 4) |
| SPACE | LEVEL | DECAY, PREDELAY, FOCUS, STEREO, FLUX | Reverb FDN de la suite con predelay y filtro de foco en el envío (oscurece o adelgaza) |
| MAGNETIC | AMOUNT | WEAR-FLUT, RATE, DROPOUTS, STEREO, FLUX | Pérdida de agudos, temblor de nivel y caídas de volumen aleatorias |

- **MAGNITUDE** multiplica la cantidad de todos los módulos (100 % = lo que dicen las perillas).
- **FLUX**: deriva aleatoria suavizada (cambia cada 0.7 a 2.5 s) que mueve la cantidad del módulo entre
  ~0.35x y 1.65x a profundidad máxima, y en WOBBLE y SPACE también las velocidades y el decay.
- **EQ final**: pasa-altos, pasa-bajos y tilt (TONE), con encendido propio.
- **WIDTH** (0 a 200 %), **MIX** general y ganancias de entrada y salida.

## Medido (tests de motor, `ScourDspTest`, TODO OK)

Cantidades en 0 = señal intacta; ruidos a nivel previsto y sin pasarse de 0 dBFS; distorsión con
armónicos y nivel parejo; bitcrusher: el tono de 10 kHz cae > 15 dB, 4 bits = ruido marcado; wow al 100 %
= ±55 centésimos medidos y flutter ±16; wear al 100 %: -16 dB a 10 kHz y graves intactos; caídas de volumen
solo si se piden; reverb con cola, decay y predelay; EQ; ancho 0 = mono; MIX 0 y MAGNITUDE 0 = señal intacta;
FLUX deriva solo si se pide; todo al máximo con parámetros cambiando a 44.1, 48 y 96 kHz = salida finita.

## Límites

- Sin probar por oído ni en un DAW por mí.
- WOBBLE es un vibrato de verdad (cambia el tono) y su retardo medio crece con la profundidad (hasta ~20 ms con wow lento al máximo): con MIX bajo se oye como chorus.
- El ruido es sintético (no son muestras grabadas).
- Sin presets propios todavía (los guarda el DAW).
