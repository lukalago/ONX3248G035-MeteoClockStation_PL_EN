// =====================================================================
//  MsnWeather.h - opcjonalne zrodlo pogody i prognozy: MSN Pogoda
// =====================================================================
//  Jak to dziala:
//    1. Szkic zawsze pobiera dane z Open-Meteo (godzinowka, trend cisnienia, kody ikon,
//       dni prognozy z data i nazwa dnia). To jest "baza".
//    2. Gdy w panelu WWW wybrano zrodlo MSN, applyMsnWeather() pobiera dodatkowo
//       odpowiedz MSN i NADPISUJE nia to, co widac na ekranach: biezace pomiary
//       (temperatura, odczuwalna, wilgotnosc, cisnienie, wiatr), ikone, prognoze dni
//       (min/max, szansa opadow, ikona dnia) oraz godzinowke w widoku dnia i alarm porywow.
//       Z Open-Meteo zostaje trend cisnienia i jakosc powietrza. Dodatkowo MSN przekazuje
//       ostrzezenia IMGW - kazdy dzien dostaje najwyzszy stopien ostrzezenia, ktore go obejmuje
//       (barwi temperature i pokazuje nazwe w widoku dnia).
//    3. Gdy MSN nie odpowie albo zwroci cos nieoczekiwanego, zostaja dane z Open-Meteo,
//       a powod trafia do weatherStatus (widoczny w panelu WWW i w Serial Monitorze).
//
//  UWAGA - to jest NIEOFICJALNY dostep. Adres i klucz klienta nizej to te same, ktorych
//  uzywa mini-aplikacja pogodowa MSN i dekodery Enigma2 (OpenATV). MSN ma tez oficjalne API,
//  ale wymaga klucza przydzielonego partnerowi. Nieoficjalny dostep moze przestac dzialac
//  w kazdej chwili (zmiana klucza, formatu odpowiedzi, blokada) - dlatego zawsze jest
//  wsparty Open-Meteo.
//
//  Plik dolaczany ze szkicu PO definicjach: struktur pogody (current, todayDay, forecast),
//  wmoToIcon(), settingsStore, geoReady/cityLat/cityLon, weatherStatus.
// =====================================================================
#pragma once
#include "Lang.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <new>
#include "MsnCodes.h"

// Po starcie (pierwsze udane pobranie) wypisz w Serial Monitorze poczatek odpowiedzi MSN oraz
// fragment z danymi godzinowymi - przydatne do rozbudowy (np. godzinowka z MSN). 0 = wylacz.
#ifndef MSN_DEBUG_PAYLOAD
#define MSN_DEBUG_PAYLOAD 0
#endif

static const uint8_t MSN_MAX_DAYS = 6;            // dzis + 5 kolejnych dni (tyle wystarczy)
static const uint16_t MSN_MAX_HOURS = 144;        // 6 dni x 24 h wpisow godzinowych
static const uint8_t MSN_MAX_ALERTS = 8;          // tyle ostrzezen jednoczesnie wystarczy
static const int MSN_MAX_PAYLOAD = 900000;        // zabezpieczenie pamieci [B]

// Dane z MSN wyciagniete z JSON do prostych pol (dzieki temu logike nakladania mozna testowac
// bez biblioteki JSON).
struct MsnDay {
  bool ok = false;       // false = MSN nie podal min/max na ten dzien
  char date[11] = "";    // "RRRR-MM-DD" z daty wschodu slonca; pusty = brak
  float hi = 0, lo = 0;
  int rain = -1;         // szansa opadow [%], -1 = brak
  int wmo = -1;          // kod WMO z symbolu dnia, -1 = brak
};

// Jeden wpis godzinowy MSN (godzina lokalna z pola "valid").
struct MsnHour {
  char date[11] = "";    // "RRRR-MM-DD"
  int8_t hour = -1;      // 0-23
  int8_t rain = -1;      // szansa opadow [%], -1 = brak
  int16_t temp = 0;
  int16_t windDir = 0;   // skad wieje [stopnie]
  float windMs = 0;      // m/s
  float gustMs = 0;      // m/s
  int8_t wmo = -1;       // kod WMO z symbolu, -1 = brak
  bool isDay = true;
};

