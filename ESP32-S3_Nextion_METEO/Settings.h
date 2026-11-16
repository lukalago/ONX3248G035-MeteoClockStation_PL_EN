#pragma once
#include <Preferences.h>

// ---------- Kolory ----------
// Jedna, stala paleta (czarne tlo, bialy tekst, cyjanowy akcent). Motywy kolorystyczne i wybor
// koloru tla zostaly usuniete. Struktura zostaje, bo przyjmuja ja funkcje rysowania i ikony.
struct ThemeColors {
  uint16_t bg;
  uint16_t text;
  uint16_t accent;
  uint16_t muted;
  uint16_t divider;
};

ThemeColors getThemeColors();

// ---------- Ustawienia aplikacji ----------
struct AppSettings {
  // WiFi
  String wifiSsid;
  String wifiPassword;
  String wifiSsid2;       // sieci zapasowe (puste = brak) - proba polaczenia, gdy glownej nie ma w zasiegu
  String wifiPassword2;
  String wifiSsid3;
  String wifiPassword3;

  // Pogoda (Open-Meteo - bez klucza API)
  String city;
  String countryCode;
  String lat;          // opcjonalna szerokosc geograficzna (tekst, np. "53.4285"); puste = geokodowanie po nazwie miasta
  String lon;          // opcjonalna dlugosc geograficzna; uzywana tylko razem z lat
  String units;        // "metric" / "imperial"
  String weatherSource; // zrodlo pogody i prognozy: "openmeteo" (domyslne) / "msn" (nieoficjalne, naklada sie na Open-Meteo)
  String msnDayIcon;   // ikona dnia prognozy: "noon" (z godziny 12:00, zgodna z godzinowka) / "daily" (symbol dzienny MSN, tylko MSN) / "flow" (z przebiegu dnia 8-20: slonce z chmura i kroplami przy przelotnych opadach)
  String weatherModel; // id modelu Open-Meteo ("best_match" = automatyczny)
  String measureSource; // zrodlo biezacych pomiarow: "openmeteo" / "imgw"
  String imgwStation;   // nazwa stacji IMGW (puste = nazwa miasta)
  String iconSource;    // skad ikona aktualnej pogody: "hourly" (seria godzinowa) / "now" (odczyt "teraz")
  bool showPressure;    // dolna linia: cisnienie + trend
  bool showRain;        // dolna linia: kropla + % opadow dzis
  bool showAqi;         // dolna linia: AQI + kropka
  bool showWind;        // dolna linia: kierunek + predkosc wiatru (roza / Ksiezyc - patrz rightWidget)
  bool showHint;        // dodatkowa informacja nad paskiem komfortu (np. "Zimno - lokalne przymrozki")
  bool showSunHours;    // w kolumnach prognozy: liczba godzin ze sloncem pod szansa opadow
  bool showBattery;     // ikona baterii (poziom naladowania + ladowanie) w lewym gornym rogu
  String rightWidget;   // prawa strona bloku pogody na ekranie glownym: "wind" (roza wiatru) / "moon" (faza Ksiezyca)
  String windUnit;      // jednostka predkosci wiatru na ekranie: "ms" / "kmh"

  // Zegar / format
  bool use24h;
  String tz;            // np. "CET-1CEST,M3.5.0,M10.5.0/3"
  String hostname;      // nazwa w sieci: <hostname>.local (mDNS) i nazwa w routerze; male litery, cyfry, '-'

  // Poprawna nazwa hosta: male litery a-z, cyfry, '-' (nie na poczatku/koncu), max 32 znaki; inaczej "meteo"
  static String sanitizeHostname(const String &in) {
    String out;
    for (size_t i = 0; i < in.length() && out.length() < 32; i++) {
      char c = in[i];
      if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
      else if ((c == '-' || c == ' ' || c == '_') && out.length() && out[out.length() - 1] != '-') out += '-';
    }
    while (out.length() && out[out.length() - 1] == '-') out.remove(out.length() - 1);
    return out.length() ? out : String("meteo");
  }

  // Wygląd
  uint8_t brightnessDay;
  uint8_t brightnessNight;
  String nightStart;
  String nightEnd;

  // Praca na baterii (oszczedzanie energii)
  uint8_t brightnessBattery;   // gorny limit jasnosci podswietlenia na baterii [%]
  uint8_t fetchMin;            // co ile minut pobierac pogode na zasilaczu (10..60)
  uint8_t lang;                // jezyk ekranu i panelu: 0 = polski, 1 = English (Lang.h)
  uint8_t batteryFetchMin;     // co ile minut pobierac pogode na baterii (Wi-Fi wlaczane tylko na pobranie)
  bool usbSense;               // przerobka: wykrywanie zasilacza USB na P7 ekspandera (dzielnik z VBUS)

