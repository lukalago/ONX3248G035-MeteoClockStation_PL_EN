// =====================================================================
//  PolishTextTest.h - tryb testowy czcionek (diagnostyka)
// =====================================================================
//  Dolaczany ze szkicu tylko, gdy #define PL_FONT_TEST 1. Po starcie rysuje ekran z polskimi
//  wersalikami obok zwyklych liter (z linia bazowa: czerwona; gora wersalikow: zielona) i wypisuje
//  w Serial Monitorze metryki glifow, ktore urzadzenie odczytuje z czcionki.
//  Wymaga od szkicu skrotow czcionek FONT_SMALL i FONT_DAY_TITLE (zdefiniowanych PRZED dolaczeniem).
// =====================================================================
#pragma once

#include "PolishText.h"

// Metryki glifow tak, jak je odczytuje urzadzenie z czcionki skompilowanej w programie
inline void plFontDump(const GFXfont *f, const char *name) {
  Serial.printf("[%s] first=0x%02X last=0x%02X yAdvance=%d sizeof(GFXglyph)=%d\n", name, (int)f->first, (int)f->last, (int)f->yAdvance, (int)sizeof(GFXglyph));
  const uint8_t codes[] = {'S', 'R', 0x8F, 0x95, 0x86};   // S, R, Ś (stary glif zlozony), nakladka akcentu Ś, ś
  for (uint8_t c : codes) {
    const GFXglyph &g = f->glyph[c - f->first];
    Serial.printf("  kod 0x%02X: offset=%u w=%u h=%u xAdv=%u xOff=%d yOff=%d | bajty: %02X %02X %02X %02X\n",
                  (int)c, (unsigned)g.bitmapOffset, (unsigned)g.width, (unsigned)g.height, (unsigned)g.xAdvance,
                  (int)g.xOffset, (int)g.yOffset, f->bitmap[g.bitmapOffset], f->bitmap[g.bitmapOffset + 1],
                  f->bitmap[g.bitmapOffset + 2], f->bitmap[g.bitmapOffset + 3]);
  }
  char buf[16];
  toFontCodes("ŚRODA", buf, sizeof(buf));
  gfx->setFont(f);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  Serial.printf("  getTextBounds(\"ŚRODA\"): x1=%d y1=%d w=%u h=%u | textTopOffset=%d\n", (int)x1, (int)y1, (unsigned)w, (unsigned)h,
                (int)textTopOffset(buf, f));
}

// Linie pomocnicze: CZERWONA = tuz pod linia bazowa (na niej maja stac dolne krawedzie liter),
// ZIELONA = gorna krawedz zwyklych wersalikow
inline void plGuides(const GFXfont *f, int baseline) {
  int16_t capTop = f->glyph['H' - f->first].yOffset;
  gfx->drawFastHLine(0, baseline + 1, LCD_H_RES, 0xF800);
  gfx->drawFastHLine(0, baseline + capTop, LCD_H_RES, 0x07E0);
}

inline void plTestLine(const GFXfont *f, const char *utf8, int topY) {
  char buf[96];
  toFontCodes(utf8, buf, sizeof(buf));
  int baseline = topY - textTopOffset(buf, f);
  printAt(utf8, 8, topY, f, 0xFFFF, 0x0000);
  plGuides(f, baseline);
}

// Rysowanie pojedynczych liter wlasnym rysownikiem (z pominieciem print/getTextBounds); litery
// zlozone (litera + nakladka) rysujemy tak samo jak w programie, czyli przez toFontCodes()
inline void plDirectRow(const GFXfont *f, int baseline, const char *utf8) {
  gfx->setFont(f);
  gfx->setTextSize(1);
  gfx->setTextColor(0xFFFF, 0x0000);
  int x = 8;
  const char *p = utf8;
  uint32_t cp;
  while (utf8Next(p, cp)) {
    char one[4];
    char tmp[5] = {0, 0, 0, 0, 0};
    if (cp < 0x80) tmp[0] = (char)cp;
    else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); }
    toFontCodes(tmp, one, sizeof(one));
    int cx = x;
    for (const char *q = one; *q; q++) {
      uint8_t c = (uint8_t)*q;
      drawGlyphRaw(f, c, cx, baseline, 0xFFFF);
      cx += f->glyph[c - f->first].xAdvance;     // nakladka ma xAdvance 0 i ujemny xOffset
    }
    x = cx + 8;
  }
  plGuides(f, baseline);
}

inline void plFontTest() {
  gfx->fillScreen(0x0000);
  plTestLine(FONT_DAY_TITLE, "ŚRODA RODA", 12);
  plTestLine(FONT_DAY_TITLE, "ĆŃÓŹŻ ĄĘŁ", 58);
  plTestLine(FONT_SMALL, "Śr Pt Wt ŚRODA", 104);
  plTestLine(FONT_SMALL, "ĆŃÓŹŻ ĄĘŁ śćńóźż", 134);
  plDirectRow(FONT_SMALL, 190, "SŚROCĆAĄONŃ");        // 9 pt - rysowane wprost przez drawChar
  plDirectRow(FONT_DAY_TITLE, 240, "SŚROCĆAĄONŃ");    // Bold 12 pt - j.w.
  printAt("czerwona = linia bazowa, zielona = gora wersalikow", 8, 270, FONT_SMALL, 0xFFFF, 0x0000);
  plFontDump(FONT_SMALL, "FreeSans 9pt");
  plFontDump(FONT_DAY_TITLE, "FreeSansBold 12pt");
}
