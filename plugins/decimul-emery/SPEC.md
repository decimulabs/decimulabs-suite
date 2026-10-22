# DecimuLabs Emery — Spec (nombre de trabajo)

De-esser y de-popper para voces en un solo plugin liviano: baja las **eses ásperas** (SIBILANCIA) y
los **golpes de aire** de la p y la b (GOLPES). Sin latencia, con los canales ligados (la imagen
estéreo no se mueve).

"Emery" es el esmeril, el abrasivo con el que se lija. El nombre es de trabajo: se cambia en
`CMakeLists.txt` (`PRODUCT_NAME`) y en los textos de la UI. Búsqueda web de choques el 22/10/2026:
no apareció ningún plugin con ese nombre.

Inspirado en de-essers como *Pro-DS* (FabFilter), *DeEsser* (Waves), *VoxDeEss* (LANDR) y *DeEsser*
(Sonnox) — nombres verificados con una búsqueda web. Implementación propia con nombre, interfaz y
código propios.

## Uso

1. Ponelo en la pista de voz (mono o estéreo; la salida tiene el mismo formato que la entrada).
2. **SIBILANCIA**: prendé `LISTEN` y barré **FREQ** hasta oír la parte que silba (suele estar entre 5 y 8 kHz).
   Bajá **THRESHOLD** hasta que lo gris cruce la línea solo en las eses ásperas; **AMOUNT** es lo
   máximo que va a bajar (6 a 10 dB suena natural).
3. **GOLPES**: lo mismo con **CUTOFF** (100 a 150 Hz va bien para casi todas las voces) y su
   umbral. Una voz normal tiene que quedar por debajo de la línea; solo el golpe la cruza.
4. **BAND** (apagado) baja solo la banda; **WIDE** baja todo el sonido mientras pasa la ese o el golpe.
5. **LISTEN** deja oír solo lo que se está quitando de esa sección.

## Cómo funciona (`Source/EmeryDsp.h`)

```
entrada ► [ SIBILANCIA ] ► [ GOLPES ] ► salida
 sección:  banda = pasabanda (ese, Q 1.4) o pasabajos de 4º orden (golpe)
           detector = pico de la banda (el mayor de L y R), cae suave
           reducción (dB) = (nivel de la banda - umbral), entre 0 y AMOUNT
           reducción suavizada (ataque 0.25 ms / caída 28 ms en eses; 0.6 ms / 90 ms en golpes)
           BAND: salida = entrada - (1 - g) x banda      WIDE: salida = entrada x g
```

- La reducción lleva la banda hasta el umbral (no más), así que el efecto depende de cuánto se pasa.
- La sección de golpes mira lo que sale de la de eses. Cero latencia (no mira hacia adelante): el
  primer instante (0.2 ms) de una ese o un golpe muy brusco pasa antes de que el detector reaccione.
- Las frecuencias se suavizan: se pueden mover sonando sin clicks.
- LISTEN: salida = (lo que entra a la sección) - (lo que sale); con las dos prendidas, todo lo quitado.

## Límites

- Umbral absoluto en dBFS (no relativo al nivel de la voz): hay que ajustarlo a cada grabación.
- Un golpe que saturó el micrófono no se arregla después.
- Sin probar por oído ni en un DAW por mí.
