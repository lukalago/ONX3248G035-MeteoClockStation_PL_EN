#!/usr/bin/env python3
"""Generator wygladzonych czcionek *Pl.h (4 bity krycia na piksel) dla stacji pogody.

Uklad zgodny z Adafruit GFX (GFXfont / GFXglyph), wiec metryki (szerokosc napisu, getTextBounds)
dzialaja bez zmian, ale TABLICA BITMAP ma inny format: 4 bity na piksel (0 = przezroczysty,
15 = pelny kolor), dwa piksele na bajt (starszy polbajt pierwszy), wiersz za wierszem; kazdy glif
zaczyna sie od pelnego bajtu. Rysuje je drawGlyphRaw() z PolishText.h (mieszanie z kolorem tla).

xAdvance bierzemy z dotychczasowych czcionek 1-bitowych, zeby uklad ekranow sie nie zmienil.

Uzycie: python3 gen_fonts_aa.py <katalog_z_czcionkami_Pl.h> [katalog_wyjsciowy]
"""
import re, sys, os
from PIL import Image, ImageDraw, ImageFont

TTF = {False: '/usr/share/fonts/truetype/freefont/FreeSans.ttf',
       True:  '/usr/share/fonts/truetype/freefont/FreeSansBold.ttf'}
PL = 'ąćęłńóśźżĄĆĘŁŃÓŚŹŻ'                      # kody 0x80..0x91
OVL = [('Ć', 'C', 'akcent nad C (dla Ć)'), ('Ń', 'N', 'akcent nad N (dla Ń)'), ('Ó', 'O', 'akcent nad O (dla Ó)'),
       ('Ś', 'S', 'akcent nad S (dla Ś)'), ('Ź', 'Z', 'akcent nad Z (dla Ź)'), ('Ż', 'Z', 'kropka nad Z (dla Ż)'),
       ('ż', 'z', 'akcent nad z (dla ż)')]       # kody 0x92..0x98

def old_glyphs(path):
    s = open(path, encoding='utf-8').read()
    g = [tuple(map(int, x)) for x in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}', s)]
    m = re.search(r'0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*(\d+)\s*\};', s)
    px = float(re.search(r'([0-9.]+) px', s).group(1))
    return g, int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)), px

def render(font, ch, pad):
    """Zwraca (alfa 2D lista, left, top) wzgledem punktu poczatkowego na linii bazowej; None gdy pusty."""
    W = H = pad * 4
    img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(img).text((pad, pad * 2), ch, font=font, fill=255, anchor='ls')
    bb = img.getbbox()
    if not bb: return None
    return img, pad, pad * 2

