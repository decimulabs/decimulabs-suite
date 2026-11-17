#!/bin/bash
# Vuelve la netbook a como estaba antes del Sinte FM. El compilador y ALSA quedan instalados.
if [ -f /etc/inittab.antes-del-sintefm ]; then sudo cp /etc/inittab.antes-del-sintefm /etc/inittab; fi
for f in "$HOME/.bash_profile" "$HOME/.profile"; do
  [ -f "$f" ] && sed -i '/# >>> sintefm/,/# <<< sintefm/d' "$f"
done
if [ -f "$HOME/sintefm/.escritorios-apagados" ]; then
  while read -r dm; do sudo update-rc.d "$dm" enable >/dev/null 2>&1; done <"$HOME/sintefm/.escritorios-apagados"
  rm -f "$HOME/sintefm/.escritorios-apagados"
fi
sudo rm -f /etc/sudoers.d/sintefm /etc/security/limits.d/sintefm-audio.conf
echo "Listo. Reiniciá con:  sudo reboot"
