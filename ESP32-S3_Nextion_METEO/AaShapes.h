#pragma once
// AaShapes.h - wygladzane (antyaliasowane) ksztalty dla stacji pogody.
// Osobny plik (a nie w .ino), bo Arduino generuje prototypy funkcji szkicu PRZED definicjami struktur,
// co psulo kompilacje funkcji przyjmujacych AaShape / AaLayer.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "BoardConfig.h"      // LCD_H_RES, LCD_V_RES

extern Arduino_GFX *gfx;      // definicja w szkicu

// =====================================================================
//  Wygladzane ksztalty (antyaliasing) - kola, pierscienie, kreski, trojkaty, luki
// ---------------------------------------------------------------------
// Biblioteczne fillCircle/drawLine/fillTriangle/fillArc zapalaja piksel albo nie, stad "schodki".
// Tu dla kazdego piksela liczymy odleglosc od krawedzi ksztaltu (SDF, ujemna = w srodku) i z niej
// pokrycie piksela 0..1; kolor mieszamy z tlem. Rysunek sklada sie z WARSTW (kazda: suma ksztaltow
// w jednym kolorze, opcjonalnie przycieta klinem katowym); warstwy mieszamy w pamieci piksel po pikselu,
// wiec nachodzace elementy (np. grot na trzonie strzalki, strzalka na pierscieniu rozy) nie maja
// ciemnych szwow. Wyswietlacz nie pozwala odczytac tla, wiec "bg" musi byc kolorem, na ktorym lezy
// rysunek; piksele poza ksztaltami nie sa ruszane.
// Wspolrzedne: srodki pikseli w liczbach calkowitych (jak w Adafruit GFX). Promienie wypelnien sa
// o 0,5 px wieksze niz w fillCircle(r), zeby rozmiar zostal taki sam jak dotad.
enum : uint8_t { AA_DISC, AA_RING, AA_CAPSULE, AA_TRI, AA_ELLIPSE_STROKE };

struct AaShape {
  uint8_t type;
  float p[6];
  // AA_DISC:           cx, cy, r
  // AA_RING:           cx, cy, rSrodkowy, polGrubosci
  // AA_CAPSULE:        x0, y0, x1, y1, r            (kreska o grubosci 2r z zaokraglonymi koncami)
  // AA_TRI:            x0, y0, x1, y1, x2, y2       (+0,5 px dookola - jak fillTriangle)
  // AA_ELLIPSE_STROKE: cx, cy, rx, ry, polGrubosci  (obrys elipsy)
};

struct AaLayer {
  const AaShape *s;
  uint8_t n;
  uint16_t col;
  bool clip;                 // przyciecie klinem: katy a0..a1 [stopnie, 0 = w prawo, rosna zgodnie
  float ccx, ccy, a0, a1;    // z ruchem wskazowek zegara - jak fillArc], rozpietosc <= 180
};

static inline AaShape aaDisc(float cx, float cy, float r) { return {AA_DISC, {cx, cy, r, 0, 0, 0}}; }
static inline AaShape aaRing(float cx, float cy, float rMid, float half) { return {AA_RING, {cx, cy, rMid, half, 0, 0}}; }
static inline AaShape aaCapsule(float x0, float y0, float x1, float y1, float r) { return {AA_CAPSULE, {x0, y0, x1, y1, r, 0}}; }
static inline AaShape aaTri(float x0, float y0, float x1, float y1, float x2, float y2) { return {AA_TRI, {x0, y0, x1, y1, x2, y2}}; }
static inline AaShape aaEllipse(float cx, float cy, float rx, float ry, float half) { return {AA_ELLIPSE_STROKE, {cx, cy, rx, ry, half, 0}}; }
static inline AaLayer aaLayer(const AaShape *s, uint8_t n, uint16_t col) { return {s, n, col, false, 0, 0, 0, 0}; }
static inline AaLayer aaLayerClip(const AaShape *s, uint8_t n, uint16_t col, float cx, float cy, float a0, float a1) {
  return {s, n, col, true, cx, cy, a0, a1};
}

