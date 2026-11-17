/* ym2612.c — motor del chip de sonido FM de la Mega Drive (YM2612), DecimuLabs.
   Escrito de cero. Sin libc: sirve igual para WebAssembly, VST y Linux.
   Se maneja SOLO escribiendo registros, como en la consola real.        */
#include "ym2612.h"
#include "ym_tables.h"

typedef unsigned int u32; typedef int i32; typedef unsigned char u8;
#define CHIP_RATE 53267.0 /* 7670453 / 144 (NTSC) */

enum { EG_ATT, EG_DEC, EG_SUS, EG_REL };

static const u8 DT_TAB[4][32] = {
 {0},
 {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,8,8},
 {1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,16,16,16},
 {2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20,22,22,22,22}};
static const u8 EG_STEP[4][8] = {
 {0,1,0,1,0,1,0,1},{0,1,0,1,1,1,0,1},{0,1,1,1,0,1,1,1},{0,1,1,1,1,1,1,1}};
static const u8 LFO_DIV[8] = {108,77,71,67,62,44,8,5};
/* profundidad del vibrato (PMS) → factor */
static const i32 PMS_K[8] = {0,128,254,379,530,758,1516,3031};
static const u8 SLOT_OF_REG[4] = {0,2,1,3}; /* +0=S1 +4=S3 +8=S2 +12=S4 */
static const u8 CH3_SLOT[3] = {1, 2, 0};    /* S1←A9/AD, S2←AA/AE, S3←A8/AC */

static int keycode(int fnum, int block) {
  int f11 = (fnum >> 10) & 1, f10 = (fnum >> 9) & 1, f9 = (fnum >> 8) & 1, f8 = (fnum >> 7) & 1;
  return (block << 2) | (f11 << 1) | ((f11 && (f10 | f9 | f8)) || (!f11 && f10 && f9 && f8));
}

static void keyOp(YMOp *o, int on) {
  if (on && !o->key) {
    o->phase = 0; o->state = EG_ATT;
    o->ssginv = (o->ssg & 8) ? ((o->ssg >> 2) & 1) : 0;
  } else if (!on && o->key) {
    if (o->ssginv) o->level = (0x200 - o->level) & 0x3FF;
    o->ssginv = 0; o->state = EG_REL;
  }
  o->key = on;
}

void ym2612_write(YM *y, int port, int a, int d) {
  int c, s; YMOp *o; d &= 0xFF; a &= 0xFF; port &= 1;
  y->regs[(port << 8) | a] = (u8)d;
  if (port == 0 && a < 0x30) {
    switch (a) {
      case 0x22: y->lfoOn = (d >> 3) & 1; y->lfoFreq = d & 7; if (!y->lfoOn) y->lfoCnt = 0; break;
      case 0x27: y->ch3mode = (d >> 6) & 3; break;
      case 0x28: c = d & 3; if (c == 3) return; if (d & 4) c += 3;
        for (s = 0; s < 4; s++) keyOp(&y->ch[c].op[s], (d >> (4 + s)) & 1); break;
      case 0x2A: y->dacVal = d; break;
      case 0x2B: y->dacOn = d >> 7; break;
    }
    return;
  }
  c = a & 3; if (c == 3) return; c += port * 3;
  if (a >= 0x30 && a < 0xA0) {
    o = &y->ch[c].op[SLOT_OF_REG[(a >> 2) & 3]];
    switch (a & 0xF0) {
      case 0x30: o->dt = (d >> 4) & 7; o->mul = d & 15; break;
      case 0x40: o->tl = d & 127; break;
      case 0x50: o->ks = d >> 6; o->ar = d & 31; break;
      case 0x60: o->am = d >> 7; o->d1r = d & 31; break;
      case 0x70: o->d2r = d & 31; break;
      case 0x80: o->sl = d >> 4; o->rr = d & 15; break;
      case 0x90: o->ssg = d & 15; break;
    }
    return;
  }
  switch (a & 0xFC) {
    case 0xA4: y->fnLatch[port] = d; break;
    case 0xA0: y->ch[c].fnum = ((y->fnLatch[port] & 7) << 8) | d; y->ch[c].block = (y->fnLatch[port] >> 3) & 7;
      y->ch[c].kc = keycode(y->ch[c].fnum, y->ch[c].block); break;
    case 0xAC: if (port == 0) y->f3latch = d; break;
    case 0xA8: if (port == 0) { int i = a & 3; if (i < 3) { y->f3num[i] = ((y->f3latch & 7) << 8) | d; y->f3block[i] = (y->f3latch >> 3) & 7; } } break;
    case 0xB0: y->ch[c].alg = d & 7; y->ch[c].fb = (d >> 3) & 7; break;
    case 0xB4: y->ch[c].l = d >> 7; y->ch[c].r = (d >> 6) & 1; y->ch[c].ams = (d >> 4) & 3; y->ch[c].pms = d & 7; break;
  }
}