// Ostrzezenie IMGW przekazane przez MSN (pole "alerts"); start/end = czas lokalny.
struct MsnAlert {
  uint8_t level = 0;         // 1 zolty, 2 pomaranczowy, 3 czerwony
  char title[40] = "";       // nazwa bez koloru, np. "Mgła"
  char startDate[11] = "";   // "RRRR-MM-DD"
  int8_t startHour = -1;
  char endDate[11] = "";
  int8_t endHour = -1;
};

// Ostrzezenia przed wiatrem (nazwa zawiera "wiatr", np. "Silny wiatr", "Silny wiatr z deszczem") -
// okna czasowe do kolorowania kierunku i predkosci wiatru (ekran glowny, godzinowka).
struct WindAlertWin {
  uint8_t level = 0;
  char title[40] = "";       // nazwa ostrzezenia jak w MSN/IMGW, np. "Wiatr", "Silny wiatr"
  char startDate[11] = "";
  int8_t startHour = 0;
  char endDate[11] = "";
  int8_t endHour = 0;
};
static WindAlertWin msnWindAlerts[MSN_MAX_ALERTS];
static uint8_t msnWindAlertCount = 0;

inline bool msnIsWindAlert(const char *title) {
  char low[48];
  size_t n = 0;
  for (const char *p = title; *p && n < sizeof(low) - 1; p++) low[n++] = (char)tolower((unsigned char)*p);
  low[n] = 0;
  return strstr(low, "wiatr") != nullptr;
}

// Najwyzszy stopien ostrzezenia przed wiatrem obowiazujacego w danej godzinie (date "RRRR-MM-DD", hour 0-23).
// Okno: od (startDate, startHour) wlacznie do (endDate, endHour) wylacznie. title (opcjonalnie) = nazwa tego ostrzezenia.
inline uint8_t windAlertLevelAt(const char *date, int hour, const char **title = nullptr) {
  uint8_t best = 0;
  if (title) *title = nullptr;
  if (!date || !date[0]) return 0;
  for (uint8_t i = 0; i < msnWindAlertCount; i++) {
    const WindAlertWin &w = msnWindAlerts[i];
    int c1 = strcmp(date, w.startDate), c2 = strcmp(date, w.endDate);
    bool afterStart = c1 > 0 || (c1 == 0 && hour >= w.startHour);
    bool beforeEnd = c2 < 0 || (c2 == 0 && hour < w.endHour);
    if (afterStart && beforeEnd && w.level > best) {
      best = w.level;
      if (title) *title = w.title;
    }
  }
  return best;
}

struct MsnSnapshot {
  bool haveCurrent = false;
  float temp = 0, feels = 0;
  int humidity = -1;               // -1 = brak
  float pressureHpa = 0;           // 0 = brak
  float windMs = -1;               // -1 = brak
  int windDeg = -1;                // -1 = brak
  float gustMs = 0;
  int nowWmo = -1;  bool nowIsDay = true;    // symbol z bloku "current" ("odczyt teraz")
  int hourWmo = -1; bool hourIsDay = true;   // symbol pierwszego wpisu godzinowego (godzinowka)
  char curDate[11] = "";                      // data i godzina lokalna odczytu "teraz" (pole current.created)
  int curHour = -1;
  MsnDay days[MSN_MAX_DAYS];
  uint8_t dayCount = 0;
  MsnHour hours[MSN_MAX_HOURS];
  uint16_t hourCount = 0;
  MsnAlert alerts[MSN_MAX_ALERTS];
  uint8_t alertCount = 0;
};

struct MsnApplyResult {
  bool current = false;      // nalozono biezace dane
  bool icon = false;         // nalozono ikone
  bool today = false;        // nalozono min/max/opady na DZISIAJ
  uint8_t days = 0;          // ile dni prognozy zaktualizowano
  uint16_t hours = 0;        // ile wpisow godzinowych trafilo do tabel dnia
  bool nowRow = false;       // uzupelniono wiersz biezacej godziny wartosciami "teraz"
  bool gustFromMsn = false;  // alarm porywow policzony z godzinowki MSN
  uint8_t alertDays = 0;     // ile dni (z dzisiejszym) ma ostrzezenie
};

