# Generator spojnego zestawu ikon pogody 80x80 (RGB565 + alfa, RLE [n, alfa, lo, hi]) dla WeatherIconsBmp.h
import math
from PIL import Image, ImageDraw
K = 8                      # nadprobkowanie (wygladzanie krawedzi)
N = 80
SUN   = (255, 206, 0)
BOLT  = (255, 214, 0)
MOON  = (213, 222, 238)
CLOUD = (205, 210, 222)
DARK  = (148, 157, 172)
BACK  = (148, 157, 172)
DROP  = (24, 161, 255)
FLAKE = (230, 242, 255)
FOG   = (148, 157, 172)

class Ico:
    def __init__(s):
        s.im = Image.new('RGBA', (N*K, N*K), (0, 0, 0, 0))
    def layer(s, fn, color):
        m = Image.new('L', (N*K, N*K), 0); d = ImageDraw.Draw(m); fn(d)
        solid = Image.new('RGBA', m.size, color + (255,))
        s.im = Image.composite(solid, s.im, m)   # nowa warstwa przykrywa poprzednie
    def out(s):
        # rysunek wysrodkowany w poziomie (srodek obrysu = srodek pola, dokladnosc 1/K px). W pionie bez zmian:
        # dolna krawedz chmury we wszystkich ikonach lezy na tej samej linii CB - chmurki stoja rowno w rzedzie
        bb = s.im.split()[3].getbbox()
        if bb:
            dx = (N*K - (bb[0] + bb[2])) // 2
            moved = Image.new('RGBA', s.im.size, (0, 0, 0, 0))
            moved.paste(s.im, (dx, 0))
            s.im = moved
        p = s.im.convert('RGBa').resize((N, N), Image.BOX).convert('RGBA')
        return p

def S(v): return v * K
def circ(d, cx, cy, r): d.ellipse([S(cx-r), S(cy-r), S(cx+r), S(cy+r)], fill=255)
def line(d, x0, y0, x1, y1, w):
    d.line([S(x0), S(y0), S(x1), S(y1)], fill=255, width=int(S(w)))
    circ(d, x0, y0, w/2); circ(d, x1, y1, w/2)

def cloud_shape(cx, bottom, w):
    hb = 0.36 * w
    def f(d):
        d.rounded_rectangle([S(cx-w/2), S(bottom-hb), S(cx+w/2), S(bottom)], radius=S(hb/2), fill=255)
        circ(d, cx + 0.06*w, bottom - 0.40*w, 0.25*w)
        circ(d, cx - 0.21*w, bottom - 0.29*w, 0.17*w)
    return f

def sun(ic, cx, cy, r):
    def f(d):
        circ(d, cx, cy, r)
        for k in range(8):
            a = k * math.pi / 4
            r0, r1 = r + 0.30*r + 2, r + 0.30*r + 2 + 0.45*r + 2
            line(d, cx + math.cos(a)*r0, cy + math.sin(a)*r0, cx + math.cos(a)*r1, cy + math.sin(a)*r1, max(2.2, 0.2*r))
    ic.layer(f, SUN)

def sun_extent(r): return r + 0.30*r + 2 + 0.45*r + 2

def moon(ic, cx, cy, r):
    m = Image.new('L', (N*K, N*K), 0); d = ImageDraw.Draw(m)
    circ(d, cx, cy, r)
    circ_cut = Image.new('L', m.size, 0); d2 = ImageDraw.Draw(circ_cut)
    circ(d2, cx + 0.45*r, cy - 0.30*r, 0.82*r)
    from PIL import ImageChops
    m = ImageChops.subtract(m, circ_cut)
    solid = Image.new('RGBA', m.size, MOON + (255,))
    ic.im = Image.composite(solid, ic.im, m)

def drops(ic, xs, y, h=10, w=3.4):
    def f(d):
        for x in xs:
            r = w
            circ(d, x, y + h - r, r)
            d.polygon([(S(x), S(y)), (S(x - r*0.97), S(y + h - r*1.25)), (S(x + r*0.97), S(y + h - r*1.25))], fill=255)
    ic.layer(f, DROP)

def flakes(ic, pts, r=4.6, w=1.8):
    def f(d):
        for (x, y) in pts:
            for k in range(3):
                a = math.pi/2 + k * math.pi/3
                line(d, x - math.cos(a)*r, y - math.sin(a)*r, x + math.cos(a)*r, y + math.sin(a)*r, w)
    ic.layer(f, FLAKE)

def bolt(ic, x, y, s=1.0):
    pts = [(4, 0), (13, 0), (9, 8), (15, 8), (3, 22), (6, 12), (0, 12)]
    def f(d):
        d.polygon([(S(x + px*s), S(y + py*s)) for px, py in pts], fill=255)
    ic.layer(f, BOLT)

def hail(ic, pts, r=2.2):
    def f(d):
        for (x, y) in pts: circ(d, x, y, r)
    ic.layer(f, FLAKE)