/* triángulos del LFO */
static int lfoAM(const YM *y) { int t = y->lfoCnt & 127; return (t < 64 ? 63 - t : t - 64) << 1; } /* 0..126 */
static int lfoPM(const YM *y) { int t = y->lfoCnt & 127; return t < 32 ? t : t < 96 ? 64 - t : t - 128; } /* -32..32 */

static void egTick(YM *y, YMOp *o, int kc) {
  int r, eff, inc = 0, sl = o->sl == 15 ? 0x3E0 : o->sl << 5;
  u32 egCnt = y->egCnt;
  if (o->state == EG_ATT && o->level == 0) o->state = EG_DEC;
  if (o->state == EG_DEC && o->level >= sl) o->state = EG_SUS;
  r = o->state == EG_ATT ? o->ar : o->state == EG_DEC ? o->d1r : o->state == EG_SUS ? o->d2r : o->rr * 2 + 1;
  eff = r ? 2 * r + (kc >> (3 - o->ks)) : 0;
  if (eff > 63) eff = 63;
  if (eff >= 60) inc = 8;
  else if (eff >= 48) inc = EG_STEP[eff & 3][egCnt & 7] << ((eff >> 2) - 12);
  else if (eff >= 2) { int sh = 11 - (eff >> 2); if (!(egCnt & ((1u << sh) - 1))) inc = EG_STEP[eff & 3][(egCnt >> sh) & 7]; }
  if (o->state == EG_ATT) {
    if (eff >= 62) o->level = 0;
    else if (inc) { o->level += ((~o->level) * inc) >> 4; if (o->level < 0) o->level = 0; }
    return;
  }
  if ((o->ssg & 8) && o->state != EG_REL) {
    if (o->level < 0x200) o->level += inc << 2;
    if (o->level >= 0x200) {
      if (o->ssg & 1) { /* hold */
        if ((o->ssg & 2) && !(o->ssg & 0x10)) { o->ssginv ^= 1; o->ssg |= 0x10; }
        o->level = 0x200;
      } else {
        if (o->ssg & 2) o->ssginv ^= 1; else o->phase = 0;
        o->state = EG_ATT;
      }
    }
    return;
  }
  o->level += inc; if (o->level > 0x3FF) o->level = 0x3FF;
}

static i32 opCalc(YMOp *o, u32 inc, int mod, int env) {
  int ph, q, att; i32 v;
  ph = ((o->phase >> 10) + mod) & 0x3FF;
  o->phase = (o->phase + inc) & 0xFFFFF;
  q = ph & 0x100 ? (~ph) & 0xFF : ph & 0xFF;
  att = YM_LOGSIN[q] + (env << 2);
  if (att > 0x1FFF) v = 0;
  else v = ((YM_EXP[(att & 0xFF) ^ 0xFF] | 0x400) << 2) >> (att >> 8);
  return ph & 0x200 ? -v : v;
}

static u32 phaseInc(const YM *y, const YMOp *o, int fnum, int block, int pms) {
  int kc, d; u32 b;
  if (pms && y->lfoOn) fnum += (i32)(((long long)fnum * lfoPM(y) * PMS_K[pms]) >> 21);
  fnum &= 0xFFF;
  kc = keycode(fnum & 0x7FF, block);
  b = ((u32)fnum << block) >> 1;
  d = DT_TAB[o->dt & 3][kc];
  if (o->dt & 4) b -= d; else b += d;
  b &= 0x1FFFF;
  return (o->mul ? b * o->mul : b >> 1) & 0xFFFFF;
}

