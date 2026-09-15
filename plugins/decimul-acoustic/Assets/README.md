# Assets de marca

Bajados directamente de [decimulabs.com](https://www.decimulabs.com) (sitio del propio
usuario/estudio) y de Google Fonts, para que la UI del plugin use la identidad real de
decimulabs en vez de inventar una paleta genérica.

| Archivo | Origen | Licencia |
|---|---|---|
| `wordmark.png` | `decimulabs.com/logo/wordmark.png` | Propiedad de decimulabs |
| `dog.png` | `decimulabs.com/logo/dog.png` | Propiedad de decimulabs |
| `ChakraPetch-Regular.ttf` / `ChakraPetch-Bold.ttf` | [Google Fonts — Chakra Petch](https://fonts.google.com/specimen/Chakra+Petch) | SIL Open Font License 1.1 (ver `ChakraPetch-OFL.txt`) — libre, redistribuible, incluso embebida en un binario |
| `VT323-Regular.ttf` | [Google Fonts — VT323](https://fonts.google.com/specimen/VT323) | SIL Open Font License 1.1 (ver `VT323-OFL.txt`) |

Paleta extraída del CSS real del sitio (`getComputedStyle` sobre la página en vivo):

| Token | Valor | Uso en el sitio |
|---|---|---|
| Fondo | `#06070D` | `body` background |
| Texto | `#C9D2E0` | texto de cuerpo |
| Texto muted | `#7A87A3` | texto secundario |
| Acento rojo/naranja | `#FF5A36` | punto "REC" |
| Acento cian | `#A6F0FF` | highlights |
| Acento azul | `#3D7BFF` | highlights |
| Acento azul claro | `#7FA8FF` | highlights |

Estos assets se empaquetan en el binario del plugin vía `juce_add_binary_data` (ver
`CMakeLists.txt`) — no se cargan en runtime desde ningún sitio externo, quedan
compilados adentro del `.vst3`/`.exe`.
