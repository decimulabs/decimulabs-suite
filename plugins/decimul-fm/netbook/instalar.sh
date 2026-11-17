#!/bin/bash
# Deja una netbook con antiX (versión sysVinit) arrancando directo en el Sinte FM 16 Bit.
# Correr con tu usuario, desde la carpeta donde están estos archivos:  bash instalar.sh
set -e
if [ "$(id -u)" -eq 0 ]; then echo "Correlo con tu usuario, no como root:  bash instalar.sh"; exit 1; fi
if [ ! -f /etc/inittab ]; then echo "No encuentro /etc/inittab: esto es para antiX con sysVinit."; exit 1; fi
if grep -q '# >>> sinte$' "$HOME/.bash_profile" "$HOME/.profile" 2>/dev/null; then
  echo "Esta netbook ya arranca en el otro sinte (FluidSynth). Sacalo primero:  bash ~/sinte/desinstalar.sh"; exit 1
fi
AQUI="$(cd "$(dirname "$0")" && pwd)"

echo "== 1/5 Instalando el compilador y ALSA (hace falta internet) =="
sudo apt-get update
sudo apt-get install -y gcc libc6-dev libasound2-dev alsa-utils

echo "== 2/5 Armando el sinte en ~/sintefm =="
mkdir -p "$HOME/sintefm"
cp "$AQUI"/sintefm.c "$AQUI"/ym2612.c "$AQUI"/ym2612.h "$AQUI"/ym_tables.h "$AQUI"/presets.h "$AQUI"/desinstalar.sh "$HOME/sintefm/"
sed -i 's/\r$//' "$HOME/sintefm/desinstalar.sh"   # por si los archivos pasaron por Windows
chmod +x "$HOME/sintefm/desinstalar.sh"
( cd "$HOME/sintefm" && gcc -O2 -o sintefm sintefm.c ym2612.c -lasound -lm )
echo "   compilado: $HOME/sintefm/sintefm"

echo "== 3/5 Permisos de audio y de apagado =="
sudo usermod -aG audio "$USER"
printf '@audio - rtprio 95\n@audio - memlock unlimited\n' | sudo tee /etc/security/limits.d/sintefm-audio.conf >/dev/null
echo "$USER ALL=(root) NOPASSWD: /sbin/poweroff, /sbin/reboot" | sudo tee /etc/sudoers.d/sintefm >/dev/null
sudo chmod 440 /etc/sudoers.d/sintefm
if ! sudo visudo -cf /etc/sudoers.d/sintefm >/dev/null; then
  sudo rm -f /etc/sudoers.d/sintefm
  echo "   (no se pudo habilitar el apagado desde el menú; se apaga con el botón)"
fi

echo "== 4/5 Inicio automático en la consola 1 =="
[ -f /etc/inittab.antes-del-sintefm ] || sudo cp /etc/inittab /etc/inittab.antes-del-sintefm
GETTY=/sbin/agetty
[ -x "$GETTY" ] || GETTY=/sbin/getty
sudo sed -i -E "s|^1:([0-9]+):respawn:.*|1:\1:respawn:$GETTY --autologin $USER --noclear 38400 tty1 linux|" /etc/inittab
if ! grep -q -- "--autologin $USER" /etc/inittab; then
  echo "No pude configurar el inicio automático (no encontré la línea de la consola 1 en /etc/inittab)."
  exit 1
fi
PERFIL="$HOME/.bash_profile"
[ -f "$PERFIL" ] || PERFIL="$HOME/.profile"
if ! grep -q '# >>> sintefm' "$PERFIL" 2>/dev/null; then
  cat >>"$PERFIL" <<'FIN'
# >>> sintefm (lo agregó instalar.sh; desinstalar.sh lo saca)
if [ "$(tty)" = /dev/tty1 ]; then
  amixer -q sset Master 90% unmute 2>/dev/null; amixer -q sset PCM 90% unmute 2>/dev/null
  "$HOME/sintefm/sintefm"
  [ $? -eq 10 ] || exit
fi
# <<< sintefm
FIN
fi

echo "== 5/5 Pantalla =="
read -r -p "Querés que prenda directo en el sinte, sin escritorio gráfico? (s/n) " r
if [[ $r == [sS]* ]]; then
  : >"$HOME/sintefm/.escritorios-apagados"
  for dm in slimski slim lightdm lxdm sddm; do
    if [ -x "/etc/init.d/$dm" ] && sudo update-rc.d "$dm" disable >/dev/null 2>&1; then
      echo "$dm" >>"$HOME/sintefm/.escritorios-apagados"
    fi
  done
  echo "   Listo: al prender va directo al sinte. El escritorio se abre a mano con 'startx'."
else
  echo "   El escritorio sigue. El sinte suena igual por detrás; para verlo: Ctrl+Alt+F1."
fi

echo
echo "Todo instalado. Reiniciá con:  sudo reboot"
