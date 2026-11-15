#pragma once
#include <Arduino_GFX_Library.h>
#include "Settings.h"
#include "WeatherIconsBmp.h"   // kolorowe ikony jako bitmapy (generowane)

// detail: skala "sztywnych" elementow (promienie, kreski) - 1.0 = bez zmian;
// mniejsza wartosc dla bardzo malych ikon (np. wiersze godzinowe).
inline void iconSun(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t sunColor, float detail = 1.0f) {
  gfx->fillCircle(cx, cy, r, sunColor);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4.0;
    int x1 = cx + cos(a) * (r + 6 * detail);
    int y1 = cy + sin(a) * (r + 6 * detail);
    int x2 = cx + cos(a) * (r + 16 * detail);
    int y2 = cy + sin(a) * (r + 16 * detail);
    gfx->drawLine(x1, y1, x2, y2, sunColor);
  }
}

// Ksiezyc w sierpie - pelne kolo, "odgryzione" przesunietym kolem w
// kolorze tla (najprostszy sposob na sierp bez prawdziwych operacji
// boolowskich na ksztaltach).
inline void iconMoon(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t moonColor, uint16_t bgColor) {
  gfx->fillCircle(cx, cy, r, moonColor);
  gfx->fillCircle(cx + r * 0.5, cy - r * 0.3, r * 0.85, bgColor);
}

inline void iconCloud(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t cloudColor) {
  gfx->fillCircle(cx - r * 0.6, cy, r * 0.7, cloudColor);
  gfx->fillCircle(cx + r * 0.5, cy - r * 0.2, r * 0.8, cloudColor);
  gfx->fillCircle(cx + r * 1.3, cy, r * 0.6, cloudColor);
  gfx->fillRect(cx - r * 1.1, cy, r * 2.4, r * 0.8, cloudColor);
}

// Czeste przejasnienia ("duze zachmurzenie"): duza chmura, zza ktorej wyglada niewielkie slonce/ksiezyc
// (widac tylko gorna, lewa czesc tarczy i kilka promieni). Rysujemy najpierw slonce, potem chmure.
inline void iconPeek(Arduino_GFX *gfx, int cx, int cy, int r, bool isNight, uint16_t sunColor, uint16_t moonColor,
                     uint16_t bgColor, uint16_t cloudColor, float detail) {
  int sx = cx - r * 0.55, sy = cy - r * 0.55, sr = r * 0.55;
  if (isNight) {
    iconMoon(gfx, sx, sy, sr, moonColor, bgColor);
  } else {
    gfx->fillCircle(sx, sy, sr, sunColor);
    for (int i = 3; i <= 7; i++) {                 // promienie tylko od lewej przez gore do prawej-gory
      float a = i * PI / 4.0;
      int x1 = sx + cos(a) * (sr + 5 * detail);
      int y1 = sy + sin(a) * (sr + 5 * detail);
      int x2 = sx + cos(a) * (sr + 13 * detail);
      int y2 = sy + sin(a) * (sr + 13 * detail);
      gfx->drawLine(x1, y1, x2, y2, sunColor);
    }
  }
  iconCloud(gfx, cx + r * 0.15, cy + r * 0.25, r * 0.95, cloudColor);
}

// ---------------------------------------------------------------------
//  Opady: kropla / platek / grad — rozmiary i rozstaw skalowane wzgledem
//  promienia ikony (k = r/22; dla r=22 wyglada tak samo jak dotychczas).
// ---------------------------------------------------------------------

// Kropla deszczu: dwie rownolegle, skosne kreski (~2px). lenFactor<1 = krotsza
// (mzawka), 1 = zwykly deszcz.
inline void dropAt(Arduino_GFX *gfx, float x, float y, float k, float lenFactor, uint16_t color) {
  int len = (int)(14 * k * lenFactor + 0.5f);
  if (len < 3) len = 3;
  int dx = (int)(5 * k * lenFactor + 0.5f);
  int xi = (int)x, yi = (int)y;
  gfx->drawLine(xi, yi, xi - dx, yi + len, color);
  gfx->drawLine(xi - 1, yi, xi - dx - 1, yi + len, color);
}

// Platek sniegu: gwiazdka z 4 kresek.
inline void flakeAt(Arduino_GFX *gfx, float x, float y, float k, uint16_t color) {
  int a = (int)(5 * k + 0.5f); if (a < 2) a = 2;
  int b = (int)(3 * k + 0.5f); if (b < 1) b = 1;
  int xi = (int)x, yi = (int)y;
  gfx->drawLine(xi - a, yi, xi + a, yi, color);
  gfx->drawLine(xi, yi - a, xi, yi + a, color);
  gfx->drawLine(xi - b, yi - b, xi + b, yi + b, color);
  gfx->drawLine(xi - b, yi + b, xi + b, yi - b, color);
}

