// =====================================================================
//  PolishText.h - polskie znaki i rysowanie tekstu czcionkami *Pl.h
// =====================================================================
//  Co tu jest:
//    - zamiana napisu UTF-8 na kody czcionki (toFontCodes), w tym skladanie liter z akcentem
//      nad litera (Ć Ń Ó Ś Ź Ż ż) z litery bazowej i nakladki akcentu (plOverlay),
//    - zamiana na wielkie litery z polskimi znakami (uppercasePL),
//    - wlasny rysownik glifow (drawGlyphRaw) dla czcionek WYGLADZONYCH (4 bity krycia na piksel,
//      krawedzie mieszane z kolorem tla), bez drawChar()/print() z biblioteki,
//    - funkcje tekstu uzywane w calym szkicu: printAt, printCentered, printCenteredSpaced, textWidth.
//
//  Czego wymaga od szkicu:
//    - globalnego wskaznika Arduino_GFX *gfx (definicja w pliku .ino),
//    - stalej LCD_H_RES (BoardConfig.h),
//    - czcionek z zestawu *Pl.h (ASCII + polskie litery, wygladzone - tools/gen_fonts_aa.py),
//      dolaczanych w szkicu PRZED tym plikiem.
//
//  Plik jest "header-only" (funkcje inline) i dolaczany tylko z szkicu .ino.
// =====================================================================
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_GFX.h>     // GFXfont / GFXglyph
#include "BoardConfig.h"      // LCD_H_RES

#ifndef PL_FONT_AA4
#error "Czcionki *Pl.h musza byc w wersji wygladzonej (4 bity, tools/gen_fonts_aa.py) i dolaczone przed PolishText.h"
#endif

extern Arduino_GFX *gfx;      // definicja w szkicu

// ---------------------------------------------------------------------
//  Polskie znaki. Biblioteka graficzna nie dekoduje UTF-8, ale czcionki *Pl.h
//  zawieraja polskie litery pod kodami 0x80..0x91 (a c e l n o s z z, potem duze)
//  i nakladki akcentow 0x92..0x98, zaraz za ASCII. Napis UTF-8 zamieniamy na takie
//  kody (toFontCodes), a rysuje je wlasny rysownik glifow (drawGlyphRaw, nizej).
// ---------------------------------------------------------------------

// Punkt kodowy Unicode polskiej litery -> numer 0..17 (kolejnosc glifow w czcionce), -1 = inny znak
inline int plIndex(uint32_t cp) {
  switch (cp) {
    case 0x0105: return 0;   // ą
    case 0x0107: return 1;   // ć
    case 0x0119: return 2;   // ę
    case 0x0142: return 3;   // ł
    case 0x0144: return 4;   // ń
    case 0x00F3: return 5;   // ó
    case 0x015B: return 6;   // ś
    case 0x017A: return 7;   // ź
    case 0x017C: return 8;   // ż
    case 0x0104: return 9;   // Ą
    case 0x0106: return 10;  // Ć
    case 0x0118: return 11;  // Ę
    case 0x0141: return 12;  // Ł
    case 0x0143: return 13;  // Ń
    case 0x00D3: return 14;  // Ó
    case 0x015A: return 15;  // Ś
    case 0x0179: return 16;  // Ź
    case 0x017B: return 17;  // Ż
  }
  return -1;
}

inline bool utf8Next(const char *&p, uint32_t &cp) {
  uint8_t c = (uint8_t)*p++;
  if (!c) { cp = 0; return false; }
  if (c < 0x80) { cp = c; return true; }
  if ((c & 0xE0) == 0xC0) {
    uint8_t c2 = (uint8_t)*p++;
    if ((c2 & 0xC0) != 0x80) { cp = '?'; return true; }
    cp = ((uint32_t)(c & 0x1F) << 6) | (c2 & 0x3F);
    return true;
  }
  if ((c & 0xF0) == 0xE0) {
    uint8_t c2 = (uint8_t)*p++, c3 = (uint8_t)*p++;
    if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) { cp = '?'; return true; }
    cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
    return true;
  }
  cp = '?';
  return true;
}