static void chipSample(YM *y, float *L, float *R) {
  int c, s, sl = 0, sr = 0;
  int am = y->lfoOn ? lfoAM(y) : 0;
  if (++y->egDiv >= 3) {
    y->egDiv = 0; y->egCnt = (y->egCnt + 1) & 0xFFF; if (!y->egCnt) y->egCnt = 1;
    for (c = 0; c < 6; c++) for (s = 0; s < 4; s++) {
      int kc = y->ch[c].kc;
      if (c == 2 && y->ch3mode && s < 3) kc = keycode(y->f3num[CH3_SLOT[s]], y->f3block[CH3_SLOT[s]]);
      egTick(y, &y->ch[c].op[s], kc);
    }
  }
  if (y->lfoOn && ++y->lfoDiv >= LFO_DIV[y->lfoFreq]) { y->lfoDiv = 0; y->lfoCnt = (y->lfoCnt + 1) & 127; }

  for (c = 0; c < 6; c++) {
    YMCh *k = &y->ch[c]; i32 o0, o1, o2, o3, out, m; int env[4]; u32 inc[4];
    static const int amShift[4] = {8, 3, 1, 0};
    for (s = 0; s < 4; s++) {
      YMOp *o = &k->op[s]; int fn = k->fnum, bl = k->block, e;
      if (c == 2 && y->ch3mode && s < 3) { fn = y->f3num[CH3_SLOT[s]]; bl = y->f3block[CH3_SLOT[s]]; }
      inc[s] = phaseInc(y, o, fn, bl, k->pms);
      e = o->level;
      if (o->ssginv && o->state != EG_REL) e = (0x200 - e) & 0x3FF;
      e += o->tl << 3;
      if (o->am && k->ams) e += am >> amShift[k->ams];
      env[s] = e > 0x3FF ? 0x3FF : e;
    }
    m = k->fb ? (k->op[0].out + k->op[0].out1) >> (10 - k->fb) : 0;
    k->op[0].out1 = k->op[0].out;
    o0 = k->op[0].out = opCalc(&k->op[0], inc[0], m, env[0]);
    switch (k->alg) {
      case 0: o1 = opCalc(&k->op[1], inc[1], o0 >> 1, env[1]); o2 = opCalc(&k->op[2], inc[2], o1 >> 1, env[2]);
              out = opCalc(&k->op[3], inc[3], o2 >> 1, env[3]); break;
      case 1: o1 = opCalc(&k->op[1], inc[1], 0, env[1]); o2 = opCalc(&k->op[2], inc[2], (o0 + o1) >> 1, env[2]);
              out = opCalc(&k->op[3], inc[3], o2 >> 1, env[3]); break;
      case 2: o1 = opCalc(&k->op[1], inc[1], 0, env[1]); o2 = opCalc(&k->op[2], inc[2], o1 >> 1, env[2]);
              out = opCalc(&k->op[3], inc[3], (o0 + o2) >> 1, env[3]); break;
      case 3: o1 = opCalc(&k->op[1], inc[1], o0 >> 1, env[1]); o2 = opCalc(&k->op[2], inc[2], 0, env[2]);
              out = opCalc(&k->op[3], inc[3], (o1 + o2) >> 1, env[3]); break;
      case 4: o1 = opCalc(&k->op[1], inc[1], o0 >> 1, env[1]); o2 = opCalc(&k->op[2], inc[2], 0, env[2]);
              o3 = opCalc(&k->op[3], inc[3], o2 >> 1, env[3]); out = o1 + o3; break;
      case 5: o1 = opCalc(&k->op[1], inc[1], o0 >> 1, env[1]); o2 = opCalc(&k->op[2], inc[2], o0 >> 1, env[2]);
              o3 = opCalc(&k->op[3], inc[3], o0 >> 1, env[3]); out = o1 + o2 + o3; break;
      case 6: o1 = opCalc(&k->op[1], inc[1], o0 >> 1, env[1]); o2 = opCalc(&k->op[2], inc[2], 0, env[2]);
              o3 = opCalc(&k->op[3], inc[3], 0, env[3]); out = o1 + o2 + o3; break;
      default: o1 = opCalc(&k->op[1], inc[1], 0, env[1]); o2 = opCalc(&k->op[2], inc[2], 0, env[2]);
              o3 = opCalc(&k->op[3], inc[3], 0, env[3]); out = o0 + o1 + o2 + o3; break;
    }
    if (out > 8191) out = 8191;
    if (out < -8192) out = -8192;
    if (c == 5 && y->dacOn) out = (y->dacVal - 128) << 6;
    out >>= 5; /* DAC de 9 bits */
    if (y->ladder && out) out += out > 0 ? 4 : -4; /* distorsión de la Mega Drive 1 */
    k->out = out;
    if (k->l) sl += out;
    if (k->r) sr += out;
  }
  *L = sl / 1536.0f; *R = sr / 1536.0f; /* 6 canales al máximo = 1.0 */
}

void ym2612_reset(YM *y, double rate) {
  int i, c;
  u8 *p = (u8 *)y; for (i = 0; i < (int)sizeof *y; i++) p[i] = 0;
  for (c = 0; c < 6; c++) { y->ch[c].l = y->ch[c].r = 1; for (i = 0; i < 4; i++) { y->ch[c].op[i].level = 0x3FF; y->ch[c].op[i].state = EG_REL; } }
  y->outRate = rate > 0 ? rate : 48000; y->egCnt = 1; y->dacVal = 0x80; y->ladder = 1;
}

void ym2612_ladder(YM *y, int on) { y->ladder = on; }

/* n muestras a la frecuencia de salida, con interpolación lineal desde los 53267 Hz del chip */
void ym2612_render(YM *y, float *L, float *R, int n) {
  double step = CHIP_RATE / y->outRate; int i;
  for (i = 0; i < n; i++) {
    float l, r;
    y->frac += step;
    while (y->frac >= 1.0) { y->frac -= 1.0; y->prevL = y->curL; y->prevR = y->curR; chipSample(y, &y->curL, &y->curR); }
    l = y->prevL + (y->curL - y->prevL) * (float)y->frac;
    r = y->prevR + (y->curR - y->prevR) * (float)y->frac;
    L[i] = l; R[i] = r;
  }
}
