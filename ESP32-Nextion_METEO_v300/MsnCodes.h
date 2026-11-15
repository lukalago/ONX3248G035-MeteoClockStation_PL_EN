// =====================================================================
//  MsnCodes.h - tlumaczenie danych MSN Pogoda na wartosci uzywane w szkicu
// =====================================================================
//  Czyste funkcje bez zaleznosci od sprzetu i sieci (dlatego osobny plik - mozna je
//  przetestowac na komputerze):
//    - msnSymbolToWmo(): symbol MSN ("d000", "n412" ...) -> kod pogody WMO, na ktorym
//      opiera sie reszta programu (ikony, wmoToIcon(), isSevereWmo()),
//    - msnSpeedToMs(), msnPressureToHpa(): przeliczenie jednostek podanych przez MSN,
//    - msnDateFromIso(), msnHourFromIso(): data "RRRR-MM-DD" i godzina (0-23) z napisu ISO 8601,
//    - msnAlertLevel(): stopien ostrzezenia IMGW (1 zolty, 2 pomaranczowy, 3 czerwony),
//    - msnCopyUtf8(): kopiowanie napisu z obcieciem na granicy znaku UTF-8,
//    - msnAlertName(): nazwa ostrzezenia bez nazwy koloru ("Mgła", a nie "Mgła - Żółty").
// =====================================================================
#pragma once

#include <Arduino.h>
#include <string.h>
#include <ctype.h>

// MSN "mostly cloudy" nie ma dokladnego odpowiednika w WMO (2 = czesciowe zachmurzenie,
// 3 = pochmurno). Uzywamy kodu 4, ktorego wmoToIcon() nie zna i daje dla niego ikone "03"
// (pojedyncza chmura) - posrednia miedzy "02" a "04".
static const int MSN_WMO_MOSTLY_CLOUDY = 4;

// Symbol MSN: litera d (dzien) / n (noc) + 3 cyfry, czasem z dopiskiem (np. flaga "windy").
// Zwraca kod WMO albo -1, gdy symbol jest nieznany/niepoprawny. isDay = pierwsza litera.
inline int msnSymbolToWmo(const char *symbol, bool &isDay) {
  isDay = true;
  if (!symbol || strlen(symbol) < 4) return -1;
  char c0 = symbol[0];
  if (c0 != 'd' && c0 != 'n' && c0 != 'D' && c0 != 'N') return -1;
  isDay = (c0 == 'd' || c0 == 'D');
  int num = 0;
  for (int i = 1; i < 4; i++) {
    if (symbol[i] < '0' || symbol[i] > '9') return -1;
    num = num * 10 + (symbol[i] - '0');
  }
  switch (num) {
    case 0:   return 0;     // bezchmurnie
    case 100: return 1;     // prawie bezchmurnie
    case 200: return 2;     // czesciowe zachmurzenie
    case 300:
    case 500: return MSN_WMO_MOSTLY_CLOUDY;   // duze zachmurzenie
    case 400: return 3;     // pochmurno
    // przelotny deszcz
    case 210: case 220: case 310: return 80;  // lekki przelotny deszcz
    case 320: return 81;                      // przelotny deszcz
    // deszcz ze sniegiem / marznace opady
    case 211: case 221: case 311: case 321:
    case 411: case 421: case 431: return 67;
    case 603: return 66;                      // marznacy deszcz
    case 605: return 67;                      // granulki lodu
    // snieg
    case 212: case 222: case 312: case 322: return 85;   // przelotny snieg
    case 412: return 71;                      // lekki snieg
    case 422: return 73;                      // snieg
    case 432: return 75;                      // intensywny snieg
    // opad ciagly
    case 410: return 61;                      // lekki deszcz
    case 420: case 430: return 63;            // deszcz / umiarkowany deszcz
    // burze
    case 240: case 340: case 440: return 95;
    // grad
    case 705: case 905: return 96;
    // mgla / zamglenie
    case 600: case 900: case 907: return 45;
  }
  return -1;
}

// Predkosc wiatru w jednostce podanej przez MSN (pole units.speed, np. "km/h", "mph") -> m/s.
// Nieznana/pusta jednostka = km/h (taki jest uklad metryczny w MSN).
inline float msnSpeedToMs(float v, const char *unit) {
  char u[16] = "";
  size_t n = 0;
  for (const char *p = unit ? unit : ""; *p && n < sizeof(u) - 1; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80 && !isspace(c)) u[n++] = (char)tolower(c);   // pomijamy znaki niedrukowalne (np. znacznik LRM)
  }
  u[n] = 0;
  if (strstr(u, "mph")) return v * 0.44704f;
  if (strstr(u, "m/s") || strcmp(u, "ms") == 0) return v;
  if (strstr(u, "kn")) return v * 0.514444f;       // wezly
  return v / 3.6f;                                  // km/h (takze "kph" i brak jednostki)
}