// Ziarno gradu: pelne kolko.
inline void hailAt(Arduino_GFX *gfx, float x, float y, float k, uint16_t color) {
  int rad = (int)(2.5f * k + 0.5f); if (rad < 2) rad = 2;
  gfx->fillCircle((int)x, (int)y, rad, color);
}

enum PrecipKind : uint8_t { PRECIP_DROP, PRECIP_FLAKE, PRECIP_MIX };

// Chmurka + rzedy opadow pod nia. Pierwszy rzad ma perRow elementow, drugi
// (perRow-1) ulozonych "w szachownice" pomiedzy elementami powyzej.
// PRECIP_MIX = na przemian kropla i platek (deszcz ze sniegiem).
inline void iconPrecip(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t cloudColor,
                       uint16_t dropColor, uint16_t flakeColor,
                       PrecipKind kind, int perRow, int rows, float lenFactor) {
  float k = r / 22.0f;
  iconCloud(gfx, cx, cy - r * 0.3, r * 0.85, cloudColor);
  float spacing = 16 * k;
  for (int row = 0; row < rows; row++) {
    int count = (row % 2 == 0) ? perRow : perRow - 1;
    float startX = cx - spacing * (count - 1) / 2.0f;
    float y = cy + r * 0.5f + row * 10 * k;
    for (int i = 0; i < count; i++) {
      float x = startX + i * spacing;
      bool useFlake = (kind == PRECIP_FLAKE) || (kind == PRECIP_MIX && ((i + row) % 2 == 1));
      if (useFlake) flakeAt(gfx, x, y + 3 * k, k, flakeColor);
      else dropAt(gfx, x, y, k, lenFactor, dropColor);
    }
  }
}

// Slonce z chmura i kroplami (przelotne opady w ciagu dnia): slonce wyglada zza chmury z 3 krotkimi kroplami.
inline void iconSunShower(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t sunColor, uint16_t cloudColor,
                          uint16_t dropColor, uint16_t flakeColor, float detail) {
  int sx = cx - r * 0.65, sy = cy - r * 0.75, sr = r * 0.55;
  gfx->fillCircle(sx, sy, sr, sunColor);
  for (int i = 3; i <= 7; i++) {
    float a = i * PI / 4.0;
    gfx->drawLine(sx + cos(a) * (sr + 4 * detail), sy + sin(a) * (sr + 4 * detail),
                  sx + cos(a) * (sr + 11 * detail), sy + sin(a) * (sr + 11 * detail), sunColor);
  }
  iconPrecip(gfx, cx + r * 0.15, cy + r * 0.1, r, cloudColor, dropColor, flakeColor, PRECIP_DROP, 3, 1, 0.55f);
}

inline void iconStorm(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t cloudColor, uint16_t boltColor, float detail = 1.0f) {
  iconCloud(gfx, cx, cy - r * 0.3, r * 0.85, cloudColor);
  int px[5] = {(int)(cx - 4 * detail), (int)(cx + 7 * detail), (int)(cx - 2 * detail), (int)(cx + 10 * detail), (int)(cx - 7 * detail)};
  int py[5] = {cy + r * 0.4, cy + r * 0.4, cy + r * 0.7, cy + r * 0.7, cy + r * 1.1};
  for (int i = 0; i < 4; i++) {
    gfx->drawLine(px[i], py[i], px[i + 1], py[i + 1], boltColor);
  }
}

// Burza z gradem: ta sama chmura i blyskawica + ziarna gradu po bokach.
inline void iconStormHail(Arduino_GFX *gfx, int cx, int cy, int r, uint16_t cloudColor,
                          uint16_t boltColor, uint16_t hailColor, float detail = 1.0f) {
  iconStorm(gfx, cx, cy, r, cloudColor, boltColor, detail);
  float k = r / 22.0f;
  float y0 = cy + r * 0.55f;
  hailAt(gfx, cx - 20 * k, y0 + 1 * k, k, hailColor);
  hailAt(gfx, cx - 14 * k, y0 + 11 * k, k, hailColor);
  hailAt(gfx, cx + 19 * k, y0 + 0 * k, k, hailColor);
  hailAt(gfx, cx + 16 * k, y0 + 11 * k, k, hailColor);
}

inline void iconMist(Arduino_GFX *gfx, int cx, int cy, int width, uint16_t mistColor, float detail = 1.0f) {
  for (int i = 0; i < 4; i++) {
    int y = cy - (int)(10 * detail) + (int)(i * 8 * detail);
    gfx->drawLine(cx - width / 2, y, cx + width / 2, y, mistColor);
  }
}