def crop(img, ox, oy, rows=None):
    px = img.load(); W, H = img.size
    pts = [(x, y) for y in range(H) for x in range(W) if px[x, y] >= 9 and (rows is None or rows(y))]
    if not pts: return None
    x0 = min(p[0] for p in pts); x1 = max(p[0] for p in pts)
    y0 = min(p[1] for p in pts); y1 = max(p[1] for p in pts)
    data = [[(px[x, y] * 15 + 127) // 255 if (rows is None or rows(y)) else 0 for x in range(x0, x1 + 1)]
            for y in range(y0, y1 + 1)]
    # yOffset jak w czcionkach 1-bitowych: dolny wiersz liter lezy NA linii bazowej (1 - bitmap_top)
    return data, x0 - ox, (y0 - oy) + 1

def pack(data):
    out = []
    flat = [v for row in data for v in row]
    if len(flat) % 2: flat.append(0)
    for i in range(0, len(flat), 2): out.append((flat[i] << 4) | flat[i + 1])
    return out

def convert(src, dst):
    name = os.path.basename(src)[:-2]
    bold = 'Bold' in name
    old, first, last, yadv, pxsize = old_glyphs(src)
    font = ImageFont.truetype(TTF[bold], pxsize)
    pad = int(pxsize * 1.5) + 8
    codes = {c: chr(c) for c in range(0x20, 0x7F)}
    for i, ch in enumerate(PL): codes[0x80 + i] = ch
    bm, glyphs = [], []
    for code in range(first, last + 1):
        o = old[code - first]
        xadv = o[3]
        res = None
        label = ''
        if code in codes:
            ch = codes[code]; label = ch
            r = render(font, ch, pad)
            if r: res = crop(*r)
        elif 0x92 <= code <= 0x98:
            comp, base, label = OVL[code - 0x92]
            rb = render(font, base, pad)
            bimg, box, boy = rb
            bpx = bimg.load()
            btop = min(y for y in range(bimg.size[1]) for x in range(bimg.size[0]) if bpx[x, y] > 0)
            rc = render(font, comp, pad)
            res = crop(*rc, rows=lambda y, t=btop: y < t)          # tylko wiersze nad litera bazowa = akcent
            if res:
                data, xo, yo = res
                res = (data, xo - old[ord(base) - first][3], yo)  # rysowany po literze (kursor juz przesuniety)
            xadv = 0
        if res:
            data, xo, yo = res
            glyphs.append((len(bm), len(data[0]), len(data), xadv, xo, yo, label))
            bm += pack(data)
        else:
            glyphs.append((len(bm), 0, 0, xadv, 0, 0, label))
    L = []
    L.append('#pragma once')
    L.append('#include <Adafruit_GFX.h>')
    L.append('')
    L.append('// %s: ASCII 0x20-0x7E + polskie litery 0x80-0x91 + nakladki akcentow 0x92-0x98 (xAdvance = 0).' % name)
    L.append('// CZCIONKA WYGLADZONA: bitmapa 4 bity na piksel (0 = przezroczysty .. 15 = pelny kolor), 2 piksele na bajt,')
    L.append('// starszy polbajt pierwszy, kazdy glif od pelnego bajtu. Rysuje ja wylacznie drawGlyphRaw() z PolishText.h')
    L.append('// (drawChar()/print() z biblioteki NIE nadaja sie do tego formatu). Metryki zgodne z Adafruit GFX.')
    L.append('// Wygenerowane z GNU FreeFont %s %.3f px narzedziem tools/gen_fonts_aa.py.' % ('FreeSansBold' if bold else 'FreeSans', pxsize))
    L.append('#ifndef PL_FONT_AA4')
    L.append('#define PL_FONT_AA4 1')
    L.append('#endif')
    L.append('')
    L.append('const uint8_t %sBitmaps[] PROGMEM = {' % name)
    for i in range(0, len(bm), 16):
        L.append('  ' + ', '.join('0x%02X' % b for b in bm[i:i + 16]) + ',')
    L.append('};')
    L.append('')
    L.append('const GFXglyph %sGlyphs[] PROGMEM = {' % name)
    for code, (off, w, h, xa, xo, yo, lab) in zip(range(first, last + 1), glyphs):
        L.append('  { %5d, %3d, %3d, %3d, %4d, %4d },   // 0x%02X %s' % (off, w, h, xa, xo, yo, code, ("'%s'" % lab) if lab else ''))
    L.append('};')
    L.append('')
    L.append('const GFXfont %s PROGMEM = {' % name)
    L.append('  (uint8_t  *)%sBitmaps,' % name)
    L.append('  (GFXglyph *)%sGlyphs,' % name)
    L.append('  0x%02X, 0x%02X, %d };' % (first, last, yadv))
    L.append('')
    L.append('// Approx. %d bytes' % (len(bm) + 7 * len(glyphs) + 7))
    open(dst, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    return len(bm)

if __name__ == '__main__':
    src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else src
    os.makedirs(dst, exist_ok=True)
    for fn in sorted(os.listdir(src)):
        if re.match(r'FreeSans(Bold)?\d+pt7bPl\.h$', fn):
            n = convert(os.path.join(src, fn), os.path.join(dst, fn))
            print('%-26s %7d B bitmap' % (fn, n))