// Naklada dane MSN na to, co pokazuja ekrany. keepMeasurements = true, gdy pomiary
// pochodza ze stacji IMGW (wtedy z MSN bierzemy ikone, porywy, prognoze i godzinowke,
// a w wierszu biezacej godziny zostaja liczby widoczne na glownym ekranie).
// iconFromNow = ikona z bloku "teraz" zamiast z pierwszego wpisu godzinowego.
// dayIconFromNoon = ikony dni prognozy z godziny 12:00 (zgodne z tabela godzinowa); false = symbol dzienny MSN.
inline MsnApplyResult msnApplySnapshot(const MsnSnapshot &m, bool keepMeasurements, bool iconFromNow, bool dayIconFromNoon = true) {
  MsnApplyResult r;
  int iconWmo = -1;        // symbol uzyty do ikony na glownym ekranie (potem takze w wierszu biezacej godziny)
  bool iconIsDay = true;

  if (m.haveCurrent) {
    if (!keepMeasurements) {
      current.temp = m.temp;
      current.feelsLike = m.feels;
      if (m.humidity >= 0) current.humidity = m.humidity;
      if (m.pressureHpa > 0) current.pressure = m.pressureHpa;
      if (m.windMs >= 0) current.windSpeed = m.windMs;
      if (m.windDeg >= 0) current.windDeg = m.windDeg;
    }
    if (iconFromNow) {
      iconWmo = m.nowWmo >= 0 ? m.nowWmo : m.hourWmo;
      iconIsDay = m.nowWmo >= 0 ? m.nowIsDay : m.hourIsDay;
    } else {
      iconWmo = m.hourWmo >= 0 ? m.hourWmo : m.nowWmo;
      iconIsDay = m.hourWmo >= 0 ? m.hourIsDay : m.nowIsDay;
    }
    if (iconWmo >= 0) {
      current.icon = wmoToIcon(iconWmo, iconIsDay);
      r.icon = true;
    }
    r.current = true;
  }

  // Godzinowka (widok dnia): wpisy MSN nadpisuja godziny o tej samej dacie i godzinie; godziny,
  // ktorych MSN nie podal (np. juz minione), zostaja z bazy.
  auto hourSlot = [&](const char *date, int hour) -> HourPoint * {
    if (hour < 0 || hour > 23 || !date || !date[0]) return nullptr;
    if (todayDay.date[0] && strncmp(date, todayDay.date, 10) == 0) return &hourlyToday[hour];
    for (uint8_t d = 0; d < forecastCount && d < MAX_FORECAST; d++)
      if (forecast[d].date[0] && strncmp(date, forecast[d].date, 10) == 0) return &hourly[d][hour];
    return nullptr;
  };
  for (uint16_t i = 0; i < m.hourCount; i++) {
    const MsnHour &e = m.hours[i];
    HourPoint *hp = hourSlot(e.date, e.hour);
    if (!hp) continue;
    hp->valid = true;
    hp->temp = e.temp;
    hp->rain = e.rain;
    hp->windMs = e.windMs;
    hp->gustMs = e.gustMs;
    hp->windDir = e.windDir;
    hp->isDay = e.isDay;
    hp->code = (e.wmo >= 0) ? (uint8_t)e.wmo : 255;
    r.hours++;
  }

  // MSN zaczyna godzinowke od NASTEPNEJ pelnej godziny, wiec wiersza biezacej godziny w niej nie
  // ma. Wypelniamy go liczbami z glownego ekranu (te same temperatura, wiatr i ikona), zeby
  // podswietlony wiersz w widoku DZISIAJ zgadzal sie z tym, co widac na ekranie glownym.
  if (m.haveCurrent && m.curHour >= 0 && todayDay.date[0] && strncmp(m.curDate, todayDay.date, 10) == 0) {
    HourPoint &hp = hourlyToday[m.curHour];
    hp.valid = true;
    hp.temp = (int16_t)lroundf(current.temp);
    hp.windMs = current.windSpeed;
    hp.gustMs = max(hp.gustMs, m.gustMs);
    hp.windDir = current.windDeg;
    if (iconWmo >= 0) {
      hp.code = (uint8_t)iconWmo;
      hp.isDay = iconIsDay;
    }
    r.nowRow = true;
  }

  // Alarm porywow: gdy mamy godzinowke MSN na dzisiaj, liczymy go w calosci z MSN (spojnie z
  // tabela dnia): maksimum z porywow "teraz" i godzin od biezacej do konca doby.
  {
    bool haveTodayHours = false;
    float gMax = m.haveCurrent ? m.gustMs : 0.0f;
    int fromHour = (m.curHour >= 0) ? m.curHour : 0;
    for (uint16_t i = 0; i < m.hourCount; i++) {
      const MsnHour &e = m.hours[i];
      if (!todayDay.date[0] || strncmp(e.date, todayDay.date, 10) != 0) continue;
      haveTodayHours = true;
      if ((GUST_COUNT_PAST_HOURS || e.hour >= fromHour) && e.gustMs > gMax) gMax = e.gustMs;
    }
    if (haveTodayHours) {
      current.gustMs = gMax;
      r.gustFromMsn = true;
    } else if (m.haveCurrent && m.gustMs > current.gustMs) {
      current.gustMs = m.gustMs;            // brak godzin MSN: jak dotad - MSN moze tylko podniesc porywy z bazy
    }
  }

  // Dni dopasowujemy po DACIE (data wschodu slonca z MSN = lokalna data dnia). Gdy MSN w ogole
  // nie podal dat, dopasowanie idzie po numerze dnia (0 = dzis, 1 = jutro...).
  bool haveDates = false;
  for (uint8_t i = 0; i < m.dayCount; i++)
    if (m.days[i].date[0]) haveDates = true;

  auto findDay = [&](const char *date, uint8_t fallbackIdx) -> const MsnDay * {
    if (haveDates) {
      if (!date || !date[0]) return nullptr;
      for (uint8_t i = 0; i < m.dayCount; i++)
        if (m.days[i].ok && strncmp(m.days[i].date, date, 10) == 0) return &m.days[i];
      return nullptr;   // MSN ma daty, ale tej nie ma - zostaje Open-Meteo
    }
    return (fallbackIdx < m.dayCount && m.days[fallbackIdx].ok) ? &m.days[fallbackIdx] : nullptr;
  };

  if (todayDay.date[0]) {
    const MsnDay *d = findDay(todayDay.date, 0);
    if (d) {
      todayDay.tempMax = d->hi;
      todayDay.tempMin = d->lo;
      if (d->rain >= 0) todayDay.rainProb = d->rain;
      r.today = true;
    }
  }
  for (uint8_t i = 0; i < forecastCount && i < MAX_FORECAST; i++) {
    const MsnDay *d = findDay(forecast[i].date, i + 1);
    if (!d) continue;
    forecast[i].tempMax = d->hi;
    forecast[i].tempMin = d->lo;
    if (d->rain >= 0) forecast[i].rainProb = d->rain;
    // Ikona dnia. Domyslnie ta sama zasada co przy Open-Meteo: pogoda z godziny FORECAST_ICON_HOUR (poludnie), a nie
    // symbol dzienny MSN (jeden na cala dobe), zeby ikona zgadzala sie z tabela godzinowa widoczna po stukneciu w
    // dzien. Godziny juz sa nalozone wyzej (z MSN, a gdy MSN jej nie podal - z bazy). Zjawisko skrajne w dobie
    // (burza, grad, marznacy deszcz...) ma pierwszenstwo przed spokojnym poludniem. Opcja w panelu WWW
    // ("Ikona dnia prognozy") pozwala wybrac zamiast tego sam symbol dzienny MSN.
    int code = d->wmo;
    if (dayIconFromNoon) {
      const HourPoint &noon = hourly[i][FORECAST_ICON_HOUR];
      if (noon.valid && noon.code != 255) code = noon.code;
      if (d->wmo >= 0 && isSevereWmo(d->wmo)) code = d->wmo;
    }
    if (code >= 0) forecast[i].icon = wmoToIcon(code, true);
    r.days++;
  }

  // Ostrzezenia IMGW: kazdy dzien dostaje najwyzszy stopien sposrod ostrzezen, ktore go obejmuja
  // (porownanie dat jako tekstow RRRR-MM-DD). Ostrzezenie trwajace przez polnoc obejmuje oba dni.
  msnWindAlertCount = 0;                         // okna ostrzezen przed wiatrem (godzinowka, ekran glowny)
  for (uint8_t i = 0; i < m.alertCount && msnWindAlertCount < MSN_MAX_ALERTS; i++) {
    const MsnAlert &a = m.alerts[i];
    if (!msnIsWindAlert(a.title)) continue;
    WindAlertWin &w = msnWindAlerts[msnWindAlertCount++];
    w.level = a.level;
    snprintf(w.title, sizeof(w.title), "%s", a.title);
    snprintf(w.startDate, sizeof(w.startDate), "%s", a.startDate);
    snprintf(w.endDate, sizeof(w.endDate), "%s", a.endDate);
    w.startHour = a.startHour >= 0 ? a.startHour : 0;
    w.endHour = a.endHour >= 0 ? a.endHour : 24;
  }
  auto applyAlerts = [&](ForecastDay &fd, bool isToday) {
    fd.alertLevel = 0;
    fd.alertText[0] = 0;
    fd.windAlertLevel = 0;
    if (!fd.date[0]) return;
    uint8_t best = 0, count = 0;
    const MsnAlert *bestA = nullptr;
    for (uint8_t i = 0; i < m.alertCount; i++) {
      const MsnAlert &a = m.alerts[i];
      if (strcmp(a.startDate, fd.date) > 0 || strcmp(a.endDate, fd.date) < 0) continue;   // dzien poza zakresem
      // konczy sie dokladnie o polnocy (np. do 00:00 nastepnego dnia) - tego dnia juz nie dotyczy
      if (strcmp(a.endDate, fd.date) == 0 && a.endHour == 0 && strcmp(a.startDate, fd.date) < 0) continue;
      // dzisiaj: ostrzezenie, ktore juz sie skonczylo (godzina konca <= biezaca), pomijamy
      if (isToday && m.curHour >= 0 && strcmp(a.endDate, fd.date) == 0 && a.endHour >= 0 && a.endHour <= m.curHour) continue;
      count++;
      if (msnIsWindAlert(a.title) && a.level > fd.windAlertLevel) fd.windAlertLevel = a.level;
      if (a.level > best) {
        best = a.level;
        bestA = &a;
      }
    }
    if (!bestA) return;
    fd.alertLevel = best;
    if (count > 1) snprintf(fd.alertText, sizeof(fd.alertText), "%s +%u", bestA->title, (unsigned)(count - 1));
    else snprintf(fd.alertText, sizeof(fd.alertText), "%s", bestA->title);
    r.alertDays++;
  };
  applyAlerts(todayDay, true);
  for (uint8_t i = 0; i < forecastCount && i < MAX_FORECAST; i++) applyAlerts(forecast[i], false);
  return r;
}