inline void drawWeatherIcon(Arduino_GFX *gfx, const String &code, int cx, int cy, int r, const ThemeColors &theme, float detail = 1.0f) {
  String group = code.substring(0, 2);
  // Kod ikony: 2 cyfry grupy + 'd' (dzien) lub 'n' (noc), np. "01d" / "10n"
  // (generowany z kodow WMO przez wmoToIcon() w szkicu glownym).
  bool isNight = code.length() >= 3 && code.charAt(2) == 'n';

  static const uint16_t SUN_COLOR = 0xFE60;
  static const uint16_t CLOUD_COLOR = 0xC618;
  static const uint16_t RAIN_COLOR = 0x051D;
  static const uint16_t MOON_COLOR = 0xCE59; // bladoszary/srebrzysty

  if (group == "01") {
    if (isNight) {
      iconMoon(gfx, cx, cy, r, MOON_COLOR, theme.bg);
    } else {
      iconSun(gfx, cx, cy, r, SUN_COLOR, detail);
    }
  } else if (group == "02") {
    if (isNight) {
      iconMoon(gfx, cx - r * 0.5, cy - r * 0.3, r * 0.7, MOON_COLOR, theme.bg);
    } else {
      iconSun(gfx, cx - r * 0.5, cy - r * 0.3, r * 0.7, SUN_COLOR, detail);
    }
    iconCloud(gfx, cx + r * 0.3, cy + r * 0.3, r * 0.7, CLOUD_COLOR);
  } else if (group == "03") {          // czeste przejasnienia (duze zachmurzenie)
    iconPeek(gfx, cx, cy, r, isNight, SUN_COLOR, MOON_COLOR, theme.bg, CLOUD_COLOR, detail);
  } else if (group == "04") {          // pochmurno - sama chmura
    iconCloud(gfx, cx, cy, r, CLOUD_COLOR);
  } else if (group == "05") {          // slonce z chmura i kroplami (przelotne opady w ciagu dnia)
    iconSunShower(gfx, cx, cy, r, SUN_COLOR, CLOUD_COLOR, RAIN_COLOR, theme.text, detail);
  } else if (group == "09") {          // mzawka / slaby deszcz: 3 krotkie krople
    iconPrecip(gfx, cx, cy, r, CLOUD_COLOR, RAIN_COLOR, theme.text, PRECIP_DROP, 3, 1, 0.55f);
  } else if (group == "10") {          // deszcz: 4 krople
    iconPrecip(gfx, cx, cy, r, CLOUD_COLOR, RAIN_COLOR, theme.text, PRECIP_DROP, 4, 1, 1.0f);
  } else if (group == "12") {          // ulewa: 4+3 krople w dwoch rzedach
    iconPrecip(gfx, cx, cy, r, CLOUD_COLOR, RAIN_COLOR, theme.text, PRECIP_DROP, 4, 2, 1.0f);
  } else if (group == "14") {          // deszcz ze sniegiem / marznacy deszcz
    iconPrecip(gfx, cx, cy, r, CLOUD_COLOR, RAIN_COLOR, theme.text, PRECIP_MIX, 4, 1, 1.0f);
  } else if (group == "13") {          // snieg: 4 platki
    iconPrecip(gfx, cx, cy, r, CLOUD_COLOR, RAIN_COLOR, theme.text, PRECIP_FLAKE, 4, 1, 1.0f);
  } else if (group == "15") {          // intensywny snieg: 4+3 platki
    iconPrecip(gfx, cx, cy, r, CLOUD_COLOR, RAIN_COLOR, theme.text, PRECIP_FLAKE, 4, 2, 1.0f);
  } else if (group == "11") {          // burza
    iconStorm(gfx, cx, cy, r, theme.muted, SUN_COLOR, detail);
  } else if (group == "17") {          // burza z gradem
    iconStormHail(gfx, cx, cy, r, theme.muted, SUN_COLOR, theme.text, detail);
  } else if (group == "50") {
    iconMist(gfx, cx, cy, r * 2, theme.muted, detail);
  } else {
    iconCloud(gfx, cx, cy, r, CLOUD_COLOR);
  }
}

