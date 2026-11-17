# DecimuLabs Vise — Spec (nombre de trabajo)

Compresor de bus: "pegamento" para grupos y master con pocas perillas. Ataque y caída (con caída
automática), rodilla, makeup automático, mezcla paralela, pasa-altos del sidechain, enlace estéreo
y detector de pico o RMS. Sin latencia, mono o estéreo.

"Vise" es el tornillo de banco: aprieta. El nombre es de trabajo (se cambia en `CMakeLists.txt`,
`PRODUCT_NAME`, y en los textos de la UI). Búsqueda web de choques el 17/11/2026: ningún plugin con
ese nombre (Clamp sí aparece en las listas de compresores y se descartó).

Inspirado en compresores de bus de la línea de consola de mezcla como *G-Bus Compressor* (Studio9),
*BUSTERse* (Analog Obsession) y *Bus Processor* (Softube); nombres verificados con una búsqueda web.
Implementación propia, con nombre, interfaz y código propios.

## Uso

1. Ponelo en un grupo (batería, voces, instrumentos) o en el master. Mono o estéreo.
2. Bajá **THRESHOLD** hasta que la pantalla muestre entre 2 y 6 dB de reducción en lo más fuerte.
3. **RATIO** 2:1 para pegar suave, 4:1 para firme. **ATTACK** lento (20-30 ms) deja pasar la pegada;
   rápido la aplasta. **RELEASE** corto bombea, largo es suave; **AUTO REL** lo adapta solo.
4. **MAKEUP** (o **AUTO GAIN**) devuelve el nivel. **MIX** menos de 100 % es compresión paralela.
5. **SC HPF** saca los graves del sidechain para que un bombo grande no haga bombear toda la mezcla.
6. **LINK** 100 % baja los dos canales juntos; 0 % cada uno por su cuenta. **PEAK / RMS** elige el detector.

## Cómo funciona (`Source/ViseDsp.h`)

```
entrada ► pasa-altos (sidechain) ► detector (pico con caída de 12 ms, o RMS de 20 ms)
        ► enlace estéreo (mezcla entre el canal y el mayor de los dos)
        ► curva de ganancia con rodilla suave (en dB)
        ► suavizado en dB: ataque si hay que comprimir más, caída si hay que soltar
        ► ganancia + makeup ► mezcla con la señal original ► salida
```

- **Caída automática:** el tiempo de caída crece con la reducción (60 ms + 45 ms por cada dB que se comprime, hasta 1.5 s).
- **Makeup automático:** `-umbral x (1 - 1/ratio) x 0.5` (hasta 24 dB).
- **Mezcla paralela:** la señal original no lleva makeup; la comprimida sí.
- Cero latencia (no mira hacia adelante). Los parámetros se suavizan (20 ms).

## Medido (tests de motor, `ViseDspTest`, TODO OK)

Bajo el umbral pasa intacto; 4:1 a 12 dB sobre el umbral baja 8.99 dB (teórico 9); 10:1 baja 10.79 dB (10.8);
rodilla de 12 dB en el umbral: 1.12 dB (teórico 1.125); ataque de 10 ms alcanza el 63 % en 10.2 ms; caída
de 100 ms y 500 ms medidas a la mitad de la reducción; caída automática más lenta cuanto más comprime;
makeup exacto; mix 0 = original; el pasa-altos del sidechain deja de comprimir con graves; enlace 100 % /
0 %; el detector de pico aprieta 10 dB más que el RMS en golpes cortos; latencia cero; 44.1, 48 y 96 kHz con
parámetros extremos y cambiando; ~118x tiempo real.

## Límites

- Sin probar por oído ni en un DAW por mí. No imita ningún compresor concreto (sin saturación ni "color").
- Sin sidechain externo ni modo mid/side.
- Sin look-ahead (se evita agregar latencia): el ataque más rápido es 0.1 ms.