// ---------------------------------------------------------------------
//  Czesc sieciowa i JSON (tylko gdy dostepna biblioteka ArduinoJson)
// ---------------------------------------------------------------------
#ifdef ARDUINOJSON_VERSION

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Klucz klienta wbudowany w aplikacje MSN (nie jest to klucz przydzielony Tobie - patrz uwaga
// na gorze pliku). Gdyby MSN go zmienil, wystarczy podmienic te dwie wartosci.
static const char MSN_APP_ID[]  = "9e21380c-ff19-4c78-b4ea-19558e93a5d3";
static const char MSN_API_KEY[] = "j5i4gDqHL6nGYwx5wi5kRhXjtf2c5qgFX9fzfk0TOo";

// GET z naglowkami przegladarki + parsowanie z filtrem (odpowiedz MSN jest duza, a potrzebujemy
// z niej kilkunastu pol). true tylko gdy HTTP 200 i poprawny JSON.
inline bool msnHttpGetJson(const String &url, JsonDocument &doc, const JsonDocument &filter) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != 200) {
    Serial.printf("MSN: HTTP %d\n", code);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len > MSN_MAX_PAYLOAD) {
    Serial.printf("MSN: odpowiedz zbyt duza (%d B) - pomijam\n", len);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

#if MSN_DEBUG_PAYLOAD
  static bool dumped = false;
  if (!dumped) {
    dumped = true;
    Serial.printf("MSN: rozmiar odpowiedzi %u B. Poczatek:\n%.1200s\n", (unsigned)payload.length(), payload.c_str());
    const char *frag[] = {"hourly", "daily", "almanac", "units"};
    const int fragLen[] = {700, 900, 250, 300};
    for (int k = 0; k < 4; k++) {
      String key = String("\"") + frag[k] + "\"";
      int h = payload.indexOf(key);
      if (h >= 0) Serial.printf("MSN: fragment od \"%s\" (%d znakow):\n%s\n", frag[k], fragLen[k], payload.substring(h, h + fragLen[k]).c_str());
      else Serial.printf("MSN: w odpowiedzi nie ma klucza \"%s\"\n", frag[k]);
    }
  }
#endif

  // Odpowiedz MSN zagniezdza dane glebiej niz domyslny limit ArduinoJson (10 poziomow, np.
  // forecast > days > daily > day > summaries[...]) - bez podniesienia limitu blad "TooDeep".
  // Filtr nie pomaga, bo parser liczy glebokosc calego wejscia, takze pomijanych pol.
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(40));
  if (err) {
    Serial.printf("MSN: blad JSON: %s\n", err.c_str());
    return false;
  }
  return true;
}