// Mieszanie RGB565: t = 0 -> a, t = 1 -> b (z zaokragleniem; dziala tez przy rozjasnianiu i przyciemnianiu)
static inline uint16_t aaMix(uint16_t a, uint16_t b, float t) {
  if (t >= 1.0f) return b;
  if (t <= 0.0f) return a;
  int r = (int)((a >> 11) + (((int)(b >> 11) - (int)(a >> 11)) * t) + 0.5f);
  int g = (int)(((a >> 5) & 0x3F) + (((int)((b >> 5) & 0x3F) - (int)((a >> 5) & 0x3F)) * t) + 0.5f);
  int bl = (int)((a & 0x1F) + (((int)(b & 0x1F) - (int)(a & 0x1F)) * t) + 0.5f);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static float aaSegDist(float px, float py, float x0, float y0, float x1, float y1) {
  float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0;
  float l2 = vx * vx + vy * vy;
  float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
  if (t < 0) t = 0; else if (t > 1) t = 1;
  float dx = wx - vx * t, dy = wy - vy * t;
  return sqrtf(dx * dx + dy * dy);
}

// Dokladna odleglosc od trojkata (ze znakiem), wg Inigo Quileza
static float aaSdTri(float px, float py, const float *p) {
  float e0x = p[2] - p[0], e0y = p[3] - p[1], e1x = p[4] - p[2], e1y = p[5] - p[3], e2x = p[0] - p[4], e2y = p[1] - p[5];
  float v0x = px - p[0], v0y = py - p[1], v1x = px - p[2], v1y = py - p[3], v2x = px - p[4], v2y = py - p[5];
  auto cl = [](float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); };
  float t0 = cl((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y));
  float t1 = cl((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y));
  float t2 = cl((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y));
  float q0x = v0x - e0x * t0, q0y = v0y - e0y * t0;
  float q1x = v1x - e1x * t1, q1y = v1y - e1y * t1;
  float q2x = v2x - e2x * t2, q2y = v2y - e2y * t2;
  float s = (e0x * e2y - e0y * e2x) < 0 ? -1.0f : 1.0f;
  float d0 = q0x * q0x + q0y * q0y, s0 = s * (v0x * e0y - v0y * e0x);
  float d1 = q1x * q1x + q1y * q1y, s1 = s * (v1x * e1y - v1y * e1x);
  float d2 = q2x * q2x + q2y * q2y, s2 = s * (v2x * e2y - v2y * e2x);
  float d = d0, sg = s0;
  if (d1 < d) d = d1;
  if (d2 < d) d = d2;
  sg = s0 < s1 ? s0 : s1;
  if (s2 < sg) sg = s2;
  return -sqrtf(d) * (sg < 0 ? -1.0f : 1.0f);
}

static float aaSd(const AaShape &s, float px, float py) {
  const float *p = s.p;
  switch (s.type) {
    case AA_DISC: { float dx = px - p[0], dy = py - p[1]; return sqrtf(dx * dx + dy * dy) - p[2]; }
    case AA_RING: { float dx = px - p[0], dy = py - p[1]; return fabsf(sqrtf(dx * dx + dy * dy) - p[2]) - p[3]; }
    case AA_CAPSULE: return aaSegDist(px, py, p[0], p[1], p[2], p[3]) - p[4];
    case AA_TRI: return aaSdTri(px, py, p) - 0.5f;
    case AA_ELLIPSE_STROKE: {
      // przyblizona odleglosc od elipsy: f / |grad f| (dokladna przy samej krzywej - tylko tam jest potrzebna)
      float dx = px - p[0], dy = py - p[1], rx2 = p[2] * p[2], ry2 = p[3] * p[3];
      float f = dx * dx / rx2 + dy * dy / ry2 - 1.0f;
      float gx = 2.0f * dx / rx2, gy = 2.0f * dy / ry2;
      float g = sqrtf(gx * gx + gy * gy);
      if (g < 1e-6f) return 1e6f;
      return fabsf(f / g) - p[4];
    }
  }
  return 1e6f;
}

