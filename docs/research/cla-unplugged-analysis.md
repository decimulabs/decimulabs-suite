# Análisis de referencia: Waves CLA Unplugged

Fuente primaria: [manual oficial de usuario (PDF)](https://assets.wavescdn.com/pdf/plugins/cla-unplugged.pdf)
y [ficha de producto](https://www.waves.com/plugins/cla-unplugged).

No se copia UI, gráficos, nombres de controles ni el naming de "colores" — solo se toma
la **estructura funcional** como punto de partida, de dominio de idea (un "channel strip
todo-en-uno" es un concepto genérico, no una obra protegible).

## Contexto del producto original

- Parte de la **CLA Artist Collection** (Vocals, Drums, Bass, Guitars, Unplugged, Effects),
  diseñada por Chris Lord-Alge para "instrumentos desenchufados": voz, piano, guitarra
  acústica y guitarra de nylon.
- Frase del propio CLA: *"a dream box of reverb effects plus great hi-res EQ and
  compression [...] designed to give you limitless reverb and delay possibilities"*.
- Dos componentes: mono→estéreo y estéreo→estéreo (WaveShell).

## Cadena de señal (orden real, de izquierda a derecha en la UI)

| # | Módulo | Control principal | Opciones ("Color") | Notas |
|---|---|---|---|---|
| 1 | Input | Input Sens (±10, pasos 0.1) + LED (apagado/verde/amarillo/rojo) + Balance (±6 dB, solo stereo) | — | Meter -26..0 dBFS |
| 2 | EQ grave | Bass (±10) | Sub / Lower / Upper | Bypass real si el fader está en 0 |
| 3 | EQ agudo | Treble (±10) | Bite / Top / Roof | ídem |
| 4 | Dinámica | Compress (±10) | Push / Spank / Wall | — |
| 5 | Reverb A | Reverb 1 (wet ±10) | Room / Hall / Chamber | + Pre-Delay 1 (64T–1/4, sync BPM), on/off |
| 6 | Reverb B | Reverb 2 (wet ±10) | Tight / Large / Canyon | + Pre-Delay 2 (64T–1/4, sync BPM), on/off |
| 7 | Delay | Delay (wet ±10) | Slap / Eighth / Quarter | tempo-synced |
| 8 | Mezcla | Direct (on/off) | — | on = insert; off = send/return |
| 9 | Output | Output (±10) + Clip LED | — | Meter -26..0 dBFS |

Detalles de comportamiento a rescatar (son buenas decisiones de UX, no "propiedad" de
nadie):

- Con el fader de EQ en 0, cambiar de "color" no hace nada — evita sorpresas.
- Los demás faders en 0 **sí** aplican el procesamiento default de CLA (no son bypass real)
  — nosotros vamos a hacer bypass real en todos, es más predecible para el usuario.
- Pre-delay sincronizado a tempo de sesión, con opción de encendido independiente del
  nivel de reverb.
- Pensado para insertarse como insert (Direct on) o como aux/return (Direct off).

## Traducción a "DecimuLabs Acoustic" (nuestra versión)

Mismo esqueleto de 9 etapas, pero: nombres propios, bypass real en todos los módulos,
oversampling opcional en dinámica, y fallback de tempo cuando no hay host (ver
[SPEC.md del plugin](../../plugins/decimul-acoustic/SPEC.md) para el detalle completo).