  // Prognoza
  uint8_t forecastDays;

  // Wywoz smieci: lista terminow, w kazdej linii data i (opcjonalnie) frakcja, np. "23.10 zmieszane"
  String wasteDates;
};

// Modele pogodowe Open-Meteo dostepne do wyboru w panelu (id do parametru
// &models= oraz etykieta). "best_match" = domyslny dobor automatyczny.
static const char *const WEATHER_MODELS[][3] = {        // id, etykieta PL, etykieta EN
  {"best_match",           "Automatyczny (domyślny)",                                   "Automatic (default)"},
  {"icon_seamless",        "DWD ICON – łączony (D2/EU/globalny)",                       "DWD ICON – combined (D2/EU/global)"},
  {"icon_eu",              "DWD ICON-EU – Europa (~6 km)",                              "DWD ICON-EU – Europe (~6 km)"},
  {"icon_d2",              "DWD ICON-D2 – Europa Środkowa (~2 km, ograniczony obszar)", "DWD ICON-D2 – Central Europe (~2 km, limited area)"},
  {"ecmwf_ifs025",         "ECMWF IFS – europejski, globalny",                          "ECMWF IFS – European, global"},
  {"meteofrance_seamless", "Météo-France – łączony (AROME/ARPEGE)",                     "Météo-France – combined (AROME/ARPEGE)"},
  {"gfs_seamless",         "NOAA GFS – amerykański, globalny",                          "NOAA GFS – American, global"},
};
static const size_t WEATHER_MODELS_COUNT = sizeof(WEATHER_MODELS) / sizeof(WEATHER_MODELS[0]);

inline bool isKnownWeatherModel(const String &id) {
  for (size_t i = 0; i < WEATHER_MODELS_COUNT; i++) {
    if (id == WEATHER_MODELS[i][0]) return true;
  }
  return false;
}

class SettingsStore {
public:
  void begin() {
    _prefs.begin("wclock", false);
    load();
  }

  void load() {
    settings.wifiSsid     = _prefs.getString("ssid", "");
    settings.wifiPassword = _prefs.getString("pass", "");
    settings.wifiSsid2     = _prefs.getString("ssid2", "");
    settings.wifiPassword2 = _prefs.getString("pass2", "");
    settings.wifiSsid3     = _prefs.getString("ssid3", "");
    settings.wifiPassword3 = _prefs.getString("pass3", "");
    settings.city         = _prefs.getString("city", "Warszawa");
    settings.countryCode  = _prefs.getString("country", "PL");
    settings.lat          = _prefs.getString("lat", "");
    settings.lon          = _prefs.getString("lon", "");
    settings.units        = _prefs.getString("units", "metric");
    settings.weatherSource = _prefs.getString("wsrc", "openmeteo");
    if (settings.weatherSource != "msn") settings.weatherSource = "openmeteo";
    settings.msnDayIcon = _prefs.getString("msnico", "noon");
    if (settings.msnDayIcon != "daily" && settings.msnDayIcon != "flow") settings.msnDayIcon = "noon";
    settings.weatherModel = _prefs.getString("wmodel", "best_match");
    settings.measureSource = _prefs.getString("msrc", "openmeteo");
    if (settings.measureSource != "imgw") settings.measureSource = "openmeteo";
    settings.imgwStation  = _prefs.getString("imgwst", "");
    settings.iconSource   = _prefs.getString("icosrc", "hourly");
    if (settings.iconSource != "now") settings.iconSource = "hourly";
    settings.showPressure = _prefs.getBool("shpress", true);
    settings.showRain     = _prefs.getBool("shrain", true);
    settings.showAqi      = _prefs.getBool("shaqi", true);
    settings.showWind     = _prefs.getBool("shwind", true);
    settings.showHint     = _prefs.getBool("shhint", true);
    settings.showBattery  = _prefs.getBool("showbat", true);
    settings.showSunHours = _prefs.getBool("shsun", true);
    settings.rightWidget  = _prefs.getString("rwidget", "wind");
    if (settings.rightWidget != "moon") settings.rightWidget = "wind";
    settings.windUnit     = _prefs.getString("wunit", "ms");
    if (settings.windUnit != "kmh") settings.windUnit = "ms";
    if (!isKnownWeatherModel(settings.weatherModel)) settings.weatherModel = "best_match";
    settings.use24h       = _prefs.getBool("use24h", true);
    settings.tz           = _prefs.getString("tz", "CET-1CEST,M3.5.0,M10.5.0/3");
    settings.hostname     = AppSettings::sanitizeHostname(_prefs.getString("host", "meteo"));
    settings.brightnessDay   = _prefs.getUChar("brDay", 90);
    settings.brightnessNight = _prefs.getUChar("brNight", 15);
    settings.nightStart   = _prefs.getString("nStart", "21:00");
    settings.nightEnd     = _prefs.getString("nEnd", "07:00");
    settings.brightnessBattery = _prefs.getUChar("brBat", 30);
    if (settings.brightnessBattery < 5 || settings.brightnessBattery > 100) settings.brightnessBattery = 30;
    settings.usbSense = _prefs.getBool("usbsense", false);
    settings.fetchMin = _prefs.getUChar("fetchMin", 10);
    if (settings.fetchMin < 10 || settings.fetchMin > 60) settings.fetchMin = 10;
    settings.lang = _prefs.getUChar("lang", 0) == 1 ? 1 : 0;
    settings.batteryFetchMin = _prefs.getUChar("batFetch", 30);
    if (settings.batteryFetchMin < 10 || settings.batteryFetchMin > 120) settings.batteryFetchMin = 30;
    settings.forecastDays = _prefs.getUChar("fcDays", 4);
    settings.wasteDates   = _prefs.getString("waste", "");
  }