// ---------------------------------------------------------------------
//  Ikony bitmapowe (kolorowe, wygladzone). Bitmapa 80x80 jest skalowana "usrednianiem" do zadanego rozmiaru
//  i mieszana z tlem (bg) wg kanalu alfa. Srodek ikony (cx, cy) = srodek CHMURKI (ikony bez chmury: srodek
//  ramki), wiec chmurki roznych ikon leza na jednej linii, slonce wystaje nad nia, a krople pod nia.
// ---------------------------------------------------------------------
inline const IconBmp *iconBmpFor(const String &code) {
  String g = code.substring(0, 2);
  bool night = code.length() >= 3 && code.charAt(2) == 'n';
  char name[4];
  name[0] = g.charAt(0); name[1] = g.charAt(1); name[2] = 0; name[3] = 0;
  if (g == "01" || g == "02" || g == "03" || g == "05") name[2] = night ? 'n' : 'd';
  const IconBmp *fallback = nullptr;
  for (int i = 0; i < ICON_BMP_COUNT; i++) {
    if (strcmp(ICON_BMPS[i].name, name) == 0) return &ICON_BMPS[i];
    if (strcmp(ICON_BMPS[i].name, "04") == 0) fallback = &ICON_BMPS[i];
  }
  return fallback;   // nieznany kod -> sama chmura
}

// Skalowanie ikony do size x size i mieszanie z tlem bg -> out (RGB565). Kosztowne (liczby zmiennoprzecinkowe),
// dlatego wynik trafia do pamieci podrecznej (patrz nizej) i jest liczony tylko raz dla kodu/rozmiaru/tla.
inline void renderIconBmp(const IconBmp *ic, int size, uint16_t bg, uint16_t *out) {
  static uint16_t srcC[ICONBMP_W * ICONBMP_W];
  static uint8_t srcA[ICONBMP_W * ICONBMP_W];
  // rozpakowanie RLE
  int p = 0;
  for (int i = 0; i < ic->runs && p < ICONBMP_W * ICONBMP_W; i++) {
    int n = pgm_read_byte(ic->data + 4 * i);
    uint8_t a = pgm_read_byte(ic->data + 4 * i + 1);
    uint16_t c = pgm_read_byte(ic->data + 4 * i + 2) | (pgm_read_byte(ic->data + 4 * i + 3) << 8);
    for (int k = 0; k < n && p < ICONBMP_W * ICONBMP_W; k++, p++) { srcC[p] = c; srcA[p] = a; }
  }
  while (p < ICONBMP_W * ICONBMP_W) { srcC[p] = 0; srcA[p] = 0; p++; }
  // skalowanie (srednia wazona powierzchnia) + mieszanie z tlem
  const float step = (float)ICONBMP_W / size;
  const int bgR = ((bg >> 11) & 31) * 255 / 31, bgG = ((bg >> 5) & 63) * 255 / 63, bgB = (bg & 31) * 255 / 31;
  for (int oy = 0; oy < size; oy++) {
    float sy0 = oy * step, sy1 = sy0 + step;
    int iy0 = (int)sy0, iy1 = (int)ceilf(sy1);
    if (iy1 > ICONBMP_W) iy1 = ICONBMP_W;
    for (int ox = 0; ox < size; ox++) {
      float sx0 = ox * step, sx1 = sx0 + step;
      int ix0 = (int)sx0, ix1 = (int)ceilf(sx1);
      if (ix1 > ICONBMP_W) ix1 = ICONBMP_W;
      float area = 0, A = 0, R = 0, G = 0, B = 0;
      for (int y = iy0; y < iy1; y++) {
        float wy = fminf(sy1, y + 1.0f) - fmaxf(sy0, (float)y);
        for (int x = ix0; x < ix1; x++) {
          float w = wy * (fminf(sx1, x + 1.0f) - fmaxf(sx0, (float)x));
          area += w;
          int idx = y * ICONBMP_W + x;
          float wa = w * srcA[idx];
          if (wa <= 0) continue;
          uint16_t c = srcC[idx];
          A += wa;
          R += wa * (((c >> 11) & 31) * 255 / 31);
          G += wa * (((c >> 5) & 63) * 255 / 63);
          B += wa * ((c & 31) * 255 / 31);
        }
      }
      uint16_t px;
      if (A <= 0 || area <= 0) px = bg;
      else {
        float alpha = A / (area * 255.0f);   // 0..1
        float r = R / A, g = G / A, b = B / A;
        int rr = (int)(bgR + (r - bgR) * alpha + 0.5f), gg = (int)(bgG + (g - bgG) * alpha + 0.5f), bb = (int)(bgB + (b - bgB) * alpha + 0.5f);
        px = ((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3);
      }
      out[oy * size + ox] = px;
    }
  }
}

// Pamiec podreczna gotowych (przeskalowanych, zmieszanych z tlem) ikon: klucz = bitmapa + rozmiar + tlo.
// Wyrzucanie najdawniej uzywanych, limit lacznej pamieci ICON_CACHE_BYTES. Bufory w PSRAM, jesli jest
// (inaczej w zwyklej pamieci). Gdy nie uda sie zaalokowac - ikona jest rysowana bez cache (wolniej).
static const int ICON_CACHE_N = 24;
static const uint32_t ICON_CACHE_BYTES = 192 * 1024;
struct IconCacheEntry { const IconBmp *ic; int16_t size; uint16_t bg; uint16_t *buf; uint32_t used; };

inline IconCacheEntry *iconCacheEntries() { static IconCacheEntry e[ICON_CACHE_N]; return e; }

inline void iconCacheFree(IconCacheEntry &e) {
  if (e.buf) free(e.buf);
  e.buf = nullptr; e.ic = nullptr;
}

inline uint16_t *iconCacheAlloc(size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32)
  if (psramFound()) {
    uint16_t *p = (uint16_t *)ps_malloc(bytes);
    if (p) return p;
  }
#endif
  return (uint16_t *)malloc(bytes);
}

