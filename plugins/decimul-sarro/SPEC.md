# DecimuLabs Sarro — Spec (nombre de trabajo)

Supresor de resonancias: baja solo los picos que sobresalen del espectro (la aspereza de una voz, el
"ring" de una guitarra, un silbido de micrófono, el filo de un platillo) y deja el resto. Actúa por
frecuencia y a lo largo del tiempo, con el espectro y la reducción a la vista.

"Sarro" es lo que se pega y se saca con la limpieza: lo áspero que se acumula. El nombre es de trabajo
(se cambia en `CMakeLists.txt`, `PRODUCT_NAME`, y en los textos de la UI). Búsqueda web de choques el
17/11/2026: ningún plugin con ese nombre.

Inspirado en supresores de resonancias dinámicos como *RESO* (Mastering The Mix), *SpecCraft*
(Three-Body Technology) y *Sleek* (HoRNet); nombres verificados con una búsqueda web.
Implementación propia, con nombre, interfaz y código propios.

## Uso

1. Ponelo en la pista o el grupo con aspereza (voz, guitarra, platillos, el bus de batería).
2. Subí **DEPTH** de a poco y mirá la pantalla: lo rojo que cuelga de arriba es lo que se está sacando.
3. **SENSITIVITY** decide cuánto tiene que sobresalir un pico para tratarlo; **SHARPNESS** qué tan angosto.
   Con **DELTA** oís solo lo que se saca: tiene que sonar a aspereza, no a cuerpo.
4. **LOW FOCUS** y **HIGH FOCUS** limitan la zona de trabajo (por ejemplo, dejar en paz los graves y el aire).
5. **ATTACK** y **RELEASE** deciden qué tan rápido empieza y suelta. **LINK** 100 % aplica la misma reducción a
   los dos canales (la imagen estéreo no se mueve).
6. **MIX** y **TRIM** para la mezcla y el nivel. El plugin tiene latencia (46 ms a 44.1 kHz, 43 ms a 48 kHz y a 96 kHz,
   donde usa una FFT de 4096) y se la informa al DAW.

## Cómo funciona (`Source/SarroDsp.h`)

```
entrada ► ventana raíz de Hann (FFT 2048, o 4096 a 88.2 kHz o más; solapamiento 75 %) ► espectro de L y R (una FFT compleja)
        ► potencia promediada ~2.5 cuadros ► dB
        ► piso = promedio en dB sobre una ventana de 0.16 a 0.62 octava (según SHARPNESS)
        ► exceso = dB - piso - umbral (12 dB a 2 dB según SENSITIVITY)
        ► reducción = exceso x DEPTH x 1.25 (tope 24 dB), solo dentro de LOW/HIGH FOCUS (bordes suaves de media octava)
        ► suavizado entre bins vecinos y en el tiempo (ataque y caída por bin) ► enlace estéreo
        ► ganancia por bin ► FFT inversa ► ventana y suma ► salida (retrasada N muestras)
```

- La señal original se retrasa lo mismo que el procesamiento: MIX, BYPASS y DELTA quedan alineados.
- DELTA = original retrasada - procesada.

## Medido (tests de motor, `SarroDspTest`, TODO OK)

Con DEPTH 0 la salida es la entrada retrasada 2048 muestras (error 1e-7); un pico de 3 kHz baja unos 20 dB y el ruido de
alrededor 0.2 dB; ruido blanco solo pierde 0.3 dB con sensibilidad baja; DEPTH 50 baja ~60 % de lo que baja DEPTH 100;
LOW/HIGH FOCUS dejan intacto lo de afuera; SHARPNESS alto trata el pico aislado y deja más el grupo ancho; RELEASE largo
suelta 9 veces más lento que corto; MIX 0, DELTA y BYPASS exactos; enlace estéreo; la pantalla marca la frecuencia correcta
y está calibrada en dBFS; 44.1, 48 y 96 kHz con parámetros extremos y cambiando; ~28x tiempo real.

## Límites

- Sin probar por oído ni en un DAW por mí.
- En material muy armónico (una voz sola a DEPTH alto) los armónicos también son "picos": baja un poco la fundamental y
  los armónicos fuertes. Usá DEPTH moderado, SENSITIVITY baja y mirá DELTA.
- Tiene latencia (se informa al DAW): no sirve para tocar en vivo a través de él.
- Sin sidechain ni modo mid/side; la zona de trabajo se elige con las perillas, no arrastrando en la pantalla.