// Cisnienie w jednostce podanej przez MSN (units.pressure) -> hPa. Przy braku jednostki
// zgadujemy po wartosci: ponizej 100 to inHg (np. 29.92), inaczej hPa/mbar.
inline float msnPressureToHpa(float v, const char *unit) {
  char u[16] = "";
  size_t n = 0;
  for (const char *p = unit ? unit : ""; *p && n < sizeof(u) - 1; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80 && !isspace(c)) u[n++] = (char)tolower(c);
  }
  u[n] = 0;
  if (strstr(u, "inhg") || strcmp(u, "in") == 0) return v * 33.8639f;
  if (strstr(u, "mmhg")) return v * 1.33322f;
  if (strstr(u, "kpa")) return v * 10.0f;
  if (strstr(u, "mb") || strstr(u, "hpa")) return v;
  if (u[0] == 0 && v > 0 && v < 100.0f) return v * 33.8639f;
  return v;
}

// "2026-10-06T06:12:00+02:00" -> "2026-10-06" (wymaga formy RRRR-MM-DD na poczatku). false = zly format.
inline bool msnDateFromIso(const char *iso, char out[11]) {
  out[0] = 0;
  if (!iso || strlen(iso) < 10) return false;
  for (int i = 0; i < 10; i++) {
    char c = iso[i];
    if (i == 4 || i == 7) { if (c != '-') return false; }
    else if (c < '0' || c > '9') return false;
  }
  memcpy(out, iso, 10);
  out[10] = 0;
  return true;
}

// "2026-10-06T17:00:00+02:00" -> 17 (godzina lokalna zapisana w napisie). -1 = zly format.
inline int msnHourFromIso(const char *iso) {
  if (!iso || strlen(iso) < 13 || iso[10] != 'T') return -1;
  if (iso[11] < '0' || iso[11] > '2' || iso[12] < '0' || iso[12] > '9') return -1;
  int h = (iso[11] - '0') * 10 + (iso[12] - '0');
  return (h >= 0 && h <= 23) ? h : -1;
}

// Stopien ostrzezenia z pol MSN: "level" ("Yellow"/"Orange"/"Red") albo, gdy go brak, "significance"
// ("Y"/"O"/"R"). 1 = zolty, 2 = pomaranczowy, 3 = czerwony. Nieznana, ale niepusta wartosc = 1
// (ostrzezenie nigdy nie ma zniknac po cichu); obie puste = 0 (brak ostrzezenia).
inline uint8_t msnAlertLevel(const char *level, const char *significance) {
  char u[12] = "";
  size_t n = 0;
  for (const char *p = level ? level : ""; *p && n < sizeof(u) - 1; p++) u[n++] = (char)tolower((unsigned char)*p);
  u[n] = 0;
  if (strstr(u, "red")) return 3;
  if (strstr(u, "orange")) return 2;
  if (strstr(u, "yellow")) return 1;
  if (u[0]) return 1;
  char c = (significance && significance[0]) ? (char)toupper((unsigned char)significance[0]) : 0;
  if (c == 'R') return 3;
  if (c == 'O') return 2;
  if (c == 'Y') return 1;
  return c ? 1 : 0;
}

// Kopiuje napis UTF-8 do bufora o rozmiarze dstSize, ucinajac go tak, zeby nie przeciac znaku
// wielobajtowego (polskie litery maja 2 bajty - przeciety znak dalby krzaczek na ekranie).
inline void msnCopyUtf8(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) return;
  dst[0] = 0;
  if (!src) return;
  size_t n = strlen(src);
  if (n >= dstSize) {
    n = dstSize - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;   // src[n] to bajt kontynuacji - cofnij do poczatku znaku
  }
  memcpy(dst, src, n);
  dst[n] = 0;
}

// Nazwa ostrzezenia BEZ nazwy koloru (kolor widac z barwy napisu). MSN podaje osobno pole "event"
// ("Mgła") i "title" ("Mgła - Żółty"); bierzemy "event", a gdy go brak - "title" ucięty od ostatniego
// " - " (nazwa koloru jest zawsze na koncu). Wynik trafia do dst (rozmiar dstSize), bez przecinania
// znakow UTF-8.
inline void msnAlertName(const char *event, const char *title, char *dst, size_t dstSize) {
  if (event && event[0]) {
    msnCopyUtf8(dst, dstSize, event);
    return;
  }
  char tmp[64];
  msnCopyUtf8(tmp, sizeof(tmp), title ? title : "");
  char *cut = nullptr;
  for (char *p = strstr(tmp, " - "); p; p = strstr(p + 3, " - ")) cut = p;   // ostatnie wystapienie
  if (cut) *cut = 0;
  msnCopyUtf8(dst, dstSize, tmp);
}