# ---- geometria wspolna ----
CB = 55                           # dolna krawedz chmury - WSPOLNA dla wszystkich ikon z chmura
PW, PCX, PB = 58, 40, CB          # chmura w ikonach z opadem: szerokosc, srodek, dol
PY = CB + 3                       # gorna krawedz strefy opadu

def make(name):
    ic = Ico()
    if name == '01d':
        sun(ic, 40, 40, 15)
    elif name == '01n':
        moon(ic, 40, 40, 25)
    elif name in ('02d', '02n'):
        # 02 = wiecej slonca niz w 03: wieksza i bardziej odsloniete slonce / ksiezyc, chmura tej samej wielkosci
        # 02 = wiecej slonca niz w 03: mniejsza chmurka przesunieta w prawo, slonce / ksiezyc prawie cale widoczne
        if name == '02d': sun(ic, 29, 28, 12)
        else: moon(ic, 29, 23, 15)
        ic.layer(cloud_shape(50, CB, 48), CLOUD)
    elif name in ('03d', '03n'):
        if name == '03d': sun(ic, 33, 24, 10)      # slonce wyraznie schowane za chmura
        else: moon(ic, 34, 23, 13)
        ic.layer(cloud_shape(43, CB, 58), CLOUD)
    elif name == '04':
        ic.layer(cloud_shape(30, CB - 12, 38), BACK)
        ic.layer(cloud_shape(44, CB, 58), CLOUD)
    elif name in ('05d', '05n'):
        if name == '05d': sun(ic, 26, 30, 9)
        else: moon(ic, 27, 29, 12)
        ic.layer(cloud_shape(PCX + 3, PB, PW), CLOUD)
        drops(ic, [33, 43, 53], PY)
    elif name in ('09', '10', '13', '14', '15'):
        ic.layer(cloud_shape(PCX, PB, PW), CLOUD)
        if name == '09': drops(ic, [29, 40, 51], PY)
        if name == '10': drops(ic, [25, 35, 45, 55], PY)
        if name == '13': flakes(ic, [(28, PY + 6), (40, PY + 6), (52, PY + 6)])
        if name == '14':
            drops(ic, [28, 52], PY); flakes(ic, [(40, PY + 6)])
        if name == '15':
            flakes(ic, [(27, PY + 4), (40, PY + 4), (53, PY + 4), (33.5, PY + 13), (46.5, PY + 13)], r=3.8, w=1.7)
    elif name in ('11', '17'):
        ic.layer(cloud_shape(PCX, PB, PW), DARK)
        bolt(ic, 33, PY - 4, 1.05)
        if name == '17': hail(ic, [(25, PY + 8), (55, PY + 8), (29, PY + 16), (51, PY + 16)])
    elif name == '50':
        def f(d):
            for (x0, x1, y) in [(16, 60, 25), (12, 56, 35), (22, 66, 45), (16, 60, 55)]:
                line(d, x0, y, x1, y, 4.2)
        ic.layer(f, FOG)
    return ic.out()

NAMES = ['01d','01n','02d','02n','03d','03n','04','05d','05n','09','10','11','13','14','15','17','50']

def rle(img):
    px = list(img.getdata()); out = []; prev = None; n = 0
    def key(p):
        r, g, b, a = p
        if a == 0: return (0, 0)
        c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        return (a, c)
    for p in px:
        k = key(p)
        if k == prev and n < 255: n += 1
        else:
            if prev is not None: out.append((n, prev))
            prev, n = k, 1
    out.append((n, prev))
    data = []
    for n, (a, c) in out: data += [n, a, c & 255, c >> 8]
    return data

if __name__ == '__main__':
    import sys
    imgs = {n: make(n) for n in NAMES}
    for n, im in imgs.items(): im.save(f'icon_{n}.png')
    lines = ['// WeatherIconsBmp.h - wygenerowane automatycznie (gen_icons.py). Spojny zestaw kolorowych ikon pogody 80x80,',
             '// wspolna chmura / slonce / ksiezyc / opady w jednakowych proporcjach. RLE: [n, alfa, kolor565 lo, hi].',
             '#pragma once', '#include <Arduino.h>', '#define ICONBMP_W 80',
             'struct IconBmp { const char *name; const uint8_t *data; uint16_t runs; };']
    for n in NAMES:
        d = rle(imgs[n])
        lines.append(f'static const uint8_t ICONBMP_{n}[] PROGMEM = {{{",".join(map(str, d))}}};')
    lines.append('static const IconBmp ICON_BMPS[] = {')
    for n in NAMES: lines.append(f'  {{"{n}", ICONBMP_{n}, sizeof(ICONBMP_{n}) / 4}},')
    lines.append('};')
    lines.append(f'static const int ICON_BMP_COUNT = {len(NAMES)};')
    open(sys.argv[1], 'w').write('\n'.join(lines) + '\n')