// Wyciaga z odpowiedzi MSN pola, ktorych potrzebujemy. Nazwy pol wg odpowiedzi "overview"
// (te same, ktorych uzywa OpenATV) i dokumentacji konektora MSN Weather.
inline bool msnParse(JsonDocument &doc, MsnSnapshot &m) {
  JsonObject w = doc["responses"][0]["weather"][0].as<JsonObject>();
  if (w.isNull()) return false;
  const char *spdUnit = doc["units"]["speed"] | "";
  const char *prUnit = doc["units"]["pressure"] | "";

  JsonObject cur = w["current"].as<JsonObject>();
  if (!cur.isNull() && !cur["temp"].isNull()) {
    m.haveCurrent = true;
    m.temp = cur["temp"].as<float>();
    m.feels = cur["feels"].isNull() ? m.temp : cur["feels"].as<float>();
    if (!cur["rh"].isNull()) m.humidity = (int)lroundf(cur["rh"].as<float>());
    if (!cur["baro"].isNull()) m.pressureHpa = msnPressureToHpa(cur["baro"].as<float>(), prUnit);
    if (!cur["windSpd"].isNull()) m.windMs = msnSpeedToMs(cur["windSpd"].as<float>(), spdUnit);
    if (!cur["windDir"].isNull()) m.windDeg = cur["windDir"].as<int>();
    if (!cur["windGust"].isNull()) m.gustMs = msnSpeedToMs(cur["windGust"].as<float>(), spdUnit);
    m.nowWmo = msnSymbolToWmo(cur["symbol"] | "", m.nowIsDay);
    const char *created = cur["created"] | "";
    msnDateFromIso(created, m.curDate);
    m.curHour = msnHourFromIso(created);
  }

  // ostrzezenia IMGW ("alerts"): stopien z pola level/significance, czas lokalny start/end
  for (JsonObject a : w["alerts"].as<JsonArray>()) {
    if (m.alertCount >= MSN_MAX_ALERTS) break;
    MsnAlert e;
    e.level = msnAlertLevel(a["level"] | "", a["significance"] | "");
    if (e.level == 0) continue;
    // nazwa bez nazwy koloru (kolor widac z barwy napisu): pole "event", a gdy go brak - "title" bez koncowki " - Żółty"
    msnAlertName(a["event"] | "", a["title"] | "", e.title, sizeof(e.title));
    const char *st = a["start"] | "";
    const char *en = a["end"] | "";
    if (!msnDateFromIso(st, e.startDate) || !msnDateFromIso(en, e.endDate)) continue;
    e.startHour = (int8_t)msnHourFromIso(st);
    e.endHour = (int8_t)msnHourFromIso(en);
    m.alerts[m.alertCount++] = e;
  }

  JsonArray days = w["forecast"]["days"].as<JsonArray>();
  uint8_t n = 0;
  for (JsonObject d : days) {
    if (n >= MSN_MAX_DAYS) break;
    MsnDay &md = m.days[n];
    JsonObject daily = d["daily"].as<JsonObject>();
    if (!daily.isNull() && !daily["tempHi"].isNull() && !daily["tempLo"].isNull()) {
      md.ok = true;
      md.hi = daily["tempHi"].as<float>();
      md.lo = daily["tempLo"].as<float>();
      if (!daily["precip"].isNull()) md.rain = (int)lroundf(daily["precip"].as<float>());
      else if (!daily["day"]["precip"].isNull()) md.rain = (int)lroundf(daily["day"]["precip"].as<float>());
      bool dummy;
      md.wmo = msnSymbolToWmo(daily["symbol"] | "", dummy);
    }
    msnDateFromIso(d["almanac"]["sunrise"] | "", md.date);

    // wpisy godzinowe tego dnia ("valid" = czas lokalny, np. 2026-10-06T17:00:00+02:00)
    JsonArray hr = d["hourly"].as<JsonArray>();
    for (JsonObject h : hr) {
      if (m.hourCount >= MSN_MAX_HOURS) break;
      if (h["temp"].isNull()) continue;
      const char *valid = h["valid"] | "";
      int hh = msnHourFromIso(valid);
      MsnHour e;
      if (hh < 0 || !msnDateFromIso(valid, e.date)) continue;
      e.hour = (int8_t)hh;
      e.temp = (int16_t)lroundf(h["temp"].as<float>());
      if (!h["precip"].isNull()) {
        long pr = lroundf(h["precip"].as<float>());
        e.rain = (int8_t)(pr < 0 ? 0 : (pr > 100 ? 100 : pr));
      }
      if (!h["windSpd"].isNull()) e.windMs = msnSpeedToMs(h["windSpd"].as<float>(), spdUnit);
      if (!h["windDir"].isNull()) e.windDir = (int16_t)h["windDir"].as<int>();
      if (!h["windGust"].isNull()) e.gustMs = msnSpeedToMs(h["windGust"].as<float>(), spdUnit);
      bool hourIsDay = true;
      e.wmo = (int8_t)msnSymbolToWmo(h["symbol"] | "", hourIsDay);
      e.isDay = hourIsDay;
      if (m.hourCount == 0 && e.wmo >= 0) {     // pierwszy wpis = "ikona z godzinowki" na glownym ekranie
        m.hourWmo = e.wmo;
        m.hourIsDay = e.isDay;
      }
      m.hours[m.hourCount++] = e;
    }
    n++;
  }
  m.dayCount = n;
  return m.haveCurrent || n > 0 || m.hourCount > 0;
}