// Wersaliki z akcentem NAD litera (Ć Ń Ó Ś Ź Ż) oraz "ż" rysujemy jako DWA glify: zwykla litera ASCII
// + sama nakladka akcentu (kody 0x92..0x98, xAdvance = 0). Powod: glify z pustym wierszem miedzy akcentem
// a litera sa zle rysowane przez zoptymalizowany drawChar() biblioteki dla wyswietlaczy TFT (skrocony
// korpus litery, drobniejszy i przesuniety akcent - widac to na zdjeciu ekranu testowego). Wyglad po
// zlozeniu jest identyczny jak pierwotny glif. Zwraca true i wpisuje litere bazowa oraz kod nakladki.
inline bool plOverlay(uint32_t cp, char &base, uint8_t &accent) {
  switch (cp) {
    case 0x0106: base = 'C'; accent = 0x92; return true;   // Ć
    case 0x0143: base = 'N'; accent = 0x93; return true;   // Ń
    case 0x00D3: base = 'O'; accent = 0x94; return true;   // Ó
    case 0x015A: base = 'S'; accent = 0x95; return true;   // Ś
    case 0x0179: base = 'Z'; accent = 0x96; return true;   // Ź
    case 0x017B: base = 'Z'; accent = 0x97; return true;   // Ż
    case 0x017C: base = 'z'; accent = 0x98; return true;   // ż
  }
  return false;
}

// Napis UTF-8 -> bajty czcionki: ASCII bez zmian, polskie litery -> 0x80..0x91 (albo litera + nakladka
// 0x92..0x98, patrz plOverlay), reszta -> '?'
inline void toFontCodes(const char *utf8, char *out, size_t maxLen) {
  size_t oi = 0;
  const char *p = utf8;
  uint32_t cp;
  while (oi + 2 < maxLen && utf8Next(p, cp)) {
    char base;
    uint8_t accent;
    if (plOverlay(cp, base, accent)) {
      out[oi++] = base;
      out[oi++] = (char)accent;
      continue;
    }
    int pi = plIndex(cp);
    if (pi >= 0) out[oi++] = (char)(0x80 + pi);
    else if (cp >= 0x20 && cp <= 0x7E) out[oi++] = (char)cp;
    else out[oi++] = '?';
  }
  out[oi] = 0;
}

// Gorny brzeg tekstu (yOffset najwyzszego piksela), liczony tak, zeby AKCENTY NAD WERSALIKAMI
// NIE PRZESUWALY LINII BAZOWEJ. printAt() wyrownuje do topY gorny piksel napisu; gdy napis ma
// "Ś/Ć/Ń/Ó/Ź/Ż", najwyzszym pikselem jest akcent (3-5 px ponad wersalikiem) i cala linijka
// ladowalaby o tyle nizej niz napisy bez takich liter (np. "Śr" nizej niz "Pt", "ŚRODA" nizej
// niz "WTOREK"). Dlatego glify polskich liter (kody >= 0x80) przycinamy do wysokosci wersalika
// 'H' - akcent po prostu wystaje nad gorna linie. Dla zwyklych napisow (ASCII) wynik jest taki
// sam jak z getTextBounds().
inline int16_t textTopOffset(const char *codes, const GFXfont *font) {
  int16_t capTop = font->glyph['H' - font->first].yOffset;
  int16_t top = 0x7FFF;
  for (const char *p = codes; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < font->first || c > font->last) continue;
    const GFXglyph &g = font->glyph[c - font->first];
    if (g.width == 0 || g.height == 0) continue;     // spacja nie wplywa na gorny brzeg
    int16_t yo = g.yOffset;
    if (c >= 0x80 && yo < capTop) yo = capTop;        // akcent nad wersalikiem nie liczy sie do wysokosci
    if (yo < top) top = yo;
  }
  return (top == 0x7FFF) ? 0 : top;
}

