# DecimuLabs Carve — Spec

Ducking espectral: le "hace lugar" a una pista según lo que suena en otra. Lo que entra
por el **sidechain** (ej. el bombo, la voz) se mide en 32 bandas y, en la pista donde
está Carve (ej. el bajo, los sintes), se baja **sólo** lo que el sidechain está
ocupando, en vez de bajar todo el volumen como un compresor con sidechain.

Es la técnica clásica de EQ dinámico con sidechain, la misma categoría de herramientas
que *Trackspacer* (Wavesfactory) o *Kickstart*-style carving. Implementación propia,
con nombre, interfaz y código propios; no copia a ningún plugin.

## Uso

1. Poné Carve en la pista que tiene que **dejar lugar** (ej. el bajo).
2. Mandale al **sidechain de Carve** la pista que **molesta** (ej. el bombo).
3. Subí **AMOUNT** hasta que se note el hueco que abre en la otra pista; ajustá **SC GAIN** para que "escuche"
   más o menos del sidechain.
4. **RECORTA DESDE / HASTA** limitan el rango donde se permite recortar (ej. no tocar
   por debajo de 120 Hz para que el bajo conserve su pegada).

Si el sidechain no está ruteado, Carve no toca nada y la pantalla avisa.

## Controles

| Control | Qué hace |
|---|---|
| AMOUNT (0–100 %) | Cuánto recorta como máximo: 100 % ≈ 24 dB de recorte real en bandas muy cargadas |
| SC GAIN (±24 dB) | Ganancia del sidechain antes de medirlo (mide más fuerte = recorta más) |
| ATTACK / RELEASE | Velocidad del seguidor de nivel por banda |
| SMOOTH | Esparce el recorte a bandas vecinas (suena más natural, menos "agujereado") |
| MIX | Dry/wet |
| RECORTA DESDE / HASTA | Rango de frecuencias donde puede recortar |
| OUTPUT | Trim de salida |

## La pantalla

- **Arriba** (color de acento) cuelga el recorte que Carve le aplica a la pista en cada una de las 32
  bandas; **abajo** (gris) sube el nivel del sidechain en esa banda.
- **Escalas en dB**: a la izquierda la del recorte (0 a -24 dB, de arriba hacia abajo), a la derecha la
  del sidechain (0 a -60 dBFS, de abajo hacia arriba).
- Arriba a la derecha dice cuánto es el **recorte máximo** en este momento y en qué frecuencia
  ("RECORTE MAX -9.2 dB EN 31 Hz"), o "SIN RECORTE".
- Al **pasar el mouse por una banda** muestra su frecuencia, su recorte y el nivel del sidechain exactos.

## DSP

- **Análisis:** el sidechain (suma a mono) pasa por 32 pasa-bandas (2 biquads en cascada
  por banda, Q=3, 12 dB/oct) logarítmicamente espaciados de 25 Hz a 18 kHz; cada banda
  tiene un seguidor de nivel (attack/release).
- **Mapeo nivel → recorte:** cubic sobre el rango -60..-6 dB: `recorte = amount · 24 dB · x³`.
  Compresivo a propósito: lo suave casi no recorta, lo fuerte sí.
- **Recorte:** cascada de 32 campanas (peaking, Q=3, fase mínima) sobre L y R con la
  misma ganancia (estéreo linkeado). Cada campana aplica `recorte / 2.2` para compensar
  el solape con las vecinas (medido en `tests/EngineTest.cpp`).
- **Latencia:** cero.
- Coeficientes se recalculan cada 32 muestras.

## Pruebas

- `tests/EngineTest.cpp` (motor sin JUCE): tono en el sidechain baja sólo ese tono; Amount 0
  y sin sidechain no tocan; el rango protege; Mix; parámetros extremos sin NaN;
  recuperación al callarse el sidechain.
  Build: `cmake -DDECIMU_CARVE_TESTS=ON` y target `CarveEngineTest`.
- `tests/UiShot.cpp`: renderiza la UI a PNG con datos de ejemplo
  (`CarveUiShot.exe ruta.png [sinSidechain]`, `-DDECIMU_CARVE_UISHOT=ON`).

## Pendiente / ideas

- **No probado todavía en un DAW real** (sólo motor + captura de UI): falta verificar el ruteo
  de sidechain en Reaper, Ableton y FL.
- Modo Mid/Side (recortar sólo el centro o sólo los lados).
- Botón "aprender": captura el espectro del sidechain como curva fija.
- Modo "escuchar el recorte" (oír sólo lo que se está sacando).
- Zoom de la pantalla / cambiar el número de bandas.