// Pobiera dane z MSN i naklada je na biezace dane pogodowe (patrz opis na gorze pliku).
// Nigdy nie psuje danych bazowych: przy bledzie zostaje Open-Meteo. true = nalozono dane MSN.
inline bool applyMsnWeather(bool keepMeasurements) {
  AppSettings &s = settingsStore.settings;
  if (!geoReady) return false;

  char url[460];
  snprintf(url, sizeof(url),
           "https://api.msn.com/weatherfalcon/weather/overview?&lon=%.4f&lat=%.4f&locale=pl-pl&units=%s"
           "&appId=%s&apiKey=%s&ocid=superapp-mini-weather&wrapOData=false&includenowcasting=true"
           "&feature=lifeday&lifeDays=6",
           cityLon, cityLat, (s.units == "imperial") ? "F" : "C", MSN_APP_ID, MSN_API_KEY);

  // Filtr: z calej odpowiedzi zostaja tylko pola, ktorych uzywamy
  JsonDocument filter;
  filter["units"]["speed"] = true;
  filter["units"]["pressure"] = true;
  filter["responses"][0]["weather"][0]["current"]["temp"] = true;
  filter["responses"][0]["weather"][0]["current"]["feels"] = true;
  filter["responses"][0]["weather"][0]["current"]["rh"] = true;
  filter["responses"][0]["weather"][0]["current"]["baro"] = true;
  filter["responses"][0]["weather"][0]["current"]["windSpd"] = true;
  filter["responses"][0]["weather"][0]["current"]["windDir"] = true;
  filter["responses"][0]["weather"][0]["current"]["windGust"] = true;
  filter["responses"][0]["weather"][0]["current"]["symbol"] = true;
  filter["responses"][0]["weather"][0]["current"]["created"] = true;
  filter["responses"][0]["weather"][0]["alerts"][0]["level"] = true;
  filter["responses"][0]["weather"][0]["alerts"][0]["significance"] = true;
  filter["responses"][0]["weather"][0]["alerts"][0]["title"] = true;
  filter["responses"][0]["weather"][0]["alerts"][0]["event"] = true;
  filter["responses"][0]["weather"][0]["alerts"][0]["start"] = true;
  filter["responses"][0]["weather"][0]["alerts"][0]["end"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["daily"]["tempHi"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["daily"]["tempLo"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["daily"]["precip"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["daily"]["symbol"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["daily"]["day"]["precip"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["almanac"]["sunrise"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["symbol"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["valid"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["temp"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["precip"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["windSpd"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["windDir"] = true;
  filter["responses"][0]["weather"][0]["forecast"]["days"][0]["hourly"][0]["windGust"] = true;

  auto fail = [&](const char *why) {
    Serial.printf("MSN: %s - zostaja dane Open-Meteo\n", why);
    if (keepMeasurements) {
      size_t len = strlen(weatherStatus);
      snprintf(weatherStatus + len, sizeof(weatherStatus) - len, L(" | MSN: %s - prognoza z Open-Meteo", " | MSN: %s - forecast from Open-Meteo"), why);
    } else {
      snprintf(weatherStatus, sizeof(weatherStatus), L("MSN: %s - używam Open-Meteo", "MSN: %s - using Open-Meteo"), why);
    }
    return false;
  };

  JsonDocument doc;
  if (!msnHttpGetJson(String(url), doc, filter)) return fail(L("błąd pobierania danych", "download error"));

  // Migawka jest duza (ok. 5 kB) - trzymamy ja poza stosem (stos zadania Arduino jest maly,
  // a TLS potrzebuje go sporo). Placement new = czysty stan przy kazdym wywolaniu.
  static MsnSnapshot snapStore;
  new (&snapStore) MsnSnapshot();
  MsnSnapshot &m = snapStore;
  if (!msnParse(doc, m)) return fail(L("nieoczekiwany format odpowiedzi", "unexpected response format"));
  doc.clear();

  MsnApplyResult r = msnApplySnapshot(m, keepMeasurements, s.iconSource == "now", s.msnDayIcon != "daily");
  Serial.printf("MSN: temp %.1f (odczuwalna %.1f), wilg. %d%%, cisn. %.0f hPa, wiatr %.1f m/s (porywy %.1f), symbole: teraz=%d godz.=%d\n",
                m.temp, m.feels, m.humidity, m.pressureHpa, m.windMs, m.gustMs, m.nowWmo, m.hourWmo);
  for (uint8_t i = 0; i < m.dayCount; i++)
    Serial.printf("MSN dzien %u: %s  max %.1f  min %.1f  opady %d%%  wmo %d%s\n", i, m.days[i].date, m.days[i].hi, m.days[i].lo,
                  m.days[i].rain, m.days[i].wmo, m.days[i].ok ? "" : "  (brak danych)");
  if (m.hourCount > 0)
    Serial.printf("MSN godzinowka: %u wpisow (od %s %02d:00 do %s %02d:00), teraz: %s %02d:00\n", m.hourCount, m.hours[0].date, m.hours[0].hour,
                  m.hours[m.hourCount - 1].date, m.hours[m.hourCount - 1].hour, m.curDate, m.curHour);
  for (uint8_t i = 0; i < m.alertCount; i++)
    Serial.printf("MSN ostrzezenie: %s (stopien %u), od %s %02d:00 do %s %02d:00\n", m.alerts[i].title, m.alerts[i].level, m.alerts[i].startDate,
                  m.alerts[i].startHour, m.alerts[i].endDate, m.alerts[i].endHour);
  Serial.printf("MSN: nalozono: biezace=%d ikona=%d dzis=%d dni=%u godzin=%u wiersz_teraz=%d porywy_z_MSN=%d dni_z_ostrzezeniem=%u\n", r.current, r.icon,
                r.today, r.days, r.hours, r.nowRow, r.gustFromMsn, r.alertDays);

  if (!r.current && !r.today && r.days == 0 && r.hours == 0) return fail(L("brak pasujących danych", "no matching data"));

  if (keepMeasurements) {
    size_t len = strlen(weatherStatus);
    snprintf(weatherStatus + len, sizeof(weatherStatus) - len, L(" | ikona, prognoza i godzinówka: MSN", " | icon, forecast and hourly: MSN"));
  } else {
    snprintf(weatherStatus, sizeof(weatherStatus),
             "%s", L("Pomiary, prognoza i godzinówka: MSN Pogoda (trend ciśnienia i jakość powietrza: Open-Meteo)",
                     "Readings, forecast and hourly: MSN Weather (pressure trend and air quality: Open-Meteo)"));
  }
  return true;
}

#endif  // ARDUINOJSON_VERSION
