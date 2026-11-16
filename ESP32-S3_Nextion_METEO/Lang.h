#pragma once
// Lang.h - wybor jezyka stacji (ekran i panel WWW): 0 = polski, 1 = angielski.
// Ustawienie "lang" w panelu WWW; zmiana dziala po zapisie (restart). Teksty sa w kodzie parami
// L("polski", "English") - bez osobnych tabel, zeby przy kazdym napisie bylo widac oba warianty.
#include <Arduino.h>

inline uint8_t g_lang = 0;                       // ustawiane w setup() z ustawien (settings.lang)

inline bool langEn() { return g_lang == 1; }
inline const char *L(const char *pl, const char *en) { return g_lang == 1 ? en : pl; }
