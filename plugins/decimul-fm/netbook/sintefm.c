/* sintefm.c — Sinte FM 16 Bit para la netbook (antiX / cualquier Linux con ALSA).
   El mismo motor del chip YM2612 que la página web y el VST (ym2612.c), los mismos 46 sonidos.
   Suena por ALSA, escucha cualquier teclado MIDI que se enchufe (se conecta solo) y se maneja
   con el teclado de la netbook desde la consola, sin escritorio.

   Armar:  gcc -O2 -o sintefm sintefm.c ym2612.c -lasound -lm
   Sale con código 10 cuando se pide "ir al sistema" (así el inicio automático no cierra la sesión). */

#include <alsa/asoundlib.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include "ym2612.h"
#include "presets.h"

#define RATE 44100
#define CHIP_RATE 53267.0
static const int REG_OFFSET[4] = { 0, 8, 4, 12 };

/* ---------------------------------------------------------------- estado guardado en ~/.sintefm */
static int sonido = 0, volumen = 7, octava = 4, periodo = 256, ladder = 1;
static char audioDev[64] = "default";
static char confPath[512];

static void cargar(void)
{
    const char *h = getenv("HOME");
    snprintf(confPath, sizeof confPath, "%s/.sintefm", h ? h : ".");
    FILE *f = fopen(confPath, "r");
    if (!f) return;
    char l[128];
    while (fgets(l, sizeof l, f))
    {
        sscanf(l, "SONIDO=%d", &sonido); sscanf(l, "VOLUMEN=%d", &volumen); sscanf(l, "OCTAVA=%d", &octava);
        sscanf(l, "PERIODO=%d", &periodo); sscanf(l, "DAC=%d", &ladder); sscanf(l, "AUDIO_DEV=%63s", audioDev);
    }
    fclose(f);
    if (sonido < 0 || sonido >= NPRESETS) sonido = 0;
    if (volumen < 1 || volumen > 10) volumen = 7;
    if (octava < 1 || octava > 7) octava = 4;
    if (periodo < 64 || periodo > 4096) periodo = 256;
}

static void guardar(void)
{
    FILE *f = fopen(confPath, "w");
    if (!f) return;
    fprintf(f, "SONIDO=%d\nVOLUMEN=%d\nOCTAVA=%d\nPERIODO=%d\nDAC=%d\nAUDIO_DEV=%s\n", sonido, volumen, octava, periodo, ladder, audioDev);
    fclose(f);
}

/* ---------------------------------------------------------------- el chip */
static YM chip;
static struct { int note; unsigned stamp; long off; } voz[6];   /* off: muestras que faltan para soltar (teclado de la compu), -1 = MIDI */
static unsigned stampCounter;
static float bend = 0.0f;   /* semitonos */

static void cargarSonido(int idx)
{
    const Preset *p = &PRESETS[idx];
    ym2612_ladder(&chip, ladder);
    ym2612_write(&chip, 0, 0x22, p->lfoOn ? (8 | p->lfoFreq) : 0);
    for (int c = 0; c < 6; ++c)
    {
        const int port = c < 3 ? 0 : 1, cc = c % 3;
        ym2612_write(&chip, port, 0xB0 + cc, p->alg | (p->fb << 3));
        ym2612_write(&chip, port, 0xB4 + cc, 0xC0 | (p->ams << 4) | p->pms);
        for (int o = 0; o < 4; ++o)
        {
            const int *op = p->ops[o];   /* AT D1 D2 RL SL VOL ESC MUL DES AM SSG */
            const int r[7] = { (op[8] << 4) | op[7], op[5], (op[6] << 6) | op[0], (op[9] << 7) | op[1], op[2], (op[4] << 4) | op[3], op[10] };
            for (int k = 0; k < 7; ++k) ym2612_write(&chip, port, 0x30 + k * 0x10 + REG_OFFSET[o] + cc, r[k]);
        }
    }
}

static int keyByte(int c) { return c < 3 ? c : c + 1; }

static void afinar(int c)
{
    const double f = 440.0 * pow(2.0, (voz[c].note - 69 + bend) / 12.0);
    int fnum = 2047, block = 7;
    for (int b = 0; b < 8; ++b)
    {
        const int fn = (int) lround(f * 1048576.0 / (CHIP_RATE * pow(2.0, b - 1)));
        if (fn < 2048) { fnum = fn; block = b; break; }
    }
    const int port = c < 3 ? 0 : 1, cc = c % 3;
    ym2612_write(&chip, port, 0xA4 + cc, (block << 3) | (fnum >> 8));
    ym2612_write(&chip, port, 0xA0 + cc, fnum & 0xFF);
}