// Najwiekszy rozmiar rysowania: bitmapy maja 80x80, ale skalowanie (srednia wazona powierzchnia) dziala
// tez w gore - lekko powiekszona ikona (do 88 px) wyglada dobrze. Uzywane dla ikony pogody biezacej.
static const int ICONBMP_MAX_SIZE = 88;

// Rysuje gotowy bufor size x size; clipY0..clipY1 = zakres wierszy ekranu, ktore wolno zamalowac
// (ramka powiekszonej ikony jest wieksza niz jej tresc - nie moze zamalowac np. separatora nad blokiem).
inline void blitIconRows(Arduino_GFX *gfx, int x0, int y0, uint16_t *buf, int size, int clipY0, int clipY1) {
  int r0 = clipY0 - y0, r1 = clipY1 - y0 + 1;
  if (r0 < 0) r0 = 0;
  if (r1 > size) r1 = size;
  if (r1 <= r0) return;
  gfx->draw16bitRGBBitmap(x0, y0 + r0, buf + r0 * size, size, r1 - r0);   // pelne wiersze leza w buforze ciagiem
}

inline void drawWeatherIconBmp(Arduino_GFX *gfx, const String &code, int cx, int cy, int size, uint16_t bg,
                               int clipY0 = -32768, int clipY1 = 32767) {
  static uint16_t tmpBuf[ICONBMP_MAX_SIZE * ICONBMP_MAX_SIZE];   // awaryjnie, gdy cache sie nie uda
  static uint32_t tick = 0;
  const IconBmp *ic = iconBmpFor(code);
  if (!ic) return;
  if (size > ICONBMP_MAX_SIZE) size = ICONBMP_MAX_SIZE;
  if (size < 8) size = 8;
  const int x0 = cx - size / 2, y0 = cy - size / 2;
  IconCacheEntry *E = iconCacheEntries();
  tick++;
  // trafienie w cache
  for (int i = 0; i < ICON_CACHE_N; i++) {
    if (E[i].buf && E[i].ic == ic && E[i].size == size && E[i].bg == bg) {
      E[i].used = tick;
      blitIconRows(gfx, x0, y0, E[i].buf, size, clipY0, clipY1);
      return;
    }
  }
  // brak w cache: znajdz miejsce (limit liczby wpisow i pamieci), wyrzucajac najdawniej uzywane
  const uint32_t need = (uint32_t)size * size * 2;
  for (;;) {
    uint32_t total = 0;
    int freeSlot = -1, lru = -1;
    for (int i = 0; i < ICON_CACHE_N; i++) {
      if (!E[i].buf) { if (freeSlot < 0) freeSlot = i; continue; }
      total += (uint32_t)E[i].size * E[i].size * 2;
      if (lru < 0 || E[i].used < E[lru].used) lru = i;
    }
    if (freeSlot >= 0 && total + need <= ICON_CACHE_BYTES) {
      uint16_t *buf = iconCacheAlloc(need);
      if (!buf) break;   // brak pamieci - rysujemy bez cache
      renderIconBmp(ic, size, bg, buf);
      E[freeSlot].ic = ic; E[freeSlot].size = size; E[freeSlot].bg = bg; E[freeSlot].buf = buf; E[freeSlot].used = tick;
      blitIconRows(gfx, x0, y0, buf, size, clipY0, clipY1);
      return;
    }
    if (lru < 0) break;
    iconCacheFree(E[lru]);
  }
  renderIconBmp(ic, size, bg, tmpBuf);
  blitIconRows(gfx, x0, y0, tmpBuf, size, clipY0, clipY1);
}