inline void uppercasePL(char *s, size_t maxLen) {
  char out[64];
  size_t oi = 0;
  const char *p = s;
  uint32_t cp;
  while (*p && oi + 3 < sizeof(out) && oi + 3 < maxLen) {
    const char *before = p;
    if (!utf8Next(p, cp)) break;
    if (cp >= 'a' && cp <= 'z') cp -= ('a' - 'A');
    else {
      switch (cp) {
        case 0x0105: cp = 0x0104; break; // ą -> Ą
        case 0x0107: cp = 0x0106; break; // ć -> Ć
        case 0x0119: cp = 0x0118; break; // ę -> Ę
        case 0x0142: cp = 0x0141; break; // ł -> Ł
        case 0x0144: cp = 0x0143; break; // ń -> Ń
        case 0x00F3: cp = 0x00D3; break; // ó -> Ó
        case 0x015B: cp = 0x015A; break; // ś -> Ś
        case 0x017A: cp = 0x0179; break; // ź -> Ź
        case 0x017C: cp = 0x017B; break; // ż -> Ż
        default:
          while (p - before > 0) { // copy original bytes for unsupported UTF-8
            if (oi + 1 >= sizeof(out) || oi + 1 >= maxLen) break;
            out[oi++] = *before++;
          }
          continue;
      }
    }
    if (cp < 0x80) {
      out[oi++] = (char)cp;
    } else {
      if (oi + 2 >= sizeof(out) || oi + 2 >= maxLen) break;
      out[oi++] = (char)(0xC0 | (cp >> 6));
      out[oi++] = (char)(0x80 | (cp & 0x3F));
    }
  }
  out[oi] = 0;
  strncpy(s, out, maxLen - 1);
  s[maxLen - 1] = 0;
}

// Mieszanie dwoch kolorow RGB565: a = 0..15 (0 = tlo, 15 = kolor litery)
inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a >= 15) return fg;
  if (a == 0) return bg;
  uint16_t r = ((fg >> 11) * a + (bg >> 11) * (15 - a) + 7) / 15;
  uint16_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (15 - a) + 7) / 15;
  uint16_t b = ((fg & 0x1F) * a + (bg & 0x1F) * (15 - a) + 7) / 15;
  return (r << 11) | (g << 5) | b;
}

// Krycie piksela i (0..w*h-1) glifu WYGLADZONEJ czcionki: 4 bity na piksel, starszy polbajt pierwszy
inline uint8_t glyphAlpha(const uint8_t *bm, uint32_t i) {
  uint8_t v = pgm_read_byte(bm + (i >> 1));
  return (i & 1) ? (v & 0x0F) : (v >> 4);
}

// WLASNY rysownik glifu dla czcionek WYGLADZONYCH (*Pl.h, 4 bity krycia na piksel - format opisany
// w naglowku czcionki i w tools/gen_fonts_aa.py). Piksele pelne rysujemy seriami poziomymi
// (writeFastHLine), krawedzie - pojedynczo, w kolorze zmieszanym z kolorem tla "bg". Wyswietlacz
// nie pozwala odczytac tla, dlatego "bg" MUSI byc kolorem, na ktorym lezy napis (tlo ekranu albo
// karty); przy zlym kolorze wokol liter bylaby cienka obwodka. Piksele przezroczyste (0) nie sa
// ruszane, wiec napis mozna rysowac na tle bez wczesniejszego czyszczenia, a ponowne narysowanie
// tego samego napisu daje identyczny wynik (mieszamy z "bg", nie z tym, co jest na ekranie).
// Nie uzywamy drawChar()/print() z biblioteki - nie znaja tego formatu.
inline void drawGlyphRaw(const GFXfont *f, uint8_t code, int cursorX, int baselineY, uint16_t color,
                         uint16_t bg = 0x0000) {
  if (code < f->first || code > f->last) return;
  const GFXglyph &g = f->glyph[code - f->first];
  int w = g.width, h = g.height;
  if (w == 0 || h == 0) return;
  int x0 = cursorX + g.xOffset, y0 = baselineY + g.yOffset;
  const uint8_t *bm = f->bitmap + g.bitmapOffset;
  uint16_t shade[16];                     // kolory posrednie liczone raz na glif
  for (uint8_t a = 0; a < 16; a++) shade[a] = blend565(bg, color, a);
  gfx->startWrite();
  uint32_t i = 0;
  for (int yy = 0; yy < h; yy++) {
    int runStart = -1;
    for (int xx = 0; xx < w; xx++, i++) {
      uint8_t a = glyphAlpha(bm, i);
      if (a == 15) {
        if (runStart < 0) runStart = xx;
        continue;
      }
      if (runStart >= 0) {                // koniec serii pelnych pikseli
        gfx->writeFastHLine(x0 + runStart, y0 + yy, xx - runStart, color);
        runStart = -1;
      }
      if (a) gfx->writePixel(x0 + xx, y0 + yy, shade[a]);
    }
    if (runStart >= 0) gfx->writeFastHLine(x0 + runStart, y0 + yy, w - runStart, color);
  }
  gfx->endWrite();
}