static void notaOn(int note, long dur)
{
    if (note < 0 || note > 127) return;
    int pick = -1;
    for (int c = 0; c < 6; ++c) if (voz[c].note == note) pick = c;
    if (pick < 0) { unsigned best = ~0u; for (int c = 0; c < 6; ++c) if (voz[c].note < 0 && voz[c].stamp < best) { best = voz[c].stamp; pick = c; } }
    if (pick < 0) { unsigned best = ~0u; for (int c = 0; c < 6; ++c) if (voz[c].stamp < best) { best = voz[c].stamp; pick = c; } }
    ym2612_write(&chip, 0, 0x28, keyByte(pick));
    voz[pick].note = note; voz[pick].stamp = ++stampCounter; voz[pick].off = dur;
    afinar(pick);
    ym2612_write(&chip, 0, 0x28, 0xF0 | keyByte(pick));
}

static void notaOff(int note)
{
    for (int c = 0; c < 6; ++c)
        if (voz[c].note == note) { ym2612_write(&chip, 0, 0x28, keyByte(c)); voz[c].note = -1; voz[c].stamp = ++stampCounter; }
}

static void silencio(void) { for (int c = 0; c < 6; ++c) { ym2612_write(&chip, 0, 0x28, keyByte(c)); voz[c].note = -1; } }

/* ---------------------------------------------------------------- MIDI (secuenciador de ALSA) */
static snd_seq_t *seq;
static int miPuerto, miCliente;
static char midiNombres[256];

/* Conecta al sinte todo lo que mande MIDI (teclados USB, controladores) y anota sus nombres. */
static int vigilarMidi(void)
{
    char nombres[256] = "";
    snd_seq_client_info_t *ci; snd_seq_port_info_t *pi;
    snd_seq_client_info_alloca(&ci); snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0)
    {
        const int c = snd_seq_client_info_get_client(ci);
        if (c == 0 || c == miCliente || c == SND_SEQ_CLIENT_SYSTEM) continue;
        const char *nom = snd_seq_client_info_get_name(ci);
        if (strstr(nom, "Midi Through")) continue;
        snd_seq_port_info_set_client(pi, c);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0)
        {
            const unsigned need = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
            if ((snd_seq_port_info_get_capability(pi) & need) != need) continue;
            snd_seq_connect_from(seq, miPuerto, c, snd_seq_port_info_get_port(pi));   /* si ya estaba, falla sin problema */
            if (!strstr(nombres, nom) && strlen(nombres) + strlen(nom) + 3 < sizeof nombres)
            { if (*nombres) strcat(nombres, ", "); strcat(nombres, nom); }
        }
    }
    const int cambio = strcmp(nombres, midiNombres) != 0;
    strcpy(midiNombres, nombres);
    return cambio;
}

static int pedidoSonido = -1;
static void leerMidi(void)
{
    snd_seq_event_t *ev;
    while (snd_seq_event_input_pending(seq, 1) > 0 && snd_seq_event_input(seq, &ev) >= 0)
    {
        switch (ev->type)
        {
            case SND_SEQ_EVENT_NOTEON:
                if (ev->data.note.velocity) notaOn(ev->data.note.note, -1); else notaOff(ev->data.note.note);
                break;
            case SND_SEQ_EVENT_NOTEOFF: notaOff(ev->data.note.note); break;
            case SND_SEQ_EVENT_PITCHBEND:
                bend = ev->data.control.value / 8192.0f * 2.0f;
                for (int c = 0; c < 6; ++c) if (voz[c].note >= 0) afinar(c);
                break;
            case SND_SEQ_EVENT_PGMCHANGE: pedidoSonido = ev->data.control.value % NPRESETS; break;
            case SND_SEQ_EVENT_CONTROLLER:
                if (ev->data.control.param == 120 || ev->data.control.param == 123) silencio();
                break;
            default: break;
        }
    }
}

/* ---------------------------------------------------------------- pantalla y teclado */
static struct termios termOriginal;
static void restaurarTerminal(void) { tcsetattr(0, TCSANOW, &termOriginal); printf("\033[0m\033[?25h\n"); fflush(stdout); }
static void terminalCruda(void)
{
    tcgetattr(0, &termOriginal);
    atexit(restaurarTerminal);
    struct termios t = termOriginal;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    printf("\033[?25l");
}

static int tocando = 0;   /* 0 = menú, 1 = tocar con el teclado de la netbook */