static void aaBounds(const AaShape &s, float &x0, float &y0, float &x1, float &y1) {
  const float *p = s.p;
  float a0, b0, a1, b1;
  switch (s.type) {
    case AA_DISC: a0 = p[0] - p[2]; a1 = p[0] + p[2]; b0 = p[1] - p[2]; b1 = p[1] + p[2]; break;
    case AA_RING: a0 = p[0] - p[2] - p[3]; a1 = p[0] + p[2] + p[3]; b0 = p[1] - p[2] - p[3]; b1 = p[1] + p[2] + p[3]; break;
    case AA_CAPSULE: a0 = fminf(p[0], p[2]) - p[4]; a1 = fmaxf(p[0], p[2]) + p[4];
                     b0 = fminf(p[1], p[3]) - p[4]; b1 = fmaxf(p[1], p[3]) + p[4]; break;
    case AA_TRI: a0 = fminf(p[0], fminf(p[2], p[4])) - 0.5f; a1 = fmaxf(p[0], fmaxf(p[2], p[4])) + 0.5f;
                 b0 = fminf(p[1], fminf(p[3], p[5])) - 0.5f; b1 = fmaxf(p[1], fmaxf(p[3], p[5])) + 0.5f; break;
    default: a0 = p[0] - p[2] - p[4]; a1 = p[0] + p[2] + p[4]; b0 = p[1] - p[3] - p[4]; b1 = p[1] + p[3] + p[4]; break;
  }
  if (a0 < x0) x0 = a0;
  if (b0 < y0) y0 = b0;
  if (a1 > x1) x1 = a1;
  if (b1 > y1) y1 = b1;
}

// odleglosc od klina a0..a1 (rozpietosc <= 180 stopni) wokol (cx, cy); ujemna = w srodku
static inline float aaSdWedge(const AaLayer &L, float px, float py) {
  float dx = px - L.ccx, dy = py - L.ccy;
  float r0 = L.a0 * (float)PI / 180.0f, r1 = L.a1 * (float)PI / 180.0f;
  float s0 = -dx * sinf(r0) + dy * cosf(r0);     // > 0 po stronie rosnacych katow od a0
  float s1 = dx * sinf(r1) - dy * cosf(r1);      // > 0 po stronie malejacych katow od a1
  return -fminf(s0, s1);
}

inline void aaDraw(const AaLayer *L, int nL, uint16_t bg) {
  float fx0 = 1e9f, fy0 = 1e9f, fx1 = -1e9f, fy1 = -1e9f;
  for (int i = 0; i < nL; i++)
    for (int k = 0; k < L[i].n; k++) aaBounds(L[i].s[k], fx0, fy0, fx1, fy1);
  int x0 = max(0, (int)floorf(fx0) - 1), y0 = max(0, (int)floorf(fy0) - 1);
  int x1 = min(LCD_H_RES - 1, (int)ceilf(fx1) + 1), y1 = min(LCD_V_RES - 1, (int)ceilf(fy1) + 1);
  gfx->startWrite();
  for (int y = y0; y <= y1; y++) {
    int runX = -1;
    uint16_t runCol = 0;
    for (int x = x0; x <= x1 + 1; x++) {
      bool touched = false;
      uint16_t c = bg;
      if (x <= x1) {
        for (int i = 0; i < nL; i++) {
          float sd = 1e6f;
          for (int k = 0; k < L[i].n; k++) sd = fminf(sd, aaSd(L[i].s[k], (float)x, (float)y));
          if (L[i].clip) sd = fmaxf(sd, aaSdWedge(L[i], (float)x, (float)y));
          float cov = 0.5f - sd;
          if (cov <= 0.0f) continue;
          touched = true;
          c = aaMix(c, L[i].col, cov);
        }
      }
      // serie jednakowych pikseli jedna kreska (pelne wnetrza ksztaltow), krawedzie pojedynczo
      if (touched && runX >= 0 && c == runCol) continue;
      if (runX >= 0) gfx->writeFastHLine(runX, y, x - runX, runCol);
      runX = touched ? x : -1;
      runCol = c;
    }
  }
  gfx->endWrite();
}

// Pojedyncze ksztalty jednym kolorem (najczestszy przypadek)
inline void aaFillCircle(float cx, float cy, float r, uint16_t col, uint16_t bg) {   // r jak w fillCircle
  AaShape s = aaDisc(cx, cy, r + 0.5f);
  AaLayer l = aaLayer(&s, 1, col);
  aaDraw(&l, 1, bg);
}
inline void aaFillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t col, uint16_t bg) {
  AaShape s = aaTri(x0, y0, x1, y1, x2, y2);
  AaLayer l = aaLayer(&s, 1, col);
  aaDraw(&l, 1, bg);
}