// Rysuje ciag kodow czcionki od kursora (cursorX, baselineY); kursor przesuwa o xAdvance (nakladki akcentow: 0)
inline void drawCodesRaw(const GFXfont *f, const char *codes, int cursorX, int baselineY, uint16_t color,
                         uint16_t bg) {
  for (const char *p = codes; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < f->first || c > f->last) continue;
    drawGlyphRaw(f, c, cursorX, baselineY, color, bg);
    cursorX += f->glyph[c - f->first].xAdvance;
  }
}

// "bg" = kolor tla pod napisem (do wygladzania krawedzi liter - patrz drawGlyphRaw)
inline void printAt(const char *text, int x, int topY, const GFXfont *font, uint16_t color, uint16_t bg) {
  char buf[128];
  toFontCodes(text, buf, sizeof(buf));
  gfx->setFont(font);              // potrzebne dla getTextBounds()
  gfx->setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  y1 = textTopOffset(buf, font);   // bez akcentow nad wersalikami - patrz textTopOffset()
  drawCodesRaw(font, buf, x - x1, topY - y1, color, bg);
}

inline uint16_t textWidth(const char *text, const GFXfont *font) {
  char buf[128];
  toFontCodes(text, buf, sizeof(buf));
  gfx->setFont(font);
  gfx->setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  return w;
}

inline void printCentered(const char *text, int topY, const GFXfont *font, uint16_t color, uint16_t bg) {
  int x = (LCD_H_RES - (int)textWidth(text, font)) / 2;
  printAt(text, x, topY, font, color, bg);
}

// Jak printCentered, ale z dodatkowym odstepem miedzy znakami (rozstrzelenie), z dwiema
// niezaleznymi wartosciami: "extra" - miedzy zwyklymi znakami, "extraColon" - po obu
// stronach dwukropka. Znaki rysujemy pojedynczo; kursor przesuwamy o szerokosc znaku z
// czcionki (xAdvance) + odstep, wiec naturalne odstepy czcionki zostaja.
inline void printCenteredSpaced(const char *text, int topY, const GFXfont *font, uint16_t color, uint16_t bg,
                         int extra, int extraColon) {
  char buf[64];
  toFontCodes(text, buf, sizeof(buf));
  gfx->setFont(font);
  gfx->setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  y1 = textTopOffset(buf, font);
  int n = (int)strlen(buf);
  auto gapAfter = [&](int i) {
    if ((uint8_t)buf[i + 1] >= 0x92) return 0;    // nakladka akcentu stoi przy swojej literze
    return (buf[i] == ':' || buf[i + 1] == ':') ? extraColon : extra;
  };
  int totalW = (int)w;
  for (int i = 0; i < n - 1; i++) totalW += gapAfter(i);
  int cursorX = (LCD_H_RES - totalW) / 2 - x1;
  int baseY = topY - y1;
  for (int i = 0; i < n; i++) {
    uint8_t c = (uint8_t)buf[i];
    drawGlyphRaw(font, c, cursorX, baseY, color, bg);
    int adv = (c >= font->first && c <= font->last) ? font->glyph[c - font->first].xAdvance : 0;
    cursorX += adv + ((i < n - 1) ? gapAfter(i) : 0);
  }
}
