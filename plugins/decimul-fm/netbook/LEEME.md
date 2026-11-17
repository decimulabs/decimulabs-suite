# Netbook Sinte FM 16 Bit

Convierte una Conectar Igualdad con **antiX (sysVinit)** en el Sinte FM 16 Bit: el mismo chip de la
Mega Drive que la página web y el VST, con los mismos 46 sonidos. La prendés y a los pocos segundos suena.
Es un programa chiquito en C (no necesita escritorio ni navegador), así que anda bien en la netbook.

## Instalar

1. Copiá esta carpeta (`sintefm`) a un pendrive.
2. En la netbook, con antiX ya instalado y **conectado a internet**, abrí una terminal y entrá a la
   carpeta del pendrive, por ejemplo `cd /media/sdb1/sintefm`
3. Corré `bash instalar.sh`. Te pide la contraseña, instala el compilador, arma el sinte y al final
   pregunta si querés que prenda directo en el sinte, sin escritorio.
4. Reiniciá: `sudo reboot`.

Si la netbook ya tiene el otro sinte (el de FluidSynth), el instalador te pide sacarlo primero: una
netbook, un aparato.

## Usarlo

| Tecla | Qué hace |
|---|---|
| Flechas arriba / abajo | Cambiar de sonido |
| Flechas izq / der | Saltar a otra familia (bajos, teclados, bronces…). En modo tocar: cambiar de octava |
| + / - | Volumen |
| Enter | Escuchar un acorde de prueba |
| d | Prender/apagar la distorsión del DAC de la Mega Drive 1 |
| Tab | Tocar con el teclado de la netbook (Z S X D C V… y Q 2 W 3 E…). Tab o Esc para volver |
| Espacio | Silencio (corta notas colgadas) |
| a | Apagar la netbook |
| q | Salir al sistema (para volver al sinte: `exit`) |

**Teclado MIDI USB:** enchufalo cuando quieras; se conecta solo y aparece arriba al lado de "MIDI".
Toca hasta 6 notas a la vez (como el chip). La rueda de afinación mueve ±2 semitonos y el cambio de
programa del teclado elige el sonido.

El último sonido, el volumen y la octava quedan guardados en `~/.sintefm`.

## Si algo no anda

- **No suena:** apretá `q`, corré `aplay -l` y poné la placa correcta en `~/.sintefm`, por ejemplo
  `AUDIO_DEV=plughw:1`. Después `exit` para volver.
- **Cortes o chasquidos:** en `~/.sintefm` subí `PERIODO=256` a `PERIODO=512`.
- **Tarda en responder:** probá `PERIODO=128`, si no aparecen cortes.
- **Volver todo atrás:** `bash ~/sintefm/desinstalar.sh` y reiniciar.

## Para quien lo mantiene

El motor (`ym2612.c`, `ym2612.h`, `ym_tables.h`) y los sonidos (`presets.h`) se copian desde
`sims/fm` y la suite con `node netbooks/sintefm/armar.js`. No editarlos acá.