static void dibujar(void)
{
    struct winsize ws = { 0 };
    ioctl(1, TIOCGWINSZ, &ws);
    int alto = ws.ws_row ? ws.ws_row : 25;
    int filas = alto - 10; if (filas < 3) filas = 3;
    int desde = sonido - filas / 2;
    if (desde + filas > NPRESETS) desde = NPRESETS - filas;
    if (desde < 0) desde = 0;
    char barra[11]; for (int i = 0; i < 10; ++i) barra[i] = i < volumen ? '#' : '.'; barra[10] = 0;

    printf("\033[H\033[2J\n  \033[1;33mSINTE FM 16 BIT\033[0m - DecimuLabs");
    if (tocando) printf("      \033[7m TOCANDO CON EL TECLADO - octava %d \033[0m", octava);
    printf("\n\n  Volumen [%s]   DAC MD1: %s   MIDI: %s\n\n", barra, ladder ? "si" : "no",
           *midiNombres ? midiNombres : "ninguno (enchufá un teclado USB)");
    const char *catAnterior = desde > 0 ? PRESETS[desde - 1].cat : "";
    for (int i = desde; i < desde + filas && i < NPRESETS; ++i)
    {
        const int nuevaCat = strcmp(PRESETS[i].cat, catAnterior) != 0;
        catAnterior = PRESETS[i].cat;
        if (i == sonido) printf("\033[7m  > ");
        else printf("    ");
        printf("%2d  %-24s", i + 1, PRESETS[i].name);
        printf("\033[0m");
        if (nuevaCat) printf("  \033[36m%s\033[0m", PRESETS[i].cat);
        printf("\n");
    }
    printf("\n");
    if (tocando)
    {
        printf("  Notas: Z S X D C V G B H N J M  y  Q 2 W 3 E R 5 T 6 Y 7 U I\n");
        printf("  Flechas: arriba/abajo sonido, izq/der octava   +/- volumen   Espacio silencio   Tab volver\n");
    }
    else
    {
        printf("  Flechas: arriba/abajo sonido, izq/der otra familia   +/- volumen   Enter probar   d DAC\n");
        printf("  Tab tocar con el teclado   Espacio silencio   a apagar   q ir al sistema\n");
    }
    fflush(stdout);
}

static int teclaNota(int k)
{
    static const char *abajo = "zsxdcvgbhnjm,", *arriba = "q2w3er5t6y7ui9o0p";
    const char *p;
    if (k && (p = strchr(abajo, k))) return (int) (p - abajo);
    if (k && (p = strchr(arriba, k))) return 12 + (int) (p - arriba);
    return -1;
}

static void elegir(int idx) { sonido = (idx % NPRESETS + NPRESETS) % NPRESETS; silencio(); cargarSonido(sonido); guardar(); }

static int saltarFamilia(int dir)
{
    int i = sonido;
    const char *cat = PRESETS[i].cat;
    if (dir > 0) { while (i < NPRESETS && !strcmp(PRESETS[i].cat, cat)) ++i; return i >= NPRESETS ? 0 : i; }
    while (i > 0 && !strcmp(PRESETS[i].cat, cat)) --i;          /* último de la familia anterior */
    if (!strcmp(PRESETS[i].cat, cat)) i = NPRESETS - 1;
    cat = PRESETS[i].cat;
    while (i > 0 && !strcmp(PRESETS[i - 1].cat, cat)) --i;      /* primero de esa familia */
    return i;
}

/* ---------------------------------------------------------------- principal */
static volatile sig_atomic_t salir = 0;
static void alSalir(int s) { (void) s; salir = 1; }

static snd_pcm_t *abrirAudio(const char *dev)
{
    snd_pcm_t *pcm;
    if (snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0) return NULL;
    const unsigned lat = (unsigned) (periodo * 3 * 1000000.0 / RATE);
    if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, RATE, 1, lat) < 0) { snd_pcm_close(pcm); return NULL; }
    return pcm;
}