  void save() {
    _prefs.putString("ssid", settings.wifiSsid);
    _prefs.putString("pass", settings.wifiPassword);
    _prefs.putString("ssid2", settings.wifiSsid2);
    _prefs.putString("pass2", settings.wifiPassword2);
    _prefs.putString("ssid3", settings.wifiSsid3);
    _prefs.putString("pass3", settings.wifiPassword3);
    _prefs.putString("city", settings.city);
    _prefs.putString("country", settings.countryCode);
    _prefs.putString("lat", settings.lat);
    _prefs.putString("lon", settings.lon);
    _prefs.putString("units", settings.units);
    _prefs.putString("wsrc", settings.weatherSource);
    _prefs.putString("msnico", settings.msnDayIcon);
    _prefs.putString("wmodel", settings.weatherModel);
    _prefs.putString("msrc", settings.measureSource);
    _prefs.putString("imgwst", settings.imgwStation);
    _prefs.putString("icosrc", settings.iconSource);
    _prefs.putBool("shpress", settings.showPressure);
    _prefs.putBool("shrain", settings.showRain);
    _prefs.putBool("shaqi", settings.showAqi);
    _prefs.putBool("shwind", settings.showWind);
    _prefs.putBool("shhint", settings.showHint);
    _prefs.putBool("showbat", settings.showBattery);
    _prefs.putBool("shsun", settings.showSunHours);
    _prefs.putString("rwidget", settings.rightWidget);
    _prefs.putString("wunit", settings.windUnit);
    _prefs.putBool("use24h", settings.use24h);
    _prefs.putString("tz", settings.tz);
    _prefs.putString("host", AppSettings::sanitizeHostname(settings.hostname));
    _prefs.putUChar("brDay", settings.brightnessDay);
    _prefs.putUChar("brNight", settings.brightnessNight);
    _prefs.putString("nStart", settings.nightStart);
    _prefs.putString("nEnd", settings.nightEnd);
    _prefs.putUChar("brBat", settings.brightnessBattery);
    _prefs.putUChar("fetchMin", settings.fetchMin);
    _prefs.putUChar("lang", settings.lang);
    _prefs.putUChar("batFetch", settings.batteryFetchMin);
    _prefs.putBool("usbsense", settings.usbSense);
    _prefs.putUChar("fcDays", settings.forecastDays);
    _prefs.putString("waste", settings.wasteDates);
  }

  AppSettings settings;

private:
  Preferences _prefs;
};

constexpr uint16_t rgb565(uint32_t rgb888) {
  uint8_t r = (rgb888 >> 16) & 0xFF;
  uint8_t g = (rgb888 >> 8) & 0xFF;
  uint8_t b = rgb888 & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

inline ThemeColors getThemeColors() {
  return { rgb565(0x000000), rgb565(0xFFFFFF), rgb565(0x07C8D8), rgb565(0x8A8A8A), rgb565(0x2A2A2A) };   // tlo, tekst, akcent, wyciszony, separator
}
