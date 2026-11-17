/* ym2612.h — motor del chip FM YM2612 (DecimuLabs). Cada YM es un chip independiente. */
#ifndef YM2612_H
#define YM2612_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  unsigned int phase; int level, state, key;
  int dt, mul, tl, ks, ar, d1r, d2r, rr, sl, am, ssg, ssginv;
  int out, out1;
} YMOp;

typedef struct { YMOp op[4]; int fnum, block, alg, fb, ams, pms, l, r, kc, out; } YMCh;

typedef struct {
  YMCh ch[6];
  int fnLatch[2], f3num[3], f3block[3], f3latch;
  int ch3mode, lfoOn, lfoFreq, lfoCnt, lfoDiv;
  int dacOn, dacVal, ladder;
  unsigned int egCnt; int egDiv;
  double outRate, frac;
  float prevL, prevR, curL, curR;
  unsigned char regs[512];
} YM;

void ym2612_reset(YM *y, double outRate);            /* deja el chip en silencio */
void ym2612_write(YM *y, int port, int addr, int data); /* escribe un registro (puerto 0 o 1) */
void ym2612_ladder(YM *y, int on);                    /* distorsión del DAC de la Mega Drive 1 */
void ym2612_render(YM *y, float *L, float *R, int n); /* n muestras a outRate (L y R pueden ser el mismo puntero si se quiere mono) */

#ifdef __cplusplus
}
#endif
#endif