int main(void)
{
    cargar();
    signal(SIGINT, alSalir); signal(SIGTERM, alSalir);
    printf("\n  SINTE FM 16 BIT - DecimuLabs\n\n  Arrancando el sonido...\n"); fflush(stdout);

    snd_pcm_t *pcm = abrirAudio(audioDev);
    if (!pcm && strcmp(audioDev, "plughw:0")) { pcm = abrirAudio("plughw:0"); if (pcm) strcpy(audioDev, "plughw:0"); }
    if (!pcm)
    {
        printf("\n  No pude abrir la placa de sonido (%s).\n  Corré 'aplay -l' y poné la correcta en ~/.sintefm, por ejemplo AUDIO_DEV=plughw:1\n\n", audioDev);
        return 10;
    }
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) seq = NULL;
    if (seq)
    {
        snd_seq_set_client_name(seq, "Sinte FM");
        miCliente = snd_seq_client_id(seq);
        miPuerto = snd_seq_create_simple_port(seq, "Sinte FM", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                             SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTHESIZER | SND_SEQ_PORT_TYPE_APPLICATION);
        vigilarMidi();
    }

    ym2612_reset(&chip, RATE);
    for (int c = 0; c < 6; ++c) voz[c].note = -1;
    cargarSonido(sonido);
    guardar();
    terminalCruda();
    dibujar();

    const int n = periodo;
    float *L = malloc(sizeof(float) * n), *R = malloc(sizeof(float) * n);
    short *buf = malloc(sizeof(short) * 2 * n);
    long cuadros = 0;
    long demo[4] = { -1, -1, -1, -1 };   /* acorde de prueba: muestras hasta que entra cada nota */
    int codigo = 0;

    while (!salir)
    {
        /* teclas */
        unsigned char k[8];
        int got = (int) read(0, k, sizeof k);
        int redibujar = 0;
        if (got > 0)
        {
            redibujar = 1;
            if (k[0] == 27 && got >= 3 && k[1] == '[')
            {
                if (k[2] == 'A') elegir(sonido - 1);
                else if (k[2] == 'B') elegir(sonido + 1);
                else if (k[2] == 'C') { if (tocando) { if (octava < 7) ++octava; guardar(); } else elegir(saltarFamilia(1)); }
                else if (k[2] == 'D') { if (tocando) { if (octava > 1) --octava; guardar(); } else elegir(saltarFamilia(-1)); }
            }
            else if (k[0] == 27) tocando = 0;
            else if (k[0] == '\t') tocando = !tocando;
            else if (k[0] == ' ') { silencio(); redibujar = 0; }
            else if (k[0] == '+' || k[0] == '=') { if (volumen < 10) ++volumen; guardar(); }
            else if (k[0] == '-') { if (volumen > 1) --volumen; guardar(); }
            else if (tocando)
            {
                const int t = teclaNota(k[0] | 0x20);
                if (t >= 0) notaOn(12 * (octava + 1) + t, RATE * 6 / 10);
                redibujar = 0;
            }
            else if (k[0] == '\n' || k[0] == '\r')
            {
                for (int i = 0; i < 4; ++i) demo[i] = (long) i * RATE * 15 / 100;
                redibujar = 0;
            }
            else if (k[0] == 'd' || k[0] == 'D') { ladder = !ladder; ym2612_ladder(&chip, ladder); guardar(); }
            else if (k[0] == 'q' || k[0] == 'Q') { codigo = 10; break; }
            else if (k[0] == 'a' || k[0] == 'A')
            {
                printf("\n  Apagar la netbook? (s/n) "); fflush(stdout);
                unsigned char r = 0;
                while (read(0, &r, 1) <= 0) usleep(20000);
                if (r == 's' || r == 'S')
                {
                    printf("\033[H\033[2J\n  Apagando...\n"); fflush(stdout);
                    restaurarTerminal();
                    if (system("sudo -n /sbin/poweroff") == 0) sleep(60);
                    printf("  No pude apagar (falta permiso). Mantené apretado el botón de encendido.\n"); sleep(3);
                    terminalCruda();
                }
            }
            else redibujar = 0;
        }

        /* MIDI */
        if (seq)
        {
            leerMidi();
            if (pedidoSonido >= 0) { elegir(pedidoSonido); pedidoSonido = -1; redibujar = 1; }
            if (++cuadros % (2 * RATE / n) == 0 && vigilarMidi()) redibujar = 1;
        }
        if (redibujar) dibujar();

        /* acorde de prueba y notas con duración fija */
        static const int acorde[4] = { 60, 64, 67, 72 };
        for (int i = 0; i < 4; ++i)
            if (demo[i] >= 0 && (demo[i] -= n) < 0) notaOn(acorde[i], RATE * 9 / 10 - (long) i * RATE * 15 / 100);
        for (int c = 0; c < 6; ++c)
            if (voz[c].note >= 0 && voz[c].off >= 0 && (voz[c].off -= n) < 0) notaOff(voz[c].note);

        /* audio */
        ym2612_render(&chip, L, R, n);
        const float g = powf(10.0f, (9.0f + (volumen - 7) * 3.0f) / 20.0f);
        for (int i = 0; i < n; ++i)
        {
            float a = L[i] * g, b = R[i] * g;
            a = a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a;
            b = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;
            buf[2 * i] = (short) (a * 32767.0f); buf[2 * i + 1] = (short) (b * 32767.0f);
        }
        snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf, (snd_pcm_uframes_t) n);
        if (w < 0) snd_pcm_recover(pcm, (int) w, 1);
    }

    silencio();
    snd_pcm_drop(pcm);
    snd_pcm_close(pcm);
    if (seq) snd_seq_close(seq);
    guardar();
    return codigo;
}
