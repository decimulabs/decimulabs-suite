# Notas de licenciamiento

Resumen de qué podemos usar y bajo qué condiciones, dado que la suite se libera como
**freeware/donationware y open source (GPLv3)**.

## JUCE

- JUCE es gratis **solo si el proyecto que lo usa también se libera bajo GPLv3** (código
  fuente completo público). Las licencias "Indie" y "Pro" son pagas y existen para poder
  cerrar el código fuente — no es nuestro caso, así que no hace falta pagarlas.
- Consecuencia práctica: todo el repo `decimulabs-suite` debe ir con licencia **GPLv3**
  (agregar archivo `LICENSE` en la raíz — pendiente de crear cuando se suba a GitHub).
- Fuente: https://juce.com/get-juce/ (comparar tiers).

## Airwindows

- Licencia **MIT**. Repo: https://github.com/airwindows/airwindows
- Cada plugin vive en `plugins/<Nombre>/` como un `.cpp`/`.h` de DSP puro (clase con
  `processReplacing`), separable del wrapper VST. Ideal para extraer el algoritmo y
  envolverlo en nuestro propio `juce::dsp` processor.
- Reglas: mantener el aviso de copyright de Chris Johnson en el archivo vendorizado
  (carpeta `third_party/airwindows/<algoritmo>/`), documentar de qué plugin salió cada uno.
- Candidatos usados en Decimul Acoustic: compresores **Pop!**, **Pulse**, **Velvet**,
  **Volt**; reverbs de la colección **Spaces** (16 algoritmos K-series).

## fluidsynth (para el reproductor SF2)

- Licencia **LGPL v2.1**. Repo: https://github.com/FluidSynth/fluidsynth
- LGPL permite linkear (incluso estáticamente, con más cuidado) desde un proyecto GPL sin
  problema — como nuestro repo ya es GPLv3, no hay fricción. (El motivo original para
  evitar el LGPL era solo relevante si hubiéramos querido cerrar el código; no es el caso).
- Decisión: escribir nuestro propio wrapper JUCE + fluidsynth en vez de forkear
  `juicysfplugin` (que es GPLv3 pero con una arquitectura/UI que no necesitamos heredar).

## Dragonfly Reverb (referencia, no vendorizado todavía)

- Licencia **LGPL-3.0**. Repo: https://github.com/michaelwillis/dragonfly-reverb
- Se usa solo como *referencia de calidad* para ajustar nuestros propios reverbs
  Airwindows-based; si en algún momento vendorizamos código de ahí, mismo razonamiento
  que fluidsynth (compatible con GPLv3).

## Regla general para nuevos módulos

Antes de sumar cualquier repo de terceros a `third_party/`, confirmar que su licencia sea
MIT, BSD, Apache-2.0, LGPL o GPL-compatible. Evitar licencias "no comercial" o "custom" que
no permitan redistribución — aunque no cobremos nada, donationware sigue siendo
redistribución pública.
