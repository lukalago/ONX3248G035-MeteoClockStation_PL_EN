// =====================================================================
//  Stacja Meteo — ONX3248G035 (Open Nextion Genius, ESP32-S3, ST7796 SPI)
//  Zegar + pogoda/prognoza + ostrzeżenia + jakość powietrza + BME280 + panel WWW, PL/EN
// =====================================================================

#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_CST8XX.h>
#include <Adafruit_PCF8574.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <ESPmDNS.h>     // adres <nazwa>.local (nazwa w panelu WWW, domyslnie meteo)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_mac.h>

#include <Adafruit_GFX.h>
// Czcionki: kompletne zestawy ASCII + polskie litery (kody 0x80-0x91) - pliki *Pl.h obok szkicu,
// WYGLADZONE (4 bity krycia na piksel, tools/gen_fonts_aa.py) - rysuje je wylacznie PolishText.h
#include "FreeSans9pt7bPl.h"
#include "FreeSansBold12pt7bPl.h"
#include "FreeSansBold18pt7bPl.h"
#include "FreeSansBold24pt7bPl.h"

#include "BoardConfig.h"
#include "Lang.h"        // jezyk PL / EN: L("polski", "English")
#include "Settings.h"
#include "WeatherIcons.h"
#define FW_VERSION "3.0.1"   // pokazywana w naglowku panelu WWW
#include "WebPanel.h"
#include "PolishText.h"   // polskie znaki i rysowanie tekstu (printAt, printCentered, ...)
#include "AaShapes.h"     // wygladzone kola, luki, kreski i trojkaty (aaDraw, aaFillCircle, ...)
#include "StQrCodeGen.h"  // kody QR na ekranie trybu konfiguracji (biblioteka Nayuki, MIT)

// Skroty do czcionek uzywanych w calym projekcie (zamiast setTextSize)
#define FONT_SMALL &FreeSans9pt7bPl        // data, opis pogody, linie pomocnicze
#define FONT_MED   &FreeSans9pt7bPl        // dni prognozy, temp. prognozy, naglowki
#define FONT_VALUE &FreeSansBold12pt7bPl   // wartosci w kartach ekranu "Powietrze i odczucia" (AQI, UV, odczuwalna, wilgotnosc, cisnienie)
#define FONT_LARGE &FreeSansBold18pt7bPl   // tytul ekranow startowych, "Polaczono!", opis AQI i indeks UV
#define FONT_HUGE  &FreeSansBold24pt7bPl
#define FONT_DAY_TITLE &FreeSansBold12pt7bPl   // nazwa dnia na ekranie szczegolow

// TRYB TESTOWY CZCIONEK: 1 = po starcie ekran z polskimi wersalikami obok zwyklych liter (z naniesiona
// linia bazowa) + metryki glifow w Serial Monitorze; ekran trzyma sie 30 s. Normalnie 0.
#define PL_FONT_TEST 0
#if PL_FONT_TEST
#include "PolishTextTest.h"
#endif

uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t);
ThemeColors currentTheme();

// ---------------------------------------------------------------------
//  Wyświetlacz
// ---------------------------------------------------------------------
Arduino_DataBus *bus = new Arduino_HWSPI(
    LCD_DC_PIN, LCD_CS_PIN, LCD_SCLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN, &SPI, true);
Arduino_GFX *gfx = new Arduino_ST7796(
    bus, GFX_NOT_DEFINED, 0, false, LCD_H_RES, LCD_V_RES);

Adafruit_CST8XX touch;
Adafruit_PCF8574 ioExpander;
Adafruit_BME280 bme;
bool bmeReady = false;
bool touchReady = false;

static const uint32_t LCD_SPI_FREQ = 80000000;

SettingsStore settingsStore;
WebPanel webPanel;
bool apMode = false;

// Nazwy dni/miesiecy PL i EN (UTF-8 z polskimi literami - rysowane czcionkami *Pl.h przez PolishText.h;
// wlasne tablice, bo strftime() nie ma polskiego locale). Jezyk wybiera g_lang (Lang.h).
const char *const DAY_NAMES_FULL_PL[7]  = {"Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota"};
const char *const DAY_NAMES_FULL_EN[7]  = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const DAY_NAMES_SHORT_PL[7] = {"Nd", "Pon", "Wt", "Śr", "Czw", "Pt", "Sob"};
const char *const DAY_NAMES_SHORT_EN[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const MONTH_NAMES_PL[12]    = {"Stycznia", "Lutego", "Marca", "Kwietnia", "Maja", "Czerwca",
                                           "Lipca", "Sierpnia", "Września", "Października", "Listopada", "Grudnia"};
const char *const MONTH_NAMES_EN[12]    = {"January", "February", "March", "April", "May", "June",
                                           "July", "August", "September", "October", "November", "December"};
const char *const MONTH_SHORT_EN[12]    = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *dayNameFull(int wday)  { return (g_lang == 1 ? DAY_NAMES_FULL_EN : DAY_NAMES_FULL_PL)[wday % 7]; }
const char *dayNameShort(int wday) { return (g_lang == 1 ? DAY_NAMES_SHORT_EN : DAY_NAMES_SHORT_PL)[wday % 7]; }
const char *monthName(int mon)     { return (g_lang == 1 ? MONTH_NAMES_EN : MONTH_NAMES_PL)[mon % 12]; }

// ---------------------------------------------------------------------
//  Dane pogodowe
// ---------------------------------------------------------------------
struct CurrentWeather {
  bool valid = false;
  float temp = 0;
  float feelsLike = 0;
  int humidity = 0;
  float pressure = 0;
  int windDeg = 0;
  float windSpeed = 0;
  float gustMs = 0;   // maks. porywy [m/s]: teraz i w najblizszych godzinach (alarm wiatru)
  bool hasPressTrend = false;  // czy znamy trend cisnienia z serii godzinowej API
  int8_t pressTrend = 0;       // trend cisnienia: 1 = rosnie, -1 = spada, 0 = stabilne
  String icon;
};

struct ForecastDay {
  String label;
  String icon;
  float tempMin = 1000;
  float tempMax = -1000;
  int rainProb = -1;   // maks. prawdopodobienstwo opadow w dobie [%], -1 = brak danych
  int sunHours = -1;   // prognozowana liczba godzin ze sloncem w dobie (zaokraglona), -1 = brak danych
  int16_t sunriseMin = -1;   // wschod slonca tego dnia [minuty od polnocy], -1 = brak
  int16_t sunsetMin = -1;    // zachod slonca tego dnia [minuty od polnocy], -1 = brak
  char date[11] = "";  // "RRRR-MM-DD" - do dopasowania danych godzinowych
  uint8_t wday = 0;    // dzien tygodnia (0 = niedziela) - nazwa na ekranie szczegolow
  uint8_t alertLevel = 0;      // ostrzezenie IMGW z MSN na ten dzien: 0 brak, 1 zolty, 2 pomaranczowy, 3 czerwony
  char alertText[44] = "";     // nazwa najwyzszego ostrzezenia, np. "Mgla - Zolty" (+ " +N" gdy jest wiecej)
  uint8_t windAlertLevel = 0;  // najwyzszy stopien ostrzezenia przed wiatrem na ten dzien (0 = brak)
};

// Jedna godzina prognozy - dane do ekranu szczegolow dnia
struct HourPoint {
  bool valid = false;     // false = model nie podal danych na te godzine
  bool isDay = true;
  int8_t rain = -1;       // prawdopodobienstwo opadow [%], -1 = brak
  int16_t temp = 0;
  int16_t windDir = 0;    // skad wieje [stopnie]
  float windMs = 0;       // m/s
  float gustMs = 0;       // porywy [m/s], 0 = brak danych
  uint8_t code = 255;     // kod pogody WMO, 255 = brak
};

CurrentWeather current;

// Ostrzezenie o silnym wietrze: przy zrodle MSN - ostrzezenia IMGW (windAlertLevelAt, ForecastDay::windAlertLevel);
// przy innych zrodlach (Open-Meteo, albo gdy MSN zawiedzie) - z prognozy wybranego zrodla wg progow zblizonych
// do kryteriow ostrzezen IMGW: srednia predkosc / porywy [m/s] dla stopnia 1, 2, 3.
static const float WIND_WARN_MEAN[3] = {15.0f, 20.0f, 25.0f};
static const float WIND_WARN_GUST[3] = {19.5f, 25.0f, 32.0f};   // ok. 70 / 90 / 115 km/h
bool windFromImgw = false;   // true = ostrzezenia o wietrze z IMGW (zrodlo MSN), false = z danych prognozy

uint8_t windDataLevel(float windMs, float gustMs) {
  uint8_t l = 0;
  for (int i = 0; i < 3; i++)
    if (windMs >= WIND_WARN_MEAN[i] || gustMs >= WIND_WARN_GUST[i]) l = i + 1;
  return l;
}

// Skad pochodza biezace pomiary / ewentualny blad - pokazywane w panelu WWW.
// Zwykly bufor (nie String): panel czyta go z innego zadania, a String moglby
// w trakcie zapisu realokowac pamiec.
char weatherStatus[200] = "";
// Zrodlo faktycznie uzytych danych - pokazywane na ekranie informacji (dotkniecie kresek zasiegu).
// Przy bledzie MSN zostaje "Open-Meteo", wiec napis mowi prawde o tym, co widac na ekranie.
char sourceLabel[16] = "OpenMeteo";
bool sourceImgw = false;   // pomiary biezace ze stacji IMGW
bool imgwApplied = false;   // czy ostatnie pobranie IMGW podmienilo biezace pomiary (ustawia applyImgwMeasurements)
// Dane do ekranu informacyjnego (dotkniecie kresek zasiegu): stacja i godzina pomiaru IMGW,
// godzina pomiaru z Open-Meteo, godzina ostatniego pobrania oraz wschod/zachod slonca dzisiaj.
char imgwStationName[32] = "";
char imgwHourStr[8] = "--:--";
char omObsTime[8] = "--:--";
char fetchTimeStr[8] = "--:--";
char sunriseStr[8] = "--:--";
char sunsetStr[8] = "--:--";
static const uint8_t MAX_FORECAST = 4;
ForecastDay forecast[MAX_FORECAST];
HourPoint hourly[MAX_FORECAST][24];   // [dzien prognozy][godzina 0-23]

// DZISIAJ nie jest w kolumnach prognozy (te zaczynaja sie od jutra), a ekran
// szczegolow ma pokazywac tez dzis - stad osobne podsumowanie i godziny.
ForecastDay todayDay;
HourPoint hourlyToday[24];
static const uint8_t DAY_TODAY = 255;   // "numer dnia" oznaczajacy dzisiaj na ekranie szczegolow
uint8_t forecastCount = 0;

static const uint8_t PRESSURE_HISTORY = 6;
float pressureHistory[PRESSURE_HISTORY];
uint8_t pressureHistoryCount = 0;
float localPressure = 0, localTemp = 0, localHumidity = 0;

// Rownolegla historia cisnienia z API pogodowego — uzywana do liczenia
// trendu, gdy nie ma podlaczonego BME280 (API daje tylko biezaca wartosc,
// wiec wlasna historie budujemy sami z kolejnych zapytan co 10 minut).
float apiPressureHistory[PRESSURE_HISTORY];
uint8_t apiPressureHistoryCount = 0;

unsigned long lastWeatherFetch = 0;
// Z ktorej godziny (czas lokalny) bierzemy kod pogody dla ikon prognozy dni
static const int FORECAST_ICON_HOUR = 12;
// Porywy wiatru w biezacej dobie (current.gustMs) - informacyjnie. Ostrzezenie o silnym wietrze pochodzi
// wylacznie z ostrzezen IMGW (zrodlo MSN) - patrz windAlertLevelAt() / ForecastDay::windAlertLevel.
// GUST_COUNT_PAST_HOURS: false = porywy liczymy tylko od biezacej godziny do konca doby
// ("wystapia"), true = cala dobe od 00:00 (alarm trzyma sie do polnocy, nawet gdy
// porywy juz minely).
static const bool GUST_COUNT_PAST_HOURS = false;
// Trend cisnienia: zmiana miedzy biezaca godzina a TREND_WINDOW_HOURS godzin wczesniej
// (z godzinowej serii pressure_msl). Strzalka pojawia sie, gdy zmiana (przeliczona na
// 3 godziny) wynosi co najmniej TREND_THRESHOLD_HPA. 1 hPa/3h to zwyczajowa granica
// "cisnienie stabilne" - zmniejsz, jesli chcesz widziec strzalke czesciej.
static const int TREND_WINDOW_HOURS = 3;
static const float TREND_THRESHOLD_HPA = 1.0f;
unsigned long lastPressureSample = 0;
static const unsigned long PRESSURE_SAMPLE_MS = 10UL * 60UL * 1000UL;
unsigned long lastClockTick = 0;

// Jakosc powietrza (AQI z Open-Meteo, przeliczony na skale 1-5) + wspolrzedne
// miasta potrzebne do zapytan (Open-Meteo wymaga lat/lon, nie nazwy miasta)
float cityLat = 0, cityLon = 0;
bool geoReady = false;
int currentAqi = 0; // 0 = brak danych

// Dodatkowe dane z Open-Meteo Air Quality do ekranu "Przeglad dnia": indeks UV (godzina biezaca i maksimum dzisiaj)
// oraz stezenie pylkow (godzina biezaca). Pylki API podaje tylko w sezonie pylenia - poza nim zwraca null.
struct AirExtras {
  bool uvValid = false;
  float uvNow = 0;          // indeks UV w biezacej godzinie
  float uvMax = 0;          // maksimum dzisiaj
  int8_t uvMaxHour = -1;    // godzina maksimum
  bool pollenValid = false; // true, gdy API podalo cokolwiek (poza sezonem same null)
  float pollen[5] = {0, 0, 0, 0, 0};   // olcha, brzoza, trawy, bylica, ambrozja [ziarna/m3]
  float aqiValue = -1;      // europejski indeks AQI (liczba), -1 = brak
  bool pmValid = false;     // PM2,5 / PM10 [ug/m3] z biezacej godziny
  float pm25 = 0, pm10 = 0;
};
AirExtras airExtras;

// ---------------------------------------------------------------------
//  Reset ekranu przez PCF8574 (P6)
// ---------------------------------------------------------------------
void lcdResetByExpander() {
  if (!ioExpander.begin(PCF8574_ADDR, &Wire)) {
    Serial.println("Blad inicjalizacji PCF8574");
    return;
  }
  ioExpander.digitalWriteByte(0xFF);
  delay(20);
  ioExpander.digitalWriteByte(0xFF & ~(1 << PCF8574_PIN_LCD_RST));
  delay(120);
  ioExpander.digitalWriteByte(0xFF);   // P2 (CHG_N) zostaje w stanie wysokim - mozna z niego czytac stan ladowania
  delay(120);
}

// ---------------------------------------------------------------------
//  Bateria Li-ion (18650) - napiecie z dzielnika na GPIO4, ladowanie z CHG_N (PCF8574 P2).
//  Algorytm jak w oficjalnym przykladzie 08_battery_test: srednia z 16 probek ADC,
//  filtr dolnoprzepustowy napiecia, liniowo 3,3 V -> 0%, 4,2 V -> 100%, a pokazywany procent
//  zmienia sie najwyzej o 1% na 30 s (z histereza 2%) i stoi w miejscu przy zmianie stanu
//  ladowania - podlaczenie zasilacza od razu podnosi napiecie, ale nie pojemnosc.
// ---------------------------------------------------------------------
static const uint16_t BATTERY_EMPTY_MV = 3300;
static const uint16_t BATTERY_FULL_MV = 4150;       // napiecie RZECZYWISTE (po kalibracji ADC) pelnego ogniwa w spoczynku
// Kalibracja przetwornika: miernik 4,19 V przy odczycie stacji 4,26 V -> odczyt mnozymy przez 4190/4260.
// Wszystkie progi ponizej sa w napieciu rzeczywistym (po kalibracji).
static const uint32_t BATTERY_CAL_NUM = 4190;
static const uint32_t BATTERY_CAL_DEN = 4260;
static const uint16_t BATTERY_ABSENT_MV = 2500;       // ponizej - uznajemy, ze ogniwa nie ma
// Brak ogniwa (zasilanie z USB): ladowarka trzyma na zlaczu swoje napiecie koncowe - ok. 4,21-4,22 V
// (4,28-4,29 V przed kalibracja) przy ladowanie=nie. Pelne ogniwo po zakonczeniu ladowania ok. 4,19 V, wiec
// prog lezy miedzy nimi. Liczone z napiecia BIEZACEGO (bez filtra); wlaczenie "braku ogniwa" dopiero po
// BATTERY_EXT_VOTES zgodnych pomiarach, wylaczenie od razu (odlaczenie USB = spadek napiecia). Ikona: sam piorun.
// Szeroka histereza: pelne ogniwo na zasilaczu "plywa" ok. 4,26-4,28 V - waski prog powodowal przelaczanie
// zasilacz <-> bateria co kilka sekund (miganie ikony i jasnosci). Wlaczenie od 4,25 V, wylaczenie dopiero
// ponizej 4,20 V (po odlaczeniu zasilacza pelne ogniwo pod obciazeniem schodzi tam w kilka minut);
// w obie strony wymagane BATTERY_EXT_VOTES zgodnych pomiarow.
static const uint16_t BATTERY_EXT_ON_MV = 4180;     // = 4,25 V przed kalibracja
static const uint16_t BATTERY_EXT_OFF_MV = 4130;    // = 4,20 V przed kalibracja
static const uint8_t  BATTERY_EXT_VOTES = 3;          // 3 pomiary = ok. 6 s
// Szybkie wykrycie odlaczenia zasilacza przy pelnym ogniwie: na zasilaczu ladowarka trzyma napiecie stale
// (srednia z ostatnich pomiarow = "poziom zasilacza"); po odlaczeniu ogniwo przejmuje obciazenie i napiecie
// od razu spada o kilkadziesiat mV. Spadek o BATTERY_UNPLUG_STEP_MV ponizej tego poziomu (2 pomiary z rzedu)
// = zasilacz odlaczony - bez czekania, az napiecie zejdzie ponizej 4,20 V.
static const uint16_t BATTERY_UNPLUG_STEP_MV = 30;
// Pelnego ogniwa przy USB (ladowarka skonczyla ladowac) nie da sie odroznic od braku ogniwa po napieciu -
// oba daja ok. 4,26-4,29 V. Rozstrzyga historia: jesli stacja kiedykolwiek widziala ladowanie albo napiecie
// ponizej BATTERY_CELL_SEEN_MV bez ladowania (to mozliwe tylko z ogniwem), zapamietuje "ogniwo jest" (NVS)
// i przy wysokim napieciu pokazuje pelna baterie z piorunem zamiast samego pioruna.
static const uint16_t BATTERY_CELL_SEEN_MV = 4160;  // = 4,23 V przed kalibracja
// Po odlaczeniu ladowarki napiecie ogniwa przez kilkanascie sekund opada z napiecia ladowania - po tym czasie
// filtr i procent sa ustawiane od razu z biezacego pomiaru (zamiast pelzac po 1% na 30 s).
static const uint32_t BATTERY_SETTLE_MS = 15000;
static const uint8_t  BATTERY_LOW_PERCENT = 15;       // od tego poziomu w dol ikona jest czerwona
static const uint8_t  BATTERY_ADC_SAMPLES = 16;
static const uint8_t  BATTERY_PERCENT_HYST = 2;
static const uint32_t BATTERY_PERCENT_STEP_MS = 30000;
static const uint32_t BATTERY_READ_MS = 2000;      // pomiar co 2 s - szybka reakcja na (od)laczenie zasilania

struct BatteryState {
  bool present = false;
  bool charging = false;
  bool external = false;    // brak ogniwa - zasilanie z USB (ikona ukryta, w informacjach "Zasilanie: USB")
  bool usb = false;         // zasilacz wykryty przez przerobke USB_SENSE (P7) - tylko gdy wlaczone w panelu (settings.usbSense)
  bool usbFull = false;     // ogniwo pelne, zasilacz podlaczony, ladowarka juz nie laduje (pelna bateria + piorun)
  bool cellSeen = false;    // stacja widziala kiedys ogniwo (ladowanie / rozladowanie) - zapamietane w NVS
  uint32_t mv = 0;          // napiecie po filtrze [mV]
  uint8_t percent = 0;      // procent pokazywany na ekranie (ustabilizowany)
  bool ready = false;       // byl juz co najmniej jeden pomiar
  // stan wewnetrzny stabilizacji
  bool lastCharging = false;
  uint32_t lastStepMs = 0;
  uint32_t settleAt = 0;    // != 0: o tej chwili (millis) ustawic filtr i procent z biezacego pomiaru
};
BatteryState battery;
unsigned long lastBatteryRead = 0;

uint32_t readBatteryMv() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; i++) {
    sum += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }
  uint64_t scaled = (uint64_t)(sum / BATTERY_ADC_SAMPLES) * (BATTERY_RESISTOR_UPPER + BATTERY_RESISTOR_LOWER);
  uint32_t mv = (uint32_t)(scaled / BATTERY_RESISTOR_LOWER);
  return mv * BATTERY_CAL_NUM / BATTERY_CAL_DEN;      // kalibracja wg miernika
}

// Podczas ladowania napiecie na zaciskach jest zawyzone (prad ladowania x opor wewnetrzny) - procent liczymy
// z napiecia pomniejszonego o te wartosc, inaczej rozladowane ogniwo zaraz po podlaczeniu pokazywaloby "pelne".
static const uint16_t BATTERY_CHG_COMP_MV = 150;
uint8_t batteryPercentFromMv(uint32_t mv) {
  if (mv <= BATTERY_EMPTY_MV) return 0;
  if (mv >= BATTERY_FULL_MV) return 100;
  return (uint8_t)((mv - BATTERY_EMPTY_MV) * 100 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

// Odczyt i stabilizacja. Zwraca true, gdy zmienilo sie cos, co widac na ikonie
// (obecnosc ogniwa, ladowanie albo pokazywany procent) - wtedy warto ja przerysowac.
bool updateBattery() {
  uint32_t now = millis();
  uint32_t rawMv = readBatteryMv();
  bool chg = ioExpander.digitalRead(PCF8574_PIN_CHG_N);
  if (!BATTERY_CHG_ACTIVE_HIGH) chg = !chg;
  // przerobka: bezposredni odczyt obecnosci zasilacza USB (P7 ekspandera, dzielnik z VBUS)
  bool usb = settingsStore.settings.usbSense ? ioExpander.digitalRead(PCF8574_PIN_USB_SENSE) : false;
  battery.usb = usb;

  bool oldPresent = battery.present, oldCharging = battery.charging, oldExternal = battery.external;
  bool oldUsbFull = battery.usbFull;
  static bool cellSeenLoaded = false;
  if (!cellSeenLoaded) {                         // "ogniwo bylo widziane" - przetrwa restart
    Preferences p;
    p.begin("batt", true);
    battery.cellSeen = p.getBool("seen", false);
    p.end();
    cellSeenLoaded = true;
  }
  uint8_t oldPercent = battery.percent;
  bool first = !battery.ready;

  // histereza obecnosci, zeby szum przy samej granicy nie migal ikona
  bool present = battery.present ? (rawMv >= BATTERY_ABSENT_MV - 200) : (rawMv >= BATTERY_ABSENT_MV);
  if (!present) {
    battery.present = false;
    battery.charging = false;
    battery.external = false;
    battery.usbFull = false;
    battery.mv = rawMv;
    battery.ready = true;
    return first || oldPresent;
  }

  // ogniwo widziane: ladowanie albo napiecie typowe dla ogniwa bez ladowarki
  // (z przerobka USB_SENSE: praca bez zasilacza = na pewno z ogniwa)
  if (!battery.cellSeen && (chg || rawMv < BATTERY_CELL_SEEN_MV || (settingsStore.settings.usbSense && !usb))) {
    battery.cellSeen = true;
    Preferences p;
    p.begin("batt", false);
    p.putBool("seen", true);
    p.end();
    Serial.println("Bateria: wykryto ogniwo (zapamietane)");
  }
  // napiecie ladowarki bez ladowania (brak ogniwa albo ogniwo pelne) - z biezacego napiecia,
  // wlaczenie po kilku zgodnych pomiarach, wylaczenie od razu
  static uint8_t extVotes = 0;
  static uint32_t usbBaseMv = 0;                 // srednie napiecie w stanie "zasilacz" (0 = brak)
  bool wasHigh = battery.external || battery.usbFull;
  bool ext;
  if (settingsStore.settings.usbSense) { ext = usb && !chg; extVotes = 0; }   // przerobka: zasilacz wiadomo od razu
  else if (chg) { ext = false; extVotes = 0; }
  else if (oldCharging && rawMv >= BATTERY_EXT_OFF_MV) {
    // ladowarka wlasnie skonczyla ladowac przy wysokim napieciu = ogniwo pelne, zasilacz nadal podlaczony -
    // od razu "pelna na zasilaczu" (bez chwilowej bialej ikony i bez czekania na kolejne pomiary)
    ext = true; extVotes = 0; usbBaseMv = rawMv;
  }
  else if (wasHigh) {
    // poziom napiecia na zasilaczu (wolna srednia) i wykrycie nagłego spadku = odlaczenie zasilacza
    static uint8_t stepVotes = 0;
    bool step = usbBaseMv && rawMv + BATTERY_UNPLUG_STEP_MV < usbBaseMv;
    if (step) stepVotes++; else stepVotes = 0;
    if (rawMv < BATTERY_EXT_OFF_MV) extVotes++; else extVotes = 0;   // zapas: wyraznie nisko (szum ADC - kilka pomiarow)
    ext = (extVotes < BATTERY_EXT_VOTES) && (stepVotes < 2);
    if (!ext) { extVotes = 0; stepVotes = 0; usbBaseMv = 0; }
    else if (!step) usbBaseMv = usbBaseMv ? (usbBaseMv * 15 + rawMv) / 16 : rawMv;
  }
  else {
    if (rawMv >= BATTERY_EXT_ON_MV) extVotes++; else extVotes = 0;
    ext = (extVotes >= BATTERY_EXT_VOTES);
    if (ext) extVotes = 0;
  }

  if (!battery.present || first) {                 // pierwszy pomiar albo ogniwo wlasnie podlaczone
    battery.mv = rawMv;
    battery.percent = batteryPercentFromMv(chg ? rawMv - BATTERY_CHG_COMP_MV : rawMv);
    battery.lastCharging = chg;
    battery.lastStepMs = now;
  } else {
    battery.mv = (battery.mv * 7 + rawMv) / 8;
    uint8_t raw = batteryPercentFromMv(chg ? battery.mv - BATTERY_CHG_COMP_MV : battery.mv);
    // odlaczenie zasilania (koniec ladowania albo koniec "zasilania z USB"): po chwili ustalenia sie napiecia
    // procent ustawiamy od razu z biezacego pomiaru
    if ((battery.lastCharging && !chg) || (wasHigh && !ext)) battery.settleAt = now + BATTERY_SETTLE_MS;
    if (chg || ext) battery.settleAt = 0;          // zasilanie wrocilo - nie ma na co czekac
    if (battery.settleAt && (int32_t)(now - battery.settleAt) >= 0) {
      battery.settleAt = 0;
      battery.mv = rawMv;
      battery.percent = batteryPercentFromMv(chg ? rawMv - BATTERY_CHG_COMP_MV : rawMv);
      battery.lastCharging = chg;
      battery.lastStepMs = now;
    } else if (chg != battery.lastCharging) {      // zmiana stanu ladowania - procent stoi
      battery.lastCharging = chg;
      battery.lastStepMs = now;
    } else if (battery.settleAt) {
      // trwa ustalanie napiecia po odlaczeniu - procent stoi
    } else if (now - battery.lastStepMs >= BATTERY_PERCENT_STEP_MS) {
      if (raw >= battery.percent + BATTERY_PERCENT_HYST) {
        battery.percent++;
        battery.lastStepMs = now;
      } else if (raw + BATTERY_PERCENT_HYST <= battery.percent) {
        battery.percent--;
        battery.lastStepMs = now;
      }
    }
  }
  battery.present = true;
  battery.charging = chg;
  battery.ready = true;
  battery.external = ext && !battery.cellSeen;     // wysokie napiecie i ogniwa nigdy nie bylo -> brak ogniwa
  battery.usbFull  = ext && battery.cellSeen;      // wysokie napiecie, ogniwo jest -> pelne, na zasilaczu
  if (battery.usbFull) battery.percent = 100;
  bool changed = first || !oldPresent || oldCharging != battery.charging || oldPercent != battery.percent
                 || oldExternal != battery.external || oldUsbFull != battery.usbFull;
  if (changed) Serial.printf("Bateria: raw=%lu mV, filtr=%lu mV, %u%%, ladowanie=%s, brak ogniwa=%s, pelna na USB=%s\n",
                             (unsigned long)rawMv, (unsigned long)battery.mv, battery.percent, chg ? "tak" : "nie",
                             battery.external ? "tak" : "nie", battery.usbFull ? "tak" : "nie");
  return changed;
}

// ---------------------------------------------------------------------
//  Tryb oszczedzania energii na baterii
//  Na baterii (ogniwo obecne, nie laduje i nie ma "zasilania zewnetrznego"):
//   - podswietlenie ograniczone do settings.brightnessBattery (applyBacklightSchedule),
//   - procesor 80 MHz zamiast 240 (SPI ekranu i PWM chodza z APB 80 MHz - bez zmian),
//   - Wi-Fi wylaczone, wlaczane tylko na pobranie pogody co settings.batteryFetchMin minut.
//  Po podlaczeniu zasilacza wszystko wraca (Wi-Fi na stale, panel WWW i OTA dostepne).
//  Rozladowane ogniwo: stacja dziala do konca - zasilanie odcina uklad ochronny (PCM) ogniwa. Po podlaczeniu
//  USB plytka startuje z zasilacza, a ladowarka laduje ogniwo (i "budzi" PCM).
// ---------------------------------------------------------------------

enum ScreenMode : uint8_t { SCREEN_MAIN, SCREEN_DETAIL, SCREEN_INFO, SCREEN_OUTLOOK, SCREEN_AIR };
ScreenMode screenMode = SCREEN_MAIN;
bool onBattery = false;          // aktualny tryb pracy (oszczedzanie wlaczone)
uint8_t powerModeVotes = 0;      // ile kolejnych pomiarow wskazuje zmiane trybu (odpornosc na pojedyncze odczyty)

bool batteryPowered() {
  return battery.ready && battery.present && !battery.charging && !battery.external && !battery.usbFull;
}

// Ostatni pomiar zasiegu Wi-Fi - na baterii (Wi-Fi wylaczone miedzy pobraniami) kreski pokazuja
// wartosc z ostatniego polaczenia.
int lastRssi = 0;                 // 0 = brak pomiaru
char lastRssiTime[8] = "--:--";   // godzina tego pomiaru

void rememberRssi() {
  if (WiFi.status() != WL_CONNECTED) return;
  int r = WiFi.RSSI();
  if (r == 0) return;
  lastRssi = r;
  struct tm t;
  if (getLocalTime(&t, 10)) strftime(lastRssiTime, sizeof(lastRssiTime), "%H:%M", &t);
}

void wifiPowerOff() {
  if (apMode) return;
  rememberRssi();                 // zapamietujemy zasieg tuz przed wylaczeniem
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

bool connectWiFi();   // definicja nizej
bool connectWiFi(bool tryHidden);
bool wifiPowerOn() {
  if (WiFi.status() == WL_CONNECTED) return true;
  return connectWiFi();
}

// Laczenie BEZ blokowania petli (dotyk dziala dalej): WiFi.begin() i powrot; wynik sprawdza loop().
// wifiNetIdx = siec (0 glowna, 1-2 zapasowe), z ktora ostatnio sie udalo / ktora wlasnie probujemy.
int8_t wifiNetIdx = 0;
unsigned long wifiBeginAt = 0;

bool wifiNet(int i, const char **ssid, const char **pass) {
  AppSettings &s = settingsStore.settings;
  const String *ss[3] = {&s.wifiSsid, &s.wifiSsid2, &s.wifiSsid3};
  const String *pp[3] = {&s.wifiPassword, &s.wifiPassword2, &s.wifiPassword3};
  if (i < 0 || i > 2 || ss[i]->isEmpty()) return false;
  *ssid = ss[i]->c_str(); *pass = pp[i]->c_str();
  return true;
}
int wifiNextNet(int from) {                       // nastepna skonfigurowana siec (po kolei, w kolko)
  const char *a, *b;
  for (int k = 1; k <= 3; k++) { int i = (from + k) % 3; if (wifiNet(i, &a, &b)) return i; }
  return from;
}
void wifiBeginNet(int i) {
  const char *ssid, *pass;
  if (!wifiNet(i, &ssid, &pass)) { i = wifiNextNet(i); if (!wifiNet(i, &ssid, &pass)) return; }
  WiFi.setHostname(settingsStore.settings.hostname.c_str());   // nazwa w routerze (DHCP) - przed WIFI_STA
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
  wifiNetIdx = i;
  wifiBeginAt = millis();
  Serial.printf("WiFi: laczenie (w tle) z %s\n", ssid);
}

void applyBacklightSchedule();
void setBacklightPercent(uint8_t pct);
void drawWifiBars(const ThemeColors &th);

// Po przejsciu na baterie Wi-Fi wylaczamy z opoznieniem - chwilowe "mrugniecie" zasilania nie zrywa polaczenia
// (kreski zasiegu nie znikaja, nie trzeba sie laczyc od nowa).
static const unsigned long WIFI_OFF_DELAY_MS = 60000;   // 1 min bez zasilania = na pewno praca na baterii
unsigned long wifiOffAt = 0;      // != 0: o tej chwili (millis) wylaczyc Wi-Fi (praca na baterii)

void applyPowerMode(bool bat) {
  if (bat == onBattery) return;
  if (bat && Update.isRunning()) return;     // trwa OTA - nie wylaczamy Wi-Fi w trakcie
  onBattery = bat;
  Serial.printf("Zasilanie: %s\n", bat ? "BATERIA - oszczedzanie" : "zewnetrzne");
  if (bat) {
    wifiOffAt = millis() + WIFI_OFF_DELAY_MS;    // Wi-Fi gasnie za chwile (loop), nie od razu
    setCpuFrequencyMhz(80);
  } else {
    wifiOffAt = 0;
    setCpuFrequencyMhz(240);
    if (WiFi.status() != WL_CONNECTED) wifiBeginNet(wifiNetIdx);   // w tle; polaczonego nie ruszamy
  }
  applyBacklightSchedule();
  if (screenMode == SCREEN_MAIN) drawWifiBars(currentTheme());
}


// ---------------------------------------------------------------------
//  Podswietlenie — PWM
// ---------------------------------------------------------------------
void initBacklight() {
  ledcAttach(LCD_BL_PIN, 5000, 8);
  ledcWrite(LCD_BL_PIN, 255);
}

void setBacklightPercent(uint8_t pct) {
  pct = constrain(pct, 0, 100);
  uint8_t duty = map(pct, 0, 100, 0, 255);
  ledcWrite(LCD_BL_PIN, duty);
}

// ---------------------------------------------------------------------
//  Czujnik BME280
// ---------------------------------------------------------------------
void initSensor() {
  bmeReady = bme.begin(BME280_ADDR_PRIMARY, &Wire);
  if (!bmeReady) bmeReady = bme.begin(BME280_ADDR_SECONDARY, &Wire);
  if (!bmeReady) Serial.println("BME280 nie wykryty - lokalne dane beda niedostepne");
}

void sampleSensor() {
  if (!bmeReady) return;
  localTemp = bme.readTemperature();
  localHumidity = bme.readHumidity();
  localPressure = bme.readPressure() / 100.0F;

  if (pressureHistoryCount < PRESSURE_HISTORY) {
    pressureHistory[pressureHistoryCount++] = localPressure;
  } else {
    for (uint8_t i = 1; i < PRESSURE_HISTORY; i++) pressureHistory[i - 1] = pressureHistory[i];
    pressureHistory[PRESSURE_HISTORY - 1] = localPressure;
  }
}

int8_t pressureTrend() {
  if (pressureHistoryCount < 2) return 0;
  float diff = pressureHistory[pressureHistoryCount - 1] - pressureHistory[0];
  if (diff > 0.8) return 1;
  if (diff < -0.8) return -1;
  return 0;
}

// Dodaje odczyt cisnienia z API do wlasnej historii (wywolywane
// po kazdym udanym fetchWeather()).
void recordApiPressure(float hpa) {
  if (hpa <= 0) return;
  if (apiPressureHistoryCount < PRESSURE_HISTORY) {
    apiPressureHistory[apiPressureHistoryCount++] = hpa;
  } else {
    for (uint8_t i = 1; i < PRESSURE_HISTORY; i++) apiPressureHistory[i - 1] = apiPressureHistory[i];
    apiPressureHistory[PRESSURE_HISTORY - 1] = hpa;
  }
}

int8_t apiPressureTrend() {
  if (apiPressureHistoryCount < 2) return 0;
  float diff = apiPressureHistory[apiPressureHistoryCount - 1] - apiPressureHistory[0];
  if (diff > 0.8) return 1;
  if (diff < -0.8) return -1;
  return 0;
}

// ---------------------------------------------------------------------
//  Pomocnicze funkcje do rysowania tekstu wlasciwymi czcionkami.
//  Z czcionkami GFXfont wspolrzedna Y w setCursor() to BASELINE, nie
//  gorny-lewy róg jak w domyslnej czcionce — dlatego liczymy przesuniecie
//  na podstawie getTextBounds(), zeby podawac zawsze "gorny piksel tekstu".
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
//  Ekrany informacyjne (AP / laczenie / polaczono)
// ---------------------------------------------------------------------
// Podpis w prawym dolnym rogu ekranow startowych: tekst POCHYLONY (czcionki sa tylko proste, wiec
// kazdy wiersz glifu przesuwamy w prawo proporcjonalnie do wysokosci nad linia bazowa), maly, ciemny.
// Tylko ASCII (kody czcionki = ASCII).
void drawSignature(const ThemeColors &th) {
  const char *txt = "Lukalago 2026";   // przed napisem znak (c) rysowany recznie (czcionka nie ma glifu)
  const GFXfont *f = FONT_SMALL;
  uint16_t col = lerpColor565(th.bg, th.muted, 0.5f);
  const float kSlant = 0.25f;                    // ok. 14 stopni
  int total = 0;
  for (const char *p = txt; *p; p++) total += f->glyph[(uint8_t)*p - f->first].xAdvance;
  const int kSymW = 13, kSymGap = 4;             // szerokosc znaku (c) i odstep do napisu
  int cursorX = LCD_H_RES - 14 - total;
  int baseY = LCD_V_RES - 14;
  {
    // znak (c): pierscien r~5,3 i wewnatrz "c" (pierscien r~2,5 z przerwa po prawej), pochylony jak tekst
    int cx0 = cursorX - kSymGap - kSymW / 2, cy0 = baseY - 5;
    for (int dy = -7; dy <= 7; dy++)
      for (int dx = -7; dx <= 7; dx++) {
        float d = sqrtf((float)(dx * dx + dy * dy));
        bool on = fabsf(d - 5.3f) < 0.6f || (fabsf(d - 2.5f) < 0.6f && !(dx > 0 && abs(dy) <= 1));
        if (!on) continue;
        int y = cy0 + dy;
        gfx->drawPixel(cx0 + dx + (int)lroundf((baseY - y) * kSlant), y, col);
      }
  }
  for (const char *p = txt; *p; p++) {
    uint8_t c = (uint8_t)*p;
    const GFXglyph &g = f->glyph[c - f->first];
    int w = g.width, h = g.height;
    const uint8_t *bm = f->bitmap + g.bitmapOffset;   // czcionka wygladzona: 4 bity krycia na piksel
    uint32_t i = 0;
    for (int yy = 0; yy < h; yy++) {
      int y = baseY + g.yOffset + yy;
      int shift = (int)lroundf((baseY - y) * kSlant);
      for (int xx = 0; xx < w; xx++, i++) {
        uint8_t a = glyphAlpha(bm, i);
        if (a) gfx->drawPixel(cursorX + g.xOffset + xx + shift, y, blend565(th.bg, col, a));
      }
    }
    cursorX += g.xAdvance;
  }
}

// ---- Ekran startowy "Łączenie z Wi-Fi": tytul, duza ikona Wi-Fi (luki zapalaja sie po kolei), nazwa sieci.
static const int BOOT_WIFI_CX = LCD_H_RES / 2;   // srodek kropki ikony Wi-Fi
static const int BOOT_WIFI_CY = 258;
static const int BOOT_TEXT_Y = 300;              // "Łączenie z Wi-Fi" / "Połączono"
static const int BOOT_SSID_Y = 336;              // nazwa sieci
bool bootConnectUi = false;                      // animacja tylko przy starcie (nie przy laczeniu w tle na baterii)

// step: 0 = sama kropka, 1..3 = tyle zapalonych lukow od srodka; full = wszystkie w kolorze col
// Ikona rysowana WYGLADZONA: dla kazdego piksela liczymy, jaka czesc lezy w kropce / luku (odleglosc od
// krawedzi okregow i od skosnych bokow luku, +-45 stopni od pionu), i mieszamy kolor z tlem. Rysujemy
// wiersz po wierszu z bufora (draw16bitRGBBitmap) - ok. 120x70 px, kilka ms, wiec animacja co 400 ms
// nie obciaza petli. fillArc()/fillCircle() z biblioteki daja poszarpane krawedzie.
static inline float covEdge(float sd) {          // sd = odleglosc ze znakiem od krawedzi (>0 = w srodku)
  return sd >= 0.5f ? 1.0f : (sd <= -0.5f ? 0.0f : sd + 0.5f);
}

void drawBootWifiIcon(uint8_t step, uint16_t col) {
  ThemeColors th = currentTheme();
  uint16_t dim = lerpColor565(th.bg, col, 0.18f);
  const int cx = BOOT_WIFI_CX, cy = BOOT_WIFI_CY;
  // luki 225..315 stopni (gora), grubosc 9 px, odstep 8 px - jak na typowej ikonie Wi-Fi; kropka r = 7
  const float rOut[3] = {26.0f, 43.0f, 60.0f};
  const float kThick = 9.0f, kDot = 7.0f;
  const int x0 = cx - 62, x1 = cx + 62, y0 = cy - 62, y1 = cy + (int)kDot + 2;
  const int w = x1 - x0 + 1;
  static uint16_t row[128];
  for (int y = y0; y <= y1; y++) {
    float dy = (float)(y - cy);
    for (int x = x0; x <= x1; x++) {
      float dx = (float)(x - cx);
      float d = sqrtf(dx * dx + dy * dy);
      uint16_t c = th.bg;
      float cov = covEdge(kDot - d);                       // kropka
      if (cov > 0.0f) {
        c = lerpColor565(th.bg, col, cov);
      } else {
        // klin w gore: -dy >= |dx|; odleglosc od skosnego boku = (-dy - |dx|) / sqrt(2)
        float wedge = covEdge((-dy - fabsf(dx)) * 0.70710678f);
        if (wedge > 0.0f) {
          for (int i = 0; i < 3; i++) {
            float ring = covEdge(rOut[i] - d) * covEdge(d - (rOut[i] - kThick));
            if (ring > 0.0f) {
              c = lerpColor565(th.bg, i < step ? col : dim, ring * wedge);
              break;
            }
          }
        }
      }
      row[x - x0] = c;
    }
    gfx->draw16bitRGBBitmap(x0, y, row, w, 1);
  }
}

void drawBootSsid(const char *ssid) {
  ThemeColors th = currentTheme();
  gfx->fillRect(0, BOOT_SSID_Y - 4, LCD_H_RES, 26, th.bg);
  printCentered(ssid, BOOT_SSID_Y, FONT_SMALL, th.muted, th.bg);
}

void drawBootHeader(const ThemeColors &th) {
  gfx->fillScreen(th.bg);
  // tytul pogrubiony 12 pt, rozstrzelony o 3 px (18 pt bylo za duze)
  printCenteredSpaced(L("STACJA POGODY", "WEATHER STATION"), 104, FONT_DAY_TITLE, th.accent, th.bg, 3, 3);
  char ver[16];
  snprintf(ver, sizeof(ver), "v%s", FW_VERSION);
  printCentered(ver, 134, FONT_SMALL, lerpColor565(th.bg, th.muted, 0.7f), th.bg);
}

void drawConnectingScreen() {
  ThemeColors th = currentTheme();
  drawBootHeader(th);
  drawBootWifiIcon(0, th.accent);
  printCentered(L("Łączenie z Wi-Fi", "Connecting to Wi-Fi"), BOOT_TEXT_Y, FONT_DAY_TITLE, th.accent, th.bg);
  drawBootSsid(settingsStore.settings.wifiSsid.c_str());
  drawSignature(th);
}

// wywolywane w petli oczekiwania na polaczenie: co 400 ms kolejny luk (0,1,2,3,0,...)
void bootWifiAnimTick() {
  if (!bootConnectUi) return;
  static unsigned long last = 0;
  static uint8_t step = 0;
  if (millis() - last < 400) return;
  last = millis();
  step = (step + 1) % 4;
  drawBootWifiIcon(step, currentTheme().accent);
}

// Kod QR w bialym kwadracie box x box px (lewy gorny rog x, y): moduly mozliwie duze (calkowita
// wielokrotnosc), co najmniej 2 moduly bialego marginesu dookola - telefony czytaja to bez problemu.
// false = tekst za dlugi (powyzej wersji 6) - wtedy nic nie rysujemy.
bool drawQrBox(const char *text, int x, int y, int box) {
  static uint8_t tmp[stqr_BUFFER_LEN_FOR_VERSION(6)], qr[stqr_BUFFER_LEN_FOR_VERSION(6)];
  if (!stqr_encodeText(text, tmp, qr, stqr_Ecc_MEDIUM, stqr_VERSION_MIN, 6, stqr_Mask_AUTO, true)) return false;
  int n = stqr_getSize(qr);
  int scale = box / (n + 4);
  if (scale < 1) return false;
  int off = (box - n * scale) / 2;
  gfx->fillRoundRect(x, y, box, box, 6, 0xFFFF);
  for (int my = 0; my < n; my++) {
    int mx = 0;
    while (mx < n) {                       // serie ciemnych modulow w wierszu jednym prostokatem
      if (!stqr_getModule(qr, mx, my)) { mx++; continue; }
      int start = mx;
      while (mx < n && stqr_getModule(qr, mx, my)) mx++;
      gfx->fillRect(x + off + start * scale, y + off + my * scale, (mx - start) * scale, scale, 0x0000);
    }
  }
  return true;
}

// Ekran trybu konfiguracji (AP): kod QR do polaczenia z siecia stacji (siec otwarta, bez hasla),
// pod nim nazwa sieci do wpisania recznie i adres panelu.
void drawApInfoScreen() {
  ThemeColors th = currentTheme();
  gfx->fillScreen(th.bg);
  printCentered(L("TRYB KONFIGURACJI", "SETUP MODE"), 34, FONT_MED, th.accent, th.bg);
  gfx->drawFastHLine(20, 70, LCD_H_RES - 40, th.divider);

  const int box = 140;
  drawQrBox("WIFI:T:nopass;S:StacjaPogody-Setup;;", (LCD_H_RES - box) / 2, 86, box);

  printCentered(L("1. Zeskanuj kod albo połącz z Wi-Fi:", "1. Scan the code or join Wi-Fi:"), 240, FONT_SMALL, th.text, th.bg);
  printCentered("StacjaPogody-Setup", 264, FONT_MED, th.accent, th.bg);

  char url[40];
  snprintf(url, sizeof(url), "http://%s", WiFi.softAPIP().toString().c_str());
  printCentered(L("2. Otwórz w przeglądarce:", "2. Open in your browser:"), 306, FONT_SMALL, th.text, th.bg);
  printCentered(url, 330, FONT_MED, th.accent, th.bg);

  printCentered(L("3. Wybierz WiFi i zapisz ustawienia", "3. Choose Wi-Fi and save settings"), 372, FONT_SMALL, th.text, th.bg);
  printCentered(L("Urządzenie zrestartuje się", "The station will restart"), 412, FONT_SMALL, th.muted, th.bg);
  printCentered(L("automatycznie po zapisie", "automatically after saving"), 434, FONT_SMALL, th.muted, th.bg);
}

void drawConnectedScreen() {
  ThemeColors th = currentTheme();
  const uint16_t green = 0x564C;                 // zielony (RGB 80,200,100)
  drawBootHeader(th);
  drawBootWifiIcon(3, green);
  printCentered(L("Połączono", "Connected"), BOOT_TEXT_Y, FONT_DAY_TITLE, green, th.bg);
  drawBootSsid(WiFi.SSID().c_str());            // faktycznie polaczona (glowna albo zapasowa)
  char ip[64];
  snprintf(ip, sizeof(ip), "Panel: http://%s.local", settingsStore.settings.hostname.c_str());
  printCentered(ip, BOOT_SSID_Y + 28, FONT_SMALL, th.text, th.bg);
  snprintf(ip, sizeof(ip), "%s http://%s", L("albo", "or"), WiFi.localIP().toString().c_str());
  printCentered(ip, BOOT_SSID_Y + 50, FONT_SMALL, th.muted, th.bg);
  drawSignature(th);
}

// ---------------------------------------------------------------------
//  WiFi
// ---------------------------------------------------------------------
// Polaczenie z jedna z trzech sieci (glowna + 2 zapasowe z panelu WWW). Najpierw skan: probujemy
// widoczne sieci w kolejnosci glowna -> zapasowa 1 -> zapasowa 2. Gdy zadna z nich nie jest widoczna
// (albo jest ukryta - ukryte SSID nie pojawiaja sie w skanie) i tryHidden = true, probujemy pozostale "na slepo".
bool connectWiFi(bool tryHidden) {
  AppSettings &s = settingsStore.settings;
  const String *ssids[3] = {&s.wifiSsid, &s.wifiSsid2, &s.wifiSsid3};
  const String *passes[3] = {&s.wifiPassword, &s.wifiPassword2, &s.wifiPassword3};
  if (s.wifiSsid.isEmpty() && s.wifiSsid2.isEmpty() && s.wifiSsid3.isEmpty()) return false;

  WiFi.setHostname(settingsStore.settings.hostname.c_str());   // nazwa w routerze (DHCP) - przed WIFI_STA
  WiFi.mode(WIFI_STA);
  int found = WiFi.scanNetworks();               // ok. 2-3 s
  bool visible[3] = {false, false, false};
  for (int i = 0; i < 3; i++) {
    if (ssids[i]->isEmpty()) continue;
    for (int k = 0; k < found; k++)
      if (WiFi.SSID(k) == *ssids[i]) { visible[i] = true; break; }
  }
  WiFi.scanDelete();

  for (int pass = 0; pass < 2; pass++) {         // 0 = sieci widoczne w skanie, 1 = pozostale (ukryte)
    if (pass == 1 && !tryHidden) break;
    for (int i = 0; i < 3; i++) {
      if (ssids[i]->isEmpty() || visible[i] != (pass == 0)) continue;
      Serial.printf("WiFi: proba %s (%s)\n", ssids[i]->c_str(), i == 0 ? "glowna" : "zapasowa");
      if (bootConnectUi) drawBootSsid(ssids[i]->c_str());   // na ekranie startowym: aktualnie probowana siec
      WiFi.disconnect();
      WiFi.begin(ssids[i]->c_str(), passes[i]->c_str());
      unsigned long start = millis();
      unsigned long limit = (pass == 0) ? 15000 : 10000;
      while (WiFi.status() != WL_CONNECTED && millis() - start < limit) {
        bootWifiAnimTick();
        delay(50);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi: polaczono z %s\n", ssids[i]->c_str());
        wifiNetIdx = i;
        return true;
      }
    }
  }
  return false;
}
bool connectWiFi() { return connectWiFi(true); }

void startApMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("StacjaPogody-Setup");
  Serial.print("Tryb konfiguracji: http://");
  Serial.println(WiFi.softAPIP());
  drawApInfoScreen();
}

// ---------------------------------------------------------------------
//  Pobieranie pogody — Open-Meteo (https://open-meteo.com)
//  Darmowe, bez klucza API i bez rejestracji. Wszystkie zapytania ida po
//  HTTPS; setInsecure() pomija weryfikacje certyfikatu — akceptowalne,
//  bo nie wysylamy zadnych sekretow (zadnego klucza), tylko publiczne dane.
// ---------------------------------------------------------------------

// Kodowanie procentowe (URL-encode) — potrzebne dla nazw miast ze spacjami
// i polskimi znakami (kodujemy bajty UTF-8), np. "Nowy Sacz" czy "Lodz".
String encodeUrlComponent(const String &s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// Wspolny GET po HTTPS + parsowanie JSON. true tylko gdy HTTP 200 i poprawny JSON.
bool httpsGetJson(const String &url, JsonDocument &doc, const JsonDocument *filter = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP %d dla: %s\n", code, url.c_str());
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  DeserializationError err = filter ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
                                    : deserializeJson(doc, payload);
  if (err) {
    Serial.printf("Blad JSON: %s\n", err.c_str());
    return false;
  }
  return true;
}

// Kody pogody WMO (Open-Meteo) -> kody ikon w stylu "01d"/"10n", zeby cala
// reszta kodu (rysowanie ikon, ksiezyc w nocy, korekta komfortu za slonce)
// dzialala bez zadnych zmian.
String wmoToIcon(int code, bool isDay) {
  const char *group;
  if (code == 0 || code == 1) group = "01";           // czyste / prawie czyste niebo
  else if (code == 2) group = "02";                   // czesciowe zachmurzenie
  else if (code == 3) group = "04";                   // pochmurno
  else if (code == 45 || code == 48) group = "50";    // mgla
  else if ((code >= 51 && code <= 55) || code == 61 || code == 80) group = "09";  // mzawka / slaby deszcz / lekki przelotny deszcz (3 krople)
  else if (code == 56 || code == 57 || code == 66 || code == 67) group = "14"; // marznaca mzawka/deszcz (deszcz ze sniegiem)
  else if (code == 63 || code == 65 || code == 81 || code == 82) group = "10"; // deszcz / ulewa (4 krople, bez osobnej ikony ulewy)
  else if (code == 71 || code == 73 || code == 77 || code == 85) group = "13"; // snieg / przelotny snieg
  else if (code == 75 || code == 86) group = "15";    // intensywny snieg
  else if (code == 95) group = "11";                  // burza
  else if (code == 96 || code == 99) group = "17";    // burza z gradem
  else group = "03";
  return String(group) + (isDay ? "d" : "n");
}

// Czy kod pogody WMO to zjawisko "skrajne" - takie, ktore w prognozie ma
// pierwszenstwo nad pogoda z poludnia: burza (takze z gradem), ulewa,
// intensywny snieg, marznacy deszcz/marznaca mzawka (gololedz).
bool isSevereWmo(int code) {
  return code == 95 || code == 96 || code == 99 ||   // burza / burza z gradem
         code == 65 || code == 82 ||                 // ulewa / gwaltowne przelotne opady
         code == 75 || code == 86 ||                 // intensywny snieg
         code == 56 || code == 57 || code == 66 || code == 67;  // marznacy deszcz/mzawka
}

// Opcjonalne zrodlo pogody i prognozy: MSN Pogoda (naklada sie na dane z Open-Meteo).
// Dolaczane tutaj, bo korzysta ze struktur pogody, wmoToIcon() i settingsStore zdefiniowanych wyzej.
#include "MsnWeather.h"

// Stopien ostrzezenia o wietrze w danej godzinie: z IMGW (zrodlo MSN) albo z danych tej godziny prognozy.
// title (opcjonalnie): nazwa ostrzezenia IMGW (np. "Wiatr"), nullptr przy ostrzezeniu z danych prognozy.
uint8_t windWarnAt(const char *date, int hour, const HourPoint *hp, const char **title = nullptr) {
  if (title) *title = nullptr;
  if (windFromImgw) return windAlertLevelAt(date, hour, title);
  return (hp && hp->valid) ? windDataLevel(hp->windMs, hp->gustMs) : 0;
}

// ---------------------------------------------------------------------
//  IMGW (https://danepubliczne.imgw.pl) — prawdziwe pomiary ze stacji
//  synoptycznych: temperatura, wilgotnosc, cisnienie, wiatr. Odswiezane
//  okolo raz na godzine. API zwraca liste wszystkich stacji (JSON, wartosci
//  jako napisy, brakujace jako null/pusty napis) i NIE podaje wspolrzednych,
//  wiec stacje wybieramy po nazwie.
// ---------------------------------------------------------------------

// Nazwa -> male litery, bez polskich znakow, spacji i znakow specjalnych
// ("Jelenia Góra" -> "jeleniagora"), zeby dopasowanie nie zalezalo od zapisu.
String normalizeName(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (c == 0xC3 || c == 0xC4 || c == 0xC5) {         // polskie znaki (UTF-8, 2 bajty)
      if (i + 1 >= in.length()) break;
      uint8_t d = (uint8_t)in[++i];
      char r = 0;
      if (c == 0xC4) {
        if (d == 0x84 || d == 0x85) r = 'a';
        else if (d == 0x86 || d == 0x87) r = 'c';
        else if (d == 0x98 || d == 0x99) r = 'e';
      } else if (c == 0xC5) {
        if (d == 0x81 || d == 0x82) r = 'l';
        else if (d == 0x83 || d == 0x84) r = 'n';
        else if (d == 0x9A || d == 0x9B) r = 's';
        else if (d >= 0xB9 && d <= 0xBC) r = 'z';
      } else if (c == 0xC3) {
        if (d == 0x93 || d == 0xB3) r = 'o';
      }
      if (r) out += r;
      continue;
    }
    if (c >= 'A' && c <= 'Z') out += (char)(c + 32);
    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += (char)c;
  }
  return out;
}

// Liczba z pola JSON, ktore moze byc napisem ("12.3"), liczba, null albo "".
bool getNum(JsonObject o, const char *key, float &out) {
  JsonVariant v = o[key];
  if (v.isNull()) return false;
  if (v.is<const char *>()) {
    const char *p = v.as<const char *>();
    if (!p || !*p) return false;
    char *end = nullptr;
    float f = strtof(p, &end);
    if (end == p) return false;
    out = f;
    return true;
  }
  if (v.is<float>() || v.is<int>()) {
    out = v.as<float>();
    return true;
  }
  return false;
}

// Wiek pomiaru w godzinach (data "RRRR-MM-DD" + pelna godzina). false, gdy
// zegar nie jest jeszcze zsynchronizowany lub data jest nieczytelna.
bool imgwObservationAgeHours(const char *date, int hour, float &ageH) {
  time_t nowT;
  time(&nowT);
  if (nowT < 1700000000) return false;
  int y = 0, m = 0, d = 0;
  if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3 || y == 0) return false;
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = hour;
  t.tm_isdst = -1;
  ageH = (float)(nowT - mktime(&t)) / 3600.0f;
  return true;
}

// Podmienia biezace pomiary (temperatura, wilgotnosc, cisnienie, wiatr) na
// odczyty ze stacji IMGW. Ikona, prognoza i AQI zostaja z Open-Meteo. Przy
// kazdym problemie zostaja dane z Open-Meteo, a powod trafia do weatherStatus
// (widoczny w panelu WWW) - zeby przy testach bylo jasne, skad sa liczby.
void applyImgwMeasurements() {
  AppSettings &s = settingsStore.settings;
  imgwApplied = false;   // ustawiane na true dopiero po udanej podmianie pomiarow
  String query = s.imgwStation.length() ? s.imgwStation : s.city;
  String q = normalizeName(query);

  JsonDocument filter;   // przepuszczamy tylko potrzebne pola (mniej pamieci)
  filter[0]["stacja"] = true;
  filter[0]["data_pomiaru"] = true;
  filter[0]["godzina_pomiaru"] = true;
  filter[0]["temperatura"] = true;
  filter[0]["predkosc_wiatru"] = true;
  filter[0]["kierunek_wiatru"] = true;
  filter[0]["wilgotnosc_wzgledna"] = true;
  filter[0]["cisnienie"] = true;

  JsonDocument doc;
  if (!httpsGetJson("https://danepubliczne.imgw.pl/api/data/synop", doc, &filter)) {
    snprintf(weatherStatus, sizeof(weatherStatus), L("IMGW: błąd pobierania danych - używam Open-Meteo", "IMGW: download error - using Open-Meteo"));
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    snprintf(weatherStatus, sizeof(weatherStatus), L("IMGW: nieoczekiwana odpowiedź serwera - używam Open-Meteo", "IMGW: unexpected server response - using Open-Meteo"));
    return;
  }

  // dokladne dopasowanie nazwy ma pierwszenstwo, w drugiej kolejnosci "zawiera"
  JsonObject match;
  for (JsonObject st : arr) {
    String n = normalizeName(String(st["stacja"] | ""));
    if (n.length() == 0) continue;
    if (n == q) { match = st; break; }
    if (match.isNull() && q.length() >= 3 && n.length() >= 3 &&
        (n.indexOf(q) >= 0 || q.indexOf(n) >= 0)) match = st;
  }
  if (match.isNull()) {
    snprintf(weatherStatus, sizeof(weatherStatus),
             L("IMGW: nie znaleziono stacji '%s' (sprawdź listę stacji) - używam Open-Meteo", "IMGW: station '%s' not found (check the station list) - using Open-Meteo"), query.c_str());
    return;
  }

  const char *stName = match["stacja"] | "?";
  const char *dateStr = match["data_pomiaru"] | "";
  float hourF = 0;
  bool haveHour = getNum(match, "godzina_pomiaru", hourF);

  float ageH = 0;
  if (haveHour && imgwObservationAgeHours(dateStr, (int)hourF, ageH) && (ageH > 6.0f || ageH < -3.0f)) {
    snprintf(weatherStatus, sizeof(weatherStatus),
             L("IMGW: dane stacji %s nieaktualne (pomiar %s, godz. %d:00) - używam Open-Meteo", "IMGW: station %s data out of date (reading %s, %d:00) - using Open-Meteo"),
             stName, dateStr, (int)hourF);
    return;
  }

  float t = 0;
  if (!getNum(match, "temperatura", t)) {
    snprintf(weatherStatus, sizeof(weatherStatus),
             L("IMGW: stacja %s nie podaje temperatury - używam Open-Meteo", "IMGW: station %s reports no temperature - using Open-Meteo"), stName);
    return;
  }

  // Temperatura i odczuwalna: odczuwalna z modelu (wiatr, slonce) przesuwamy
  // o roznice miedzy zmierzona a modelowa temperatura.
  float tOut = (s.units == "imperial") ? t * 9.0f / 5.0f + 32.0f : t;
  current.feelsLike += tOut - current.temp;
  current.temp = tOut;

  float v = 0;
  if (getNum(match, "wilgotnosc_wzgledna", v)) current.humidity = (int)(v + 0.5f);
  if (getNum(match, "cisnienie", v)) current.pressure = v;
  if (getNum(match, "predkosc_wiatru", v)) current.windSpeed = v;     // m/s
  if (getNum(match, "kierunek_wiatru", v)) current.windDeg = (int)v;  // skad wieje

  imgwApplied = true;
  snprintf(imgwStationName, sizeof(imgwStationName), "%s", stName);
  if (haveHour) snprintf(imgwHourStr, sizeof(imgwHourStr), "%02d:00", (int)hourF);
  else snprintf(imgwHourStr, sizeof(imgwHourStr), "--:--");
  snprintf(weatherStatus, sizeof(weatherStatus), L("Pomiary: IMGW – %s (pomiar %s, godz. %d:00)", "Readings: IMGW – %s (reading %s, %d:00)"),
           stName, dateStr, (int)hourF);
}

// Jednorazowe geokodowanie miasta -> lat/lon (Open-Meteo wymaga wspolrzednych).
// Wywolywane leniwie przez fetchWeather(); po zmianie miasta w panelu WWW
// urzadzenie i tak sie restartuje, wiec geokodowanie powtarza sie samo.
bool geocodeCity() {
  AppSettings &s = settingsStore.settings;
  // Wspolrzedne wpisane w panelu maja pierwszenstwo - bez zapytania o geokodowanie
  if (s.lat.length() && s.lon.length()) {
    cityLat = s.lat.toFloat();
    cityLon = s.lon.toFloat();
    geoReady = true;
    return true;
  }
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + encodeUrlComponent(s.city);
  url += "&count=1&language=pl&format=json";
  if (s.countryCode.length() == 2) url += "&countryCode=" + s.countryCode;

  JsonDocument doc;
  if (!httpsGetJson(url, doc)) return false;

  JsonObject first = doc["results"][0].as<JsonObject>();
  if (first.isNull()) {
    Serial.println("Geokodowanie: nie znaleziono miasta");
    return false;
  }
  cityLat = first["latitude"] | 0.0;
  cityLon = first["longitude"] | 0.0;
  geoReady = true;
  return true;
}

// Godziny sloneczne liczone z ikon godzinowki (zrodlo MSN, ktore nie podaje nasłonecznienia).
// Kazda godzina liczy sie tylko w czesci zawartej miedzy wschodem a zachodem slonca tego dnia
// (np. godzina, w ktorej slonce wschodzi o :40, liczy sie za 20 min). Waga ikony: czyste/prawie czyste
// niebo (kody 0, 1) = 1, czesciowe zachmurzenie (kod 2) = 0,5, czeste przejasnienia (kod 4 z MSN) = 0,25, pozostale = 0. Gdy brakuje wschodu/zachodu
// albo ikon z wiecej niz 20% godzin dnia, zostaje wartosc z Open-Meteo (nic nie nadpisujemy).
void applyIconSunHours() {
  for (uint8_t i = 0; i < forecastCount && i < MAX_FORECAST; i++) {
    int sr = forecast[i].sunriseMin, ss = forecast[i].sunsetMin;
    if (sr < 0 || ss <= sr) continue;
    float dayMin = 0, knownMin = 0, sunMin = 0;
    for (int h = 0; h < 24; h++) {
      int ov = min(ss, (h + 1) * 60) - max(sr, h * 60);   // minuty tej godziny, w ktorych slonce jest nad horyzontem
      if (ov <= 0) continue;
      dayMin += ov;
      const HourPoint &hp = hourly[i][h];
      if (!hp.valid || hp.code == 255) continue;
      knownMin += ov;
      if (hp.code == 0 || hp.code == 1) sunMin += ov;                        // slonce / prawie bezchmurnie
      else if (hp.code == 2) sunMin += ov * 0.5f;                            // czesciowe zachmurzenie
      else if (hp.code == MSN_WMO_MOSTLY_CLOUDY) sunMin += ov * 0.25f;       // czeste przejasnienia (MSN "duze zachmurzenie")
    }
    if (dayMin <= 0 || knownMin < dayMin * 0.8f) continue;
    forecast[i].sunHours = (int)lroundf(sunMin / 60.0f);
  }
}

// Jedno zapytanie = biezaca pogoda + prognoza dzienna (min/max/kod pogody).
// Ikona dnia "z przebiegu dnia" (opcja panelu "flow"): zamiast samej godziny 12:00 bierzemy caly dzien 8:00-20:00.
// 1. Deszcz: gdy co najmniej 2 godziny maja deszcz (kody 51-55, 61, 63, 65, 80-82; szansa >= 40% albo brak danych):
//    - "slonce z chmura i kroplami" (grupa 05), gdy deszcz trwa krocej niz 3/4 dnia i jest przerwa ze sloncem:
//      co najmniej kFlowMinSun "punktow slonca" (godzina z czesciowym zachmurzeniem lub lepiej = 1 pkt,
//      w szczycie dnia 10:00-14:00 = 2 pkt - slonce kolo poludnia waży wiecej),
//    - inaczej zwykla ikona deszczu (chmura z kroplami).
// 2. Bez takiego deszczu - zachmurzenie usrednione z calego dnia: kazda godzina dostaje "stopien zachmurzenia"
//    (0 = bezchmurnie ... 1 = pochmurno), srednia wybiera ikone 01 / 02 / 03 / 04. Dzieki temu dzien, w ktorym
//    tylko w poludnie jest pelne slonce, a reszta godzin jest lekko zachmurzona, dostaje "czesciowe zachmurzenie".
// Zjawiska skrajne z poludnia (burza, snieg, marznacy deszcz) i mgla zostaja bez zmian - to nie sa grupy 01-04.
static const int kFlowFrom = 8, kFlowTo = 20, kFlowMinHours = 2;
static const int kFlowNoonFrom = 10, kFlowNoonTo = 14;   // "szczyt dnia" - slonce w tych godzinach liczy sie podwojnie
static const int kFlowMinSun = 3;                        // min. "punktow slonca", zeby pokazac slonce z chmura i kroplami
bool isRainWmo(int code) {
  return (code >= 51 && code <= 55) || code == 61 || code == 63 || code == 65 || (code >= 80 && code <= 82);
}
// stopien zachmurzenia godziny (0..1) do sredniej dnia
float cloudinessOfWmo(int code) {
  if (code == 0) return 0.0f;                         // bezchmurnie
  if (code == 1) return 0.15f;                        // prawie bezchmurnie
  if (code == 2) return 0.5f;                         // czesciowe zachmurzenie
  if (code == MSN_WMO_MOSTLY_CLOUDY) return 0.75f;    // czeste przejasnienia / duze zachmurzenie (MSN)
  return 1.0f;                                        // pochmurno, mgla, pojedyncze opady
}
void applyDayFlowIcons() {
  if (settingsStore.settings.msnDayIcon != "flow") return;
  for (uint8_t i = 0; i < forecastCount && i < MAX_FORECAST; i++) {
    String g = forecast[i].icon.substring(0, 2);
    if (g != "01" && g != "02" && g != "03" && g != "04") continue;     // poludnie spokojne (slonce / chmury)
    int valid = 0, rainH = 0, rainCode = -1, sunnyH = 0;
    float cloudSum = 0;
    for (int h = kFlowFrom; h <= kFlowTo; h++) {
      const HourPoint &hp = hourly[i][h];
      if (!hp.valid || hp.code == 255) continue;
      valid++;
      cloudSum += cloudinessOfWmo(hp.code);
      if (cloudinessOfWmo(hp.code) <= 0.5f)                           // godzina ze sloncem (do czesciowego zachmurzenia);
        sunnyH += (h >= kFlowNoonFrom && h <= kFlowNoonTo) ? 2 : 1;   // w szczycie dnia (10-14) liczy sie podwojnie
      if (isRainWmo(hp.code) && (hp.rain < 0 || hp.rain >= 40)) { rainH++; if (hp.code > rainCode) rainCode = hp.code; }
    }
    if (valid == 0) continue;
    if (rainH >= kFlowMinHours) {
      // pada przez co najmniej 3/4 dnia albo slonca prawie nie ma - zwykla ikona deszczu (chmura z kroplami);
      // "slonce z chmura i kroplami", gdy obok opadow jest wyrazna przerwa ze sloncem (zwlaszcza okolo poludnia)
      if (rainH * 4 >= valid * 3 || sunnyH < kFlowMinSun) forecast[i].icon = wmoToIcon(rainCode, true);
      else forecast[i].icon = "05d";                                      // przelotne opady i slonce w ciagu dnia
      continue;
    }
    if (valid < (kFlowTo - kFlowFrom + 1) / 2) continue;                 // za malo godzin - zostaje ikona z poludnia
    float avg = cloudSum / valid;
    const char *grp = avg < 0.2f ? "01" : avg < 0.45f ? "02" : avg < 0.7f ? "03" : "04";
    forecast[i].icon = String(grp) + "d";
  }
}

bool fetchWeather() {
  AppSettings &s = settingsStore.settings;
  if (!geoReady && !geocodeCity()) return false;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cityLat, 4);
  url += "&longitude=" + String(cityLon, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m,wind_gusts_10m";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset,sunshine_duration";
  // godzinowe: kod pogody (ikony z poludnia + ekran szczegolow dnia), temperatura,
  // prawdopodobienstwo opadow, wiatr, dzien/noc
  url += "&hourly=weather_code,temperature_2m,precipitation_probability,wind_speed_10m,wind_direction_10m,wind_gusts_10m,pressure_msl,is_day";
  url += "&timezone=auto&wind_speed_unit=ms&forecast_days=5";
  if (s.units == "imperial") url += "&temperature_unit=fahrenheit";
  // Wybrany model pogodowy (do testow). "best_match" = domyslny dobor
  // automatyczny, wtedy parametru w ogole nie wysylamy. Wartosc pochodzi z
  // listy dozwolonych id (walidacja w Settings.h/WebPanel.h), wiec jest
  // bezpieczna do wstawienia w adres.
  if (s.weatherModel != "best_match") url += "&models=" + s.weatherModel;
  Serial.printf("Open-Meteo: model=%s\n", s.weatherModel.c_str());

  JsonDocument doc;
  if (!httpsGetJson(url, doc)) return false;

  JsonObject cur = doc["current"].as<JsonObject>();
  // Nie kazdy model obejmuje kazde miejsce (np. ICON-D2 ma ograniczony
  // obszar) - wtedy API zwraca puste (null) pola. Bez temperatury nie mamy
  // czego pokazac, wiec traktujemy to jak blad pobierania (zamiast cicho
  // wyswietlac zera) i NIE ruszamy poprzednich danych.
  if (cur.isNull() || cur["temperature_2m"].isNull()) {
    Serial.printf("Model '%s' nie zwrocil danych dla tej lokalizacji\n", s.weatherModel.c_str());
    return false;
  }

  // Pozostale pola: gdy model ich nie dostarcza (null), zostaje poprzednia wartosc.
  auto num = [&](const char *key, float keep) -> float {
    JsonVariant v = cur[key];
    return v.isNull() ? keep : v.as<float>();
  };

  bool isDay = (cur["is_day"] | 1) == 1;
  current.temp = cur["temperature_2m"].as<float>();
  current.feelsLike = num("apparent_temperature", current.temp);
  current.humidity = (int)num("relative_humidity_2m", current.humidity);
  current.pressure = num("pressure_msl", current.pressure);
  current.windSpeed = num("wind_speed_10m", current.windSpeed);
  current.gustMs = num("wind_gusts_10m", 0.0f);   // brak danych -> 0 (bez alarmu)
  current.windDeg = (int)num("wind_direction_10m", current.windDeg);
  current.icon = wmoToIcon(cur["weather_code"] | -1, isDay);  // brak kodu -> ikona chmury
  if (s.measureSource == "imgw") {
    applyImgwMeasurements();
  } else {
    snprintf(weatherStatus, sizeof(weatherStatus), L("Pomiary: Open-Meteo (model)", "Readings: Open-Meteo (model)"));
  }
  recordApiPressure(current.pressure);
  current.valid = true;

  // Prognoza: tablice dzienne; indeks 0 = dzisiaj (pomijamy, pokazujemy od jutra)
  JsonArray dTime = doc["daily"]["time"].as<JsonArray>();
  {  // wschod / zachod slonca dzisiaj ("RRRR-MM-DDTHH:MM" -> "HH:MM") - ekran informacyjny
    const char *sr = doc["daily"]["sunrise"][0] | "";
    const char *ss = doc["daily"]["sunset"][0] | "";
    if (strlen(sr) >= 16) snprintf(sunriseStr, sizeof(sunriseStr), "%.5s", sr + 11);
    if (strlen(ss) >= 16) snprintf(sunsetStr, sizeof(sunsetStr), "%.5s", ss + 11);
    const char *ct = cur["time"] | "";
    if (strlen(ct) >= 16) snprintf(omObsTime, sizeof(omObsTime), "%.5s", ct + 11);
    struct tm ft;
    if (getLocalTime(&ft, 50)) snprintf(fetchTimeStr, sizeof(fetchTimeStr), "%02d:%02d", ft.tm_hour, ft.tm_min);
  }
  JsonArray dCode = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray dMax  = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray dMin  = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray dProb = doc["daily"]["precipitation_probability_max"].as<JsonArray>();
  JsonArray dSun = doc["daily"]["sunshine_duration"].as<JsonArray>();   // sekundy ze sloncem w dobie
  JsonArray dSr = doc["daily"]["sunrise"].as<JsonArray>();
  JsonArray dSs = doc["daily"]["sunset"].as<JsonArray>();
  JsonArray hTime = doc["hourly"]["time"].as<JsonArray>();
  JsonArray hCode = doc["hourly"]["weather_code"].as<JsonArray>();
  JsonArray hTemp = doc["hourly"]["temperature_2m"].as<JsonArray>();
  JsonArray hProb = doc["hourly"]["precipitation_probability"].as<JsonArray>();
  JsonArray hWind = doc["hourly"]["wind_speed_10m"].as<JsonArray>();
  JsonArray hGust = doc["hourly"]["wind_gusts_10m"].as<JsonArray>();
  JsonArray hPress = doc["hourly"]["pressure_msl"].as<JsonArray>();
  JsonArray hDir  = doc["hourly"]["wind_direction_10m"].as<JsonArray>();
  JsonArray hDay  = doc["hourly"]["is_day"].as<JsonArray>();

  // Alarm porywow: do aktualnych porywow dokladamy maksimum z prognozy na reszte
  // biezacej doby. "Teraz" i date bierzemy z pola current.time odpowiedzi
  // ("RRRR-MM-DDTHH:MM", czas lokalny - tak jak w tablicy godzinowej), wiec nie
  // zalezy to od zegara NTP.
  {
    const char *curTime = cur["time"] | "";
    int curHour = (strlen(curTime) >= 13) ? atoi(curTime + 11) : 0;
    for (size_t j = 0; j < hTime.size() && j < hGust.size(); j++) {
      const char *ht = hTime[j] | "";
      if (strlen(ht) < 13 || strncmp(ht, curTime, 10) != 0) continue;   // tylko dzisiejsza data
      if (!GUST_COUNT_PAST_HOURS && atoi(ht + 11) < curHour) continue;  // pomijamy minione godziny
      if (!hGust[j].isNull()) current.gustMs = max(current.gustMs, hGust[j].as<float>());
    }
  }

  // Trend cisnienia z serii godzinowej API (dzisiejsza doba od 00:00): roznica miedzy
  // biezaca godzina a godzina TREND_WINDOW_HOURS wczesniej. Dostepny od razu po starcie
  // (nie trzeba zbierac wlasnej historii). Od 01:00 okno jest krotsze (1-2 h) i
  // przeliczane na 3 h; w pierwszej godzinie doby zostaje trend z wlasnej historii.
  current.hasPressTrend = false;
  {
    const char *curTime = cur["time"] | "";
    int curHour = (strlen(curTime) >= 13) ? atoi(curTime + 11) : -1;
    if (curHour >= 1) {
      int win = (curHour < TREND_WINDOW_HOURS) ? curHour : TREND_WINDOW_HOURS;
      bool haveNow = false, havePast = false;
      float pNow = 0, pPast = 0;
      for (size_t j = 0; j < hTime.size() && j < hPress.size(); j++) {
        const char *ht = hTime[j] | "";
        if (strlen(ht) < 13 || strncmp(ht, curTime, 10) != 0 || hPress[j].isNull()) continue;
        int hh = atoi(ht + 11);
        if (hh == curHour) { pNow = hPress[j].as<float>(); haveNow = true; }
        if (hh == curHour - win) { pPast = hPress[j].as<float>(); havePast = true; }
      }
      if (haveNow && havePast) {
        float change3h = (pNow - pPast) * TREND_WINDOW_HOURS / win;
        current.pressTrend = (change3h >= TREND_THRESHOLD_HPA) ? 1 : (change3h <= -TREND_THRESHOLD_HPA ? -1 : 0);
        current.hasPressTrend = true;
        Serial.printf("Trend cisnienia: teraz %.1f, %d h temu %.1f -> zmiana/3h %+.2f hPa -> %d\n",
                      pNow, win, pPast, change3h, current.pressTrend);
      }
    }
  }

  // Ikona "teraz" z godzinowej serii - dokladnie ten sam kod pogody i dzien/noc, z ktorych
  // powstaje wiersz biezacej godziny w godzinowce (spojnosc: ekran glowny = podswietlony
  // wiersz). Dotad ikona szla z bloku "current" (osobny odczyt na chwile np. 14:15), wiec
  // potrafila sie rozjechac z godzina 14:00. Brak danych godzinowych -> zostaje ikona z "current".
  {
    const char *curTime = cur["time"] | "";
    char key[20];
    snprintf(key, sizeof(key), "%.13s:00", curTime);
    for (size_t j = 0; s.iconSource == "hourly" && j < hTime.size() && j < hCode.size(); j++) {   // wybor w panelu WWW
      const char *ht = hTime[j] | "";
      if (strcmp(ht, key) != 0) continue;
      if (!hCode[j].isNull()) {
        bool hourIsDay = (hDay[j] | (isDay ? 1 : 0)) == 1;
        current.icon = wmoToIcon(hCode[j].as<int>(), hourIsDay);
      }
      break;
    }
  }

  // Dzisiaj = indeks 0 tablic dziennych: zapamietujemy podsumowanie (data, dzien
  // tygodnia, max/min, opady) do ekranu szczegolow. Brak danych -> data pusta.
  todayDay = ForecastDay();
  if (dTime.size() > 0 && !dMax[0].isNull() && !dMin[0].isNull()) {
    const char *td = dTime[0] | "";
    int ty0 = 0, tm0 = 0, tdd = 0;
    if (sscanf(td, "%d-%d-%d", &ty0, &tm0, &tdd) == 3 && ty0 != 0) {
      struct tm t0 = {};
      t0.tm_year = ty0 - 1900;
      t0.tm_mon = tm0 - 1;
      t0.tm_mday = tdd;
      t0.tm_hour = 12;
      t0.tm_isdst = -1;
      mktime(&t0);
      strncpy(todayDay.date, td, 10);
      todayDay.date[10] = 0;
      todayDay.wday = t0.tm_wday;
      todayDay.tempMax = dMax[0] | 0.0;
      todayDay.tempMin = dMin[0] | 0.0;
      todayDay.rainProb = dProb[0].isNull() ? -1 : dProb[0].as<int>();
    }
  }

  uint8_t n = 0;
  for (size_t i = 1; i < dTime.size() && n < s.forecastDays && n < MAX_FORECAST; i++) {
    const char *dateStr = dTime[i] | "";
    int y = 0, m = 0, d = 0;
    if (sscanf(dateStr, "%d-%d-%d", &y, &m, &d) != 3 || y == 0) continue;
    if (dMax[i].isNull() || dMin[i].isNull()) continue;   // model bez danych na ten dzien

    struct tm tmD = {};
    tmD.tm_year = y - 1900;
    tmD.tm_mon = m - 1;
    tmD.tm_mday = d;
    tmD.tm_hour = 12;
    tmD.tm_isdst = -1;
    mktime(&tmD);  // uzupelnia tm_wday (dzien tygodnia)

    forecast[n].label = dayNameShort(tmD.tm_wday);
    forecast[n].wday = tmD.tm_wday;
    strncpy(forecast[n].date, dateStr, 10);
    forecast[n].date[10] = 0;
    // Ikona dnia: kod pogody z godziny FORECAST_ICON_HOUR (domyslnie 12:00 - typowa
    // pogoda w ciagu dnia), a nie dzienny kod Open-Meteo = "najgorsze zjawisko
    // doby" (krotki przelotny deszcz zmienialby ikone calego dnia). Godziny
    // dopasowujemy po tekscie ("RRRR-MM-DDT12:00"), a nie po indeksie, bo w dni
    // zmiany czasu doba nie ma 24 godzin. Brak kodu godzinowego -> kod dzienny.
    int dailyCode = dCode[i] | -1;
    int code = dailyCode;
    char target[24];
    snprintf(target, sizeof(target), "%sT%02d:00", dateStr, FORECAST_ICON_HOUR);
    for (size_t j = 0; j < hTime.size() && j < hCode.size(); j++) {
      const char *ht = hTime[j] | "";
      if (strcmp(target, ht) == 0) {
        code = hCode[j] | code;   // null (model bez kodu godzinowego) -> zostaje dzienny
        break;
      }
    }
    // Zjawisko skrajne w ciagu doby ma pierwszenstwo nad spokojna pogoda z poludnia
    if (isSevereWmo(dailyCode)) code = dailyCode;
    forecast[n].icon = wmoToIcon(code, true);
    forecast[n].tempMax = dMax[i] | 0.0;
    forecast[n].tempMin = dMin[i] | 0.0;
    // nie kazdy model podaje prawdopodobienstwo opadow (null) -> -1 = brak danych
    forecast[n].rainProb = dProb[i].isNull() ? -1 : dProb[i].as<int>();
    forecast[n].sunHours = (i < dSun.size() && !dSun[i].isNull()) ? (int)lroundf(dSun[i].as<float>() / 3600.0f) : -1;
    // wschod / zachod tego dnia ("RRRR-MM-DDTHH:MM") - potrzebne do liczenia godzin slonecznych z ikon (zrodlo MSN)
    {
      const char *sr = (i < dSr.size()) ? (dSr[i] | "") : "";
      const char *ss = (i < dSs.size()) ? (dSs[i] | "") : "";
      forecast[n].sunriseMin = (strlen(sr) >= 16) ? (int16_t)(atoi(sr + 11) * 60 + atoi(sr + 14)) : -1;
      forecast[n].sunsetMin = (strlen(ss) >= 16) ? (int16_t)(atoi(ss + 11) * 60 + atoi(ss + 14)) : -1;
    }
    n++;
  }
  forecastCount = n;

  // Dane godzinowe do ekranu szczegolow: kazda godzina trafia do dnia o tej samej
  // dacie ("RRRR-MM-DDTHH:00") - do dzisiaj albo do dnia prognozy. Godzina bez
  // temperatury = niewazna.
  for (uint8_t d = 0; d < MAX_FORECAST; d++)
    for (int h = 0; h < 24; h++) hourly[d][h] = HourPoint();
  for (int h = 0; h < 24; h++) hourlyToday[h] = HourPoint();

  auto fillHour = [&](HourPoint &hp, size_t j) {
    hp.valid = true;
    hp.temp = (int16_t)lroundf(hTemp[j].as<float>());
    hp.rain = hProb[j].isNull() ? -1 : (int8_t)hProb[j].as<int>();
    hp.code = hCode[j].isNull() ? 255 : (uint8_t)hCode[j].as<int>();
    hp.windMs = hWind[j] | 0.0f;
    hp.gustMs = (j < hGust.size()) ? (hGust[j] | 0.0f) : 0.0f;
    hp.windDir = hDir[j] | 0;
    hp.isDay = (hDay[j] | 1) == 1;
  };

  for (size_t j = 0; j < hTime.size(); j++) {
    const char *ht = hTime[j] | "";
    if (strlen(ht) < 13) continue;
    int hh = atoi(ht + 11);
    if (hh < 0 || hh > 23 || hTemp[j].isNull()) continue;
    if (todayDay.date[0] && strncmp(ht, todayDay.date, 10) == 0) {
      fillHour(hourlyToday[hh], j);
      continue;
    }
    for (uint8_t d = 0; d < n; d++) {
      if (strncmp(ht, forecast[d].date, 10) != 0) continue;
      fillHour(hourly[d][hh], j);
      break;
    }
  }

  // Ostrzezenia (tylko zrodlo MSN) liczymy od zera przy kazdym pobraniu - po przelaczeniu na
  // Open-Meteo albo po bledzie MSN nie moga zostac stare.
  todayDay.alertLevel = 0;
  todayDay.alertText[0] = 0;
  todayDay.windAlertLevel = 0;
  for (uint8_t i = 0; i < MAX_FORECAST; i++) {
    forecast[i].alertLevel = 0;
    forecast[i].alertText[0] = 0;
    forecast[i].windAlertLevel = 0;
  }
  msnWindAlertCount = 0;

  // Zrodlo MSN (panel WWW): nadpisuje biezace dane i prognoze dni, a przy bledzie zostaje Open-Meteo.
  // Gdy pomiary pochodza z IMGW, z MSN bierzemy tylko ikone, porywy i prognoze.
  bool msnOk = false;
  if (s.weatherSource == "msn") msnOk = applyMsnWeather(s.measureSource == "imgw" && imgwApplied);
  snprintf(sourceLabel, sizeof(sourceLabel), "%s", msnOk ? "MSN" : "OpenMeteo");
  // ostrzezenie o wietrze bez MSN (brak ostrzezen IMGW): z prognozy - dzis od biezacej godziny, kolejne dni cala doba
  windFromImgw = msnOk;
  if (!msnOk) {
    struct tm ti;
    int ch = getLocalTime(&ti, 50) ? ti.tm_hour : 0;
    uint8_t l = windDataLevel(current.windSpeed, current.gustMs);   // gustMs = maks. porywy do konca doby
    for (int h = ch; h < 24; h++)
      if (hourlyToday[h].valid) l = max(l, windDataLevel(hourlyToday[h].windMs, hourlyToday[h].gustMs));
    todayDay.windAlertLevel = l;
    for (uint8_t i = 0; i < forecastCount && i < MAX_FORECAST; i++) {
      uint8_t d = 0;
      for (int h = 0; h < 24; h++)
        if (hourly[i][h].valid) d = max(d, windDataLevel(hourly[i][h].windMs, hourly[i][h].gustMs));
      forecast[i].windAlertLevel = d;
    }
  }
  sourceImgw = (s.measureSource == "imgw" && imgwApplied);
  if (msnOk) applyIconSunHours();   // zrodlo MSN: godziny sloneczne z ikon godzinowki (Open-Meteo: zostaja dane modelu)

  // Ikona DZISIAJ (uzywana m.in. przez buildTomorrowLines() do wykrycia "zalamania pogody"): ta sama zasada
  // co w kolumnach prognozy - pogoda z godziny FORECAST_ICON_HOUR, a zjawisko skrajne w dobie ma pierwszenstwo.
  // Liczona po nalozeniu MSN, wiec przy zrodle MSN pochodzi z godzinowki MSN. Brak danych -> ikona "teraz".
  if (todayDay.date[0]) {
    int dailyCode = (dCode.size() > 0 && !dCode[0].isNull()) ? dCode[0].as<int>() : -1;
    const HourPoint &noon = hourlyToday[FORECAST_ICON_HOUR];
    int code = (noon.valid && noon.code != 255) ? noon.code : dailyCode;
    if (isSevereWmo(dailyCode)) code = dailyCode;
    todayDay.icon = (code >= 0) ? wmoToIcon(code, true) : current.icon;
  }
  applyDayFlowIcons();
  return true;
}

// Jakosc powietrza: Open-Meteo Air Quality (europejski indeks AQI, 0-100+)
// przeliczony na nasza skale 1-5 (kolory kropki bez zmian).
// UV i pylki z odpowiedzi Air Quality: seria godzinowa na dzisiaj zaczyna sie o 00:00 czasu lokalnego,
// wiec indeks w tablicy = godzina doby.
void parseAirExtras(JsonDocument &doc) {
  struct tm ti;
  int hh = getLocalTime(&ti, 50) ? ti.tm_hour : -1;

  airExtras.uvValid = false;
  JsonArray uv = doc["hourly"]["uv_index"].as<JsonArray>();
  if (!uv.isNull() && hh >= 0 && hh < (int)uv.size() && !uv[hh].isNull()) {
    float mx = 0;
    int mh = -1;
    for (size_t i = 0; i < uv.size() && i < 24; i++) {
      if (uv[i].isNull()) continue;
      float f = uv[i].as<float>();
      if (mh < 0 || f > mx) { mx = f; mh = (int)i; }
    }
    airExtras.uvValid = true;
    airExtras.uvNow = uv[hh].as<float>();
    airExtras.uvMax = mx;
    airExtras.uvMaxHour = (int8_t)mh;
  }

  static const char *const keys[5] = {"alder_pollen", "birch_pollen", "grass_pollen", "mugwort_pollen", "ragweed_pollen"};
  airExtras.pollenValid = false;
  for (int k = 0; k < 5; k++) {
    airExtras.pollen[k] = 0;
    JsonArray a = doc["hourly"][keys[k]].as<JsonArray>();
    if (!a.isNull() && hh >= 0 && hh < (int)a.size() && !a[hh].isNull()) {
      airExtras.pollen[k] = a[hh].as<float>();
      airExtras.pollenValid = true;
    }
  }
}

bool fetchAirQuality() {
  if (!geoReady) return false;

  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(cityLat, 4);
  url += "&longitude=" + String(cityLon, 4) + "&current=european_aqi,pm2_5,pm10";
  url += "&hourly=uv_index,alder_pollen,birch_pollen,grass_pollen,mugwort_pollen,ragweed_pollen&forecast_days=1&timezone=auto";

  JsonDocument doc;
  if (!httpsGetJson(url, doc)) return false;

  parseAirExtras(doc);

  JsonVariant v = doc["current"]["european_aqi"];
  if (v.isNull()) return false;

  float e = v.as<float>();
  airExtras.aqiValue = e;
  airExtras.pmValid = !doc["current"]["pm2_5"].isNull() && !doc["current"]["pm10"].isNull();
  if (airExtras.pmValid) {
    airExtras.pm25 = doc["current"]["pm2_5"].as<float>();
    airExtras.pm10 = doc["current"]["pm10"].as<float>();
  }
  currentAqi = (e <= 20) ? 1 : (e <= 40) ? 2 : (e <= 60) ? 3 : (e <= 80) ? 4 : 5;
  return true;
}

// ---------------------------------------------------------------------
//  Rysowanie glownego ekranu
// ---------------------------------------------------------------------
void drawStaticLayout(const ThemeColors &th) {
  gfx->fillScreen(th.bg);
  gfx->drawFastHLine(14, 92, LCD_H_RES - 28, th.divider);
  gfx->drawFastHLine(14, 204, LCD_H_RES - 28, th.divider);
}

// Dodatkowy odstep [px] w zegarze - dwa niezalezne parametry, zwieksz/zmniejsz wedle gustu:
//  CLOCK_DIGIT_SPACING - miedzy cyframi (np. miedzy "1" a "4" w 14:32)
//  CLOCK_COLON_SPACING - miedzy cyfra a dwukropkiem, po OBU stronach dwukropka
static const int CLOCK_DIGIT_SPACING = 3;
static const int CLOCK_COLON_SPACING = 3;

// Ikona baterii w lewym gornym rogu (jak w smartfonie): obrys ogniwa wypelniony proporcjonalnie
// do poziomu, bez procentow. Przy ladowaniu piorun na srodku ogniwa i zielone wypelnienie.
// Brak ogniwa (zasilanie z USB): ikona ukryta (stan widac na ekranie informacji: "Zasilanie: USB").
// Niski poziom (<= BATTERY_LOW_PERCENT) - czerwone wypelnienie.
// Piorun ma obwodke w kolorze tla, zeby byl czytelny niezaleznie od koloru wypelnienia pod nim.
// Rysowana po kazdym czyszczeniu tego obszaru (drawClock zamalowuje caly pas zegara).
static void drawBolt(int x, int y, uint16_t col) {   // pole 10 x 16, (x,y) = lewy gorny rog
  gfx->fillTriangle(x + 7, y, x, y + 9, x + 6, y + 9, col);
  gfx->fillTriangle(x + 4, y + 7, x + 10, y + 7, x + 3, y + 16, col);
}

// Niski poziom (czerwone wypelnienie) - wypelnienie miga: loop() co BATTERY_BLINK_MS przelacza batteryBlinkOff
// i przerysowuje ikone.
static const unsigned long BATTERY_BLINK_MS = 700;
bool batteryBlinkOff = false;

bool batteryLowAlarm() {
  return battery.ready && battery.present && !battery.charging && !battery.external && !battery.usbFull
         && battery.percent <= BATTERY_LOW_PERCENT && settingsStore.settings.showBattery;
}

void drawBatteryIcon(const ThemeColors &th) {
  gfx->fillRect(4, 4, 66, 40, th.bg);
  if (!settingsStore.settings.showBattery || !battery.ready || !battery.present) return;

  const uint16_t COL_LOW = 0xE8E4;      // czerwony
  const uint16_t COL_CHG = 0x3E68;      // zielony
  // obrys ogniwa: y 7..24 - srodek na tej samej wysokosci co kreski zasiegu Wi-Fi (y 8..23)
  const int bx = 10, by = 7, bw = 34, bh = 18;

  if (battery.external) return;                   // brak ogniwa (zasilanie z USB) - bez ikony
  bool bolt = battery.charging || battery.usbFull;   // ladowanie albo pelna na zasilaczu (obie zielone, z piorunem)
  uint8_t pct = battery.usbFull ? 100 : battery.percent;
  uint16_t fillCol = (battery.charging || battery.usbFull) ? COL_CHG   // ladowanie i pelna na zasilaczu - zielona
                   : (!battery.external && pct <= BATTERY_LOW_PERCENT) ? COL_LOW : th.text;

  gfx->drawRoundRect(bx, by, bw, bh, 3, th.muted);
  gfx->drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 2, th.muted);   // obrys 2 px
  gfx->fillRect(bx + bw, by + 5, 3, bh - 10, th.muted);                // biegun "+"
  int innerW = bw - 6;
  int fw = (pct * innerW + 50) / 100;
  if (pct > 0 && fw < 2) fw = 2;                                        // minimalny pasek, zeby bylo widac, ze cos jest
  bool hideFill = batteryLowAlarm() && batteryBlinkOff;  // miganie przy niskim poziomie
  if (fw > 0 && !hideFill) gfx->fillRect(bx + 3, by + 3, fw, bh - 6, fillCol);

  if (bolt) {
    const int lx = bx + bw / 2 - 5, ly = by + 1;   // piorun 10x16 na srodku ogniwa
    for (int dx = -1; dx <= 1; dx++)                 // obwodka w kolorze tla
      for (int dy = -1; dy <= 1; dy++)
        if (dx || dy) drawBolt(lx + dx, ly + dy, th.bg);
    drawBolt(lx, ly, th.text);                       // piorun w kolorze tekstu (bialy)
  }
}

void drawClock(const ThemeColors &th) {
  AppSettings &s = settingsStore.settings;
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 100)) return;

  char timeStr[8];
  if (s.use24h) {
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeInfo);
  } else {
    strftime(timeStr, sizeof(timeStr), "%I:%M", &timeInfo);
  }

  char dateStr[48];
  if (langEn())                     // "Sunday, November 15"
    snprintf(dateStr, sizeof(dateStr), "%s, %s %d", dayNameFull(timeInfo.tm_wday), monthName(timeInfo.tm_mon), timeInfo.tm_mday);
  else                              // "Niedziela, 15 Listopada"
    snprintf(dateStr, sizeof(dateStr), "%s, %d %s", dayNameFull(timeInfo.tm_wday), timeInfo.tm_mday, monthName(timeInfo.tm_mon));

  gfx->fillRect(0, 10, LCD_H_RES, 80, th.bg);

  printCenteredSpaced(timeStr, 14, FONT_HUGE, th.text, th.bg, CLOCK_DIGIT_SPACING, CLOCK_COLON_SPACING);
  printCentered(dateStr, 66, FONT_SMALL, th.accent, th.bg);
  drawBatteryIcon(th);
}

// Zamienia stopnie (0-359) na skrocony kierunek wiatru (N, NE, E, SE, S, SW, W, NW)
const char *windDirLabel(int deg) {
  static const char *dirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int idx = ((deg + 22) / 45) % 8;
  if (idx < 0) idx += 8;
  return dirs[idx];
}

// Podpis wiatru do dolnej linii w dwoch czesciach: wartosc ("SW 10" / "SW 2.8") i jednostka ("km/h" / "m/s").
// Jednostke rysujemy osobno, z malym odstepem (patrz printWithUnit).
// Predkosc trzymamy wewnetrznie w m/s; do km/h przeliczamy tylko przy wyswietlaniu.
void formatWindLabel(char *val, size_t n, const char **unit, int deg, float windSpeedMs) {
  if (settingsStore.settings.windUnit == "kmh") {
    snprintf(val, n, "%s %.0f", windDirLabel(deg), windSpeedMs * 3.6f);
    *unit = "km/h";
  } else {
    snprintf(val, n, "%s %.1f", windDirLabel(deg), windSpeedMs);
    *unit = "m/s";
  }
}

// Maly wskaznik kierunku wiatru: pierscien + strzalka obrocona wg stopni
// (0 = polnoc, rosnie zgodnie z ruchem wskazowek zegara). Podpis (kierunek + predkosc) jest w dolnej linii
// ekranu - patrz formatWindLabel() i drawBottomLine().
uint16_t alertColor(uint8_t level);   // definicja nizej
void drawWindIndicator(int cx, int cy, int r, int deg, float windSpeedMs, float gustMs, const ThemeColors &th) {
  // deg = kierunek meteorologiczny (SKAD wieje, np. 270 = z zachodu).
  // Strzalke rysujemy odwrotnie (+180) - DOKAD wieje - tak jak wiekszosc aplikacji
  // pogodowych; podpis (np. "W") nadal mowi, skad wieje.
  float rad = (deg + 180) * PI / 180.0;
  float dx = sin(rad), dy = -cos(rad);

  // ostrzezenie IMGW przed wiatrem na dzis: strzalka w kolorze ostrzezenia (jak napis z wiatrem), inaczej akcent
  uint16_t arrowColor = (todayDay.windAlertLevel > 0) ? alertColor(todayDay.windAlertLevel) : th.accent;

  // trzon (2 px) od srodka do grotu + krotki, szeroki grot; wszystko wygladzone razem z pierscieniem
  const float shaftLen = r - 4;
  const float headLen = (r * 3 + 4) / 8, headWidth = (r + 1) / 3;   // r=24 -> 9 i 8 px, r=29 -> 11 i 10 px
  float tipX = cx + 0.5f + dx * shaftLen, tipY = cy + dy * shaftLen;
  float baseX = tipX - dx * headLen, baseY = tipY - dy * headLen;
  float perpX = -dy, perpY = dx;
  AaShape ring = aaRing(cx, cy, r, 0.5f);                                // pierscien 1 px (jak drawCircle)
  AaShape arrow[2] = {
    aaCapsule(cx + 0.5f, cy, baseX + dx, baseY + dy, 1.0f),            // trzon do nasady grotu
    aaTri(tipX, tipY, baseX + perpX * headWidth / 2, baseY + perpY * headWidth / 2,
          baseX - perpX * headWidth / 2, baseY - perpY * headWidth / 2)
  };
  AaLayer layers[2] = { aaLayer(&ring, 1, th.muted), aaLayer(arrow, 2, arrowColor) };
  aaDraw(layers, 2, th.bg);
}

// Pionowa strzalka trendu cisnienia - trzon + krotki grot, w tym samym
// stylu co wskaznik kierunku wiatru (zamiast ASCII "^"/"v").
void drawTrendArrow(int cx, int cy, int8_t trend, uint16_t color) {
  if (trend == 0) return;
  int dir = (trend > 0) ? -1 : 1; // -1 = koniec strzalki w gore, 1 = w dol
  const int halfLen = 5, headLen = 5, headWidth = 6;

  int tipY = cy + dir * halfLen;
  int tailY = cy - dir * halfLen;

  gfx->drawLine(cx, tailY, cx, tipY, color);
  gfx->drawLine(cx + 1, tailY, cx + 1, tipY, color);

  int baseY = tipY - dir * headLen;
  gfx->fillTriangle(cx, tipY, cx - headWidth / 2, baseY, cx + headWidth / 2, baseY, color);
}

// Kolor ostrzezenia IMGW wg stopnia: 1 zolty, 2 pomaranczowy, 3 czerwony.
uint16_t alertColor(uint8_t level) {
  switch (level) {
    case 1: return rgb565(0xFFD000);
    case 2: return rgb565(0xFF8000);
    default: return rgb565(0xFF3030);
  }
}

// Maly symbol kropli deszczu (7x11 px): trojkat (czubek) + kolo (brzusiec),
// w niebieskim kolorze kropel z ikon deszczu. x = srodek w poziomie,
// y = gorny punkt czubka. Stoi przed procentami opadow w prognozie.
static const uint16_t DROP_COLOR = 0x051D;
void drawRainDrop(int x, int y, uint16_t color, uint16_t bg) {
  AaShape s[2] = { aaTri(x, y, x - 3, y + 6, x + 3, y + 6), aaDisc(x, y + 7, 3.5f) };
  AaLayer l = aaLayer(s, 2, color);
  aaDraw(&l, 1, bg);
}

// Srodek rozy wiatru w poziomie - pod nia wysrodkowany jest podpis wiatru w dolnej linii
static const int kRoseCx = 264;

// Odstep [px] miedzy liczba a jednostka w dolnej linii (hPa, km/h, m/s) i przed znakiem %
static const int kUnitGap = 5;
static const int kPctUnitGap = 3;
static const int kHpaUnitGap = 3;   // cisnienie: "hPa" blizej liczby

// Trojkat 11 px szeroki, 9 px wysoki (wygladzony, symetryczny: "w gore" i "w dol" wygladaja tak samo).
// cx = srodek, topY = gorna krawedz.
void drawArrowTri(int cx, int topY, bool up, uint16_t color, uint16_t bg) {
  // wygladzony; wierzcholki na srodkach pikseli, +0,5 px dookola - jak dotad 11 x 9 px
  if (up) aaFillTriangle(cx, topY, cx - 5, topY + 8, cx + 5, topY + 8, color, bg);
  else    aaFillTriangle(cx - 5, topY, cx + 5, topY, cx, topY + 8, color, bg);
}

// Ikona wschodu / zachodu slonca (pole ok. 29 x 23 px): polowa tarczy nad linia horyzontu,
// 5 promieni, pod horyzontem maly trojkat - w gore = wschod, w dol = zachod.
// Wschod - slonce zolte, zachod - pomaranczowe. x0 = lewa krawedz, hy = linia horyzontu.
void drawSunriseIcon(int x0, int hy, bool sunrise, const ThemeColors &th) {
  const uint16_t sunCol = sunrise ? rgb565(0xFFC800) : rgb565(0xFF8C1A);
  const int cx = x0 + 14, r = 7;
  gfx->fillCircle(cx, hy, r, sunCol);
  gfx->fillRect(x0, hy + 1, 29, r + 1, th.bg);              // dolna polowa tarczy "za horyzontem"
  // promienie (2 px grubosci): pionowy, dwa skosne i dwa poziome
  gfx->fillRect(cx, hy - 13, 2, 4, sunCol);
  gfx->fillRect(cx - 13, hy - 3, 4, 2, sunCol);
  gfx->fillRect(cx + 10, hy - 3, 4, 2, sunCol);
  for (int i = 0; i < 3; i++) {
    gfx->fillRect(cx - 8 - i, hy - 7 - i, 2, 1, sunCol);     // lewy skosny
    gfx->fillRect(cx + 7 + i, hy - 7 - i, 2, 1, sunCol);     // prawy skosny
  }
  gfx->fillRect(x0, hy + 1, 29, 2, th.muted);               // horyzont
  const int ay = hy + 5;                                     // strzalka pod horyzontem (9 x 5 px)
  if (sunrise) gfx->fillTriangle(cx, ay, cx - 4, ay + 4, cx + 4, ay + 4, th.accent);
  else         gfx->fillTriangle(cx - 4, ay, cx + 4, ay, cx, ay + 4, th.accent);
}

int widthWithUnit(const char *val, const char *unit, int gap) {
  return textWidth(val, FONT_SMALL) + gap + textWidth(unit, FONT_SMALL);
}

// Liczba + (odstep) + jednostka, od lewej krawedzi x
void printWithUnit(const char *val, const char *unit, int gap, int x, int topY, uint16_t color, uint16_t bg) {
  printAt(val, x, topY, FONT_SMALL, color, bg);
  printAt(unit, x + textWidth(val, FONT_SMALL) + gap, topY, FONT_SMALL, color, bg);
}

// Dolna linia bloku (nad separatorem y=204):  [kropla] 90%   1024hPa [strzalka]            SW 10km/h
// Po lewej: prawdopodobienstwo opadow dzis, cisnienie z trendem, AQI z kropka; wiatr wysrodkowany pod roza (przy dlugim napisie dosuniety do prawego marginesu).
// Brak prognozy opadow (rainProb < 0) -> kropla i procenty sa pomijane.
void drawBottomLine(const ThemeColors &th, int8_t pressureTrendVal, const char *pVal, const char *windVal, const char *windUnit) {
  AppSettings &cfg = settingsStore.settings;     // przelaczniki z panelu WWW: co pokazywac
  const int kRowY = 180;                 // gorna krawedz tekstu 9 pt
  const int kXL = 14, kXR = LCD_H_RES - 14;
  int padL = 5, padR = 5, aqiGap = 5;   // odstepy miedzy grupami (bez kresek); aqiGap = napis AQI -> kropka

  // szerokosci elementow
  char rainStr[8] = "";   // sama liczba, "%" dochodzi jako jednostka
  bool showRain = cfg.showRain && todayDay.date[0] && todayDay.rainProb >= 0;
  const int kDropW = 7, kDropGap = 4;
  int rainW = 0;
  if (showRain) {
    snprintf(rainStr, sizeof(rainStr), "%d", todayDay.rainProb);
    rainW = kDropW + kDropGap + widthWithUnit(rainStr, "%", kPctUnitGap);
  }
  bool showPress = cfg.showPressure;
  int pW = showPress ? widthWithUnit(pVal, "hPa", kHpaUnitGap) : 0;
  bool showArrow = showPress && (pressureTrendVal != 0);
  const int kArrowW = 17;                // trojkat trendu 11 px + odstep 5 px przed nim i 1 px za
  bool showAqi = cfg.showAqi && (currentAqi > 0);
  int aqiW = textWidth("AQI", FONT_SMALL);
  bool showWind = cfg.showWind;          // kierunek i predkosc wiatru (niezaleznie od rozy / Ksiezyca nad nimi)
  int windW = showWind ? widthWithUnit(windVal, windUnit, kUnitGap) : 0;
  int windX = kXR + kXR;                 // bez wiatru: lewa grupa moze zajac cala linie
  if (showWind) {
    windX = kRoseCx - windW / 2;         // wysrodkowany pod roza, ale nie dalej niz do prawego marginesu
    if (windX + windW > kXR) windX = kXR - windW;
  }

  auto leftWidth = [&](bool arrow, int pl, int pr, int ag) {
    int groups = (showRain ? 1 : 0) + (showPress ? 1 : 0) + (showAqi ? 1 : 0);
    int w = rainW + pW + (arrow ? kArrowW : 0) + (showAqi ? aqiW + ag + 8 : 0);   // 8 = srednica kropki
    return w + (groups > 1 ? (groups - 1) * (pl + pr) : 0);
  };
  // gdy sie nie miesci (lewa grupa nachodzi na wiatr): 1) jeszcze ciasniejsze odstepy, 2) bez strzalki trendu,
  // 3) bez AQI. Nigdy nie ucinamy wiatru ani cisnienia.
  const int kMinGap = 18;               // minimalny odstep lewej grupy od napisu z wiatrem
  auto fits = [&]() { return kXL + leftWidth(showArrow, padL, padR, aqiGap) + kMinGap <= windX; };
  if (!fits()) { padL = 4; padR = 4; aqiGap = 4; }
  if (!fits()) showArrow = false;
  if (!fits()) showAqi = false;

  int x = kXL;
  bool first = true;
  if (showRain) {
    drawRainDrop(x + kDropW / 2, kRowY + 1, todayDay.rainProb > 0 ? DROP_COLOR : th.muted, th.bg);   // 0% = szara, jak slonce
    printWithUnit(rainStr, "%", kPctUnitGap, x + kDropW + kDropGap, kRowY, th.muted, th.bg);
    x += rainW;
    first = false;
  }
  if (showPress) {
    if (!first) x += padL + padR;
    printWithUnit(pVal, "hPa", kHpaUnitGap, x, kRowY, th.muted, th.bg);
    x += pW;
    if (showArrow) {
      drawArrowTri(x + 5 + 5, kRowY + 2, pressureTrendVal > 0, th.accent, th.bg);   // trojkat jak przy temperaturze: w gore = rosnie
      x += kArrowW;
    }
    first = false;
  }
  if (showAqi) {
    if (!first) x += padL + padR;
    printAt("AQI", x, kRowY, FONT_SMALL, th.muted, th.bg);
    drawAqiDot(x + aqiW + aqiGap + 4, kRowY + 6);
  }

  // ostrzezenie IMGW przed wiatrem na dzis (jeszcze obowiazujace) - napis w kolorze ostrzezenia
  uint16_t windCol = (todayDay.windAlertLevel > 0) ? alertColor(todayDay.windAlertLevel) : th.muted;
  if (showWind) printWithUnit(windVal, windUnit, kUnitGap, windX, kRowY, windCol, th.bg);
}

// Trend temperatury: zmiana miedzy biezaca godzina a (do) 3 godzin wczesniej w dzisiejszej serii
// godzinowej (dziala tak samo dla Open-Meteo i MSN). Od 01:00 okno krotsze, przeliczane na 3 h.
// Prog: 1 st.C / 3 h. Zwraca 1 = rosnie, -1 = spada, 0 = stabilnie / brak danych.
static const float TEMP_TREND_THRESHOLD_C = 1.0f;
int8_t temperatureTrend() {
  struct tm ti;
  if (!getLocalTime(&ti, 50)) return 0;
  int h = ti.tm_hour;
  if (h < 1 || h > 23 || !hourlyToday[h].valid) return 0;
  int win = (h < 3) ? h : 3;
  if (!hourlyToday[h - win].valid) return 0;
  float ch = (float)(hourlyToday[h].temp - hourlyToday[h - win].temp) * 3.0f / win;
  return (ch >= TEMP_TREND_THRESHOLD_C) ? 1 : (ch <= -TEMP_TREND_THRESHOLD_C ? -1 : 0);
}

float moonPhaseAt(time_t t);                                           // definicje nizej (ekran "Przeglad dnia")
void drawMoonPhase(int cx, int cy, int r, float p, const ThemeColors &th);

void drawCurrentWeather(const ThemeColors &th) {
  drawBatteryIcon(th);   // obszar rogu mogl zostac zamalowany przy przerysowaniu
  // Czyscimy caly blok miedzy separatorami (y=92 i y=204): promienie slonca w ikonie siegaja do ok. y=97,
  // wiec wczesniejsze czyszczenie od y=100 zostawialo slady starej ikony po zmianie pogody.
  gfx->fillRect(0, 93, LCD_H_RES, 111, th.bg);

  if (!current.valid) {
    printAt(L("Brak danych pogodowych", "No weather data"), 60, 130, FONT_SMALL, th.muted, th.bg);
    return;
  }

  // Geometria bloku (separatory: y=92 i y=204): temperatura, ikona i roza wiatru leza na jednej,
  // wspolnej osi poziomej (kAxisY); pod nimi jedna linia informacyjna (drawBottomLine, y=182).
  // Duze cyfry temperatury (czcionka zegara) maja 34 px wysokosci - stad kTempTop = kAxisY - 17.
  const int kAxisY = 134;          // wspolna os: srodek cyfr temperatury, ikony i rozy wiatru
  const int kTextX = 105;          // lewa krawedz temperatury (ikona jest mniejsza, wiec temperatura blizej niej)
  const int kTempTop = kAxisY - 17;

  // Ikona bitmapowa: srodek chmurki (ikony bez chmury - srodek ramki) na wspolnej osi kAxisY.
  // Ikony maja spojne proporcje (wspolna chmura / slonce / opady - WeatherIconsBmp.h z gen_icons.py), wiec wszystkie
  // rysujemy w tej samej ramce 80 px (natywny rozmiar - ostre krawedzie): chmura ma ok. 58 px, jak roza wiatru.
  // Ramka nie zamalowuje separatorow (93..203).
  drawWeatherIconBmp(gfx, current.icon, 52, kAxisY, 80, th.bg, 93, 203);

  char tempStr[8];
  snprintf(tempStr, sizeof(tempStr), "%.0f", current.temp);

  // Temperatura zawsze w kolorze tekstu - ostrzezenia IMGW jej nie barwia (widac je na ekranie szczegolow dnia
  // i w "Przegladzie dnia"; ostrzezenie przed wiatrem barwi napis z wiatrem).
  uint16_t tempColor = th.text;
  printAt(tempStr, kTextX, kTempTop, FONT_HUGE, tempColor, th.bg);

  // znak stopnia (dwa pierscienie = grubsza kreska, pasuje do pogrubionej czcionki)
  uint16_t tempW = textWidth(tempStr, FONT_HUGE);
  {
    AaShape deg = aaRing(kTextX + tempW + 9, kTempTop + 6, 4.5f, 1.0f);   // jak dwa okregi r=5 i r=4
    AaLayer l = aaLayer(&deg, 1, tempColor);
    aaDraw(&l, 1, th.bg);
  }

  // Trojkacik trendu temperatury pod znakiem stopnia: w gore = rosnie
  int trendW = 0;
  int8_t tTrend = temperatureTrend();
  if (tTrend != 0) {
    int tx = kTextX + tempW + 9 - 5;       // pod znakiem stopnia, wysrodkowany wzgledem kolka
    int ty = kTempTop + 26;               // dolna krawedz trojkata na linii dolnej krawedzi cyfr (35 px)
    drawArrowTri(tx + 5, ty, tTrend > 0, th.accent, th.bg);
    trendW = 0;   // trojkat jest pod kolkiem - uklad min/max bez zmian
  }

  // Dzisiejsze maksimum i minimum (jedno pod drugim, ciemnoszare) z prawej strony temperatury
  // biezacej. Zakres obejmuje zawsze takze temperature biezaca - "jaka moze wystapic w ciagu dnia".
  // Dwie linie 9 pt (po 13 px, z odstepem 9 px) zajmuja cala wysokosc duzych cyfr (35 px).
  if (todayDay.date[0] && todayDay.tempMax > -900 && todayDay.tempMin < 900) {
    int hi = (int)lroundf(fmaxf(todayDay.tempMax, current.temp));
    int lo = (int)lroundf(fminf(todayDay.tempMin, current.temp));
    char hiStr[8], loStr[8];
    snprintf(hiStr, sizeof(hiStr), "%d", hi);
    snprintf(loStr, sizeof(loStr), "%d", lo);
    // Cienki pionowy separator miedzy temperatura biezaca a min/max; ma wysokosc cyfr (35 px), a max/min
    // zaczynaja i koncza sie dokladnie na gornej i dolnej krawedzi duzych cyfr.
    int degRight = kTextX + tempW + 9 + 5 + trendW;
    int sepX = degRight + 6;
    gfx->drawFastVLine(sepX, kTempTop, 35, lerpColor565(th.bg, th.muted, 0.55f));
    int xr = sepX + 7;
    // Zapis "jak w matematyce": liczby wyrownane do PRAWEJ - jednosci pod jednosciami, dziesiatki pod
    // dziesiatkami. Minus "wisi" po lewej stronie cyfr; najszersza liczba (z minusem) zaczyna sie w xr,
    // wiec minus nie dotyka znaku stopnia.
    int hiW = textWidth(hiStr, FONT_SMALL), loW = textWidth(loStr, FONT_SMALL);
    int right = xr + max(hiW, loW);          // wspolna prawa krawedz obu liczb
    printAt(hiStr, right - hiW, kTempTop, FONT_SMALL, th.muted, th.bg);
    printAt(loStr, right - loW, kTempTop + 22, FONT_SMALL, th.muted, th.bg);
  }

  // prawa strona bloku: faza Ksiezyca (r = 29, jak roza wiatru - 58 px) albo roza wiatru - wybor w panelu WWW.
  // Przelacznik showWind dotyczy tylko kierunku i predkosci wiatru w dolnej linii - tu nie ma wplywu.
  if (settingsStore.settings.rightWidget == "moon") {
    time_t nowT = time(nullptr);
    if (nowT > 1700000000) drawMoonPhase(kRoseCx, kAxisY, 29, moonPhaseAt(nowT), th);
  } else {
    drawWindIndicator(kRoseCx, kAxisY, 29, current.windDeg, current.windSpeed, current.gustMs, th);
  }

  // cisnienie i jego trend
  char pStr[16];
  int8_t pressureTrendVal;
  if (bmeReady) {
    pressureTrendVal = pressureTrend();   // wlasna historia - nadpisywana ponizej trendem z API, jesli jest
    snprintf(pStr, sizeof(pStr), "%.0f", localPressure);
  } else {
    // brak lokalnego czujnika - trend liczony z wlasnej historii odczytow z API
    pressureTrendVal = apiPressureTrend();
    snprintf(pStr, sizeof(pStr), "%.0f", current.pressure);
  }
  // Trend z godzinowej serii API (okno 3 h) ma pierwszenstwo: dziala od razu po starcie,
  // a wlasna historia (ok. 50 min) rzadko kiedy przekracza prog. Wlasna historia zostaje
  // jako zapas (np. pierwsza godzina doby albo model bez cisnienia).
  if (current.hasPressTrend) pressureTrendVal = current.pressTrend;

  char windVal[16];
  const char *windUnit;
  formatWindLabel(windVal, sizeof(windVal), &windUnit, current.windDeg, current.windSpeed);
  drawBottomLine(th, pressureTrendVal, pStr, windVal, windUnit);
}

// Zakres temperatur do kolumny prognozy: zawsze z ukosnikiem "min/max", np. "9/19", "-5/-2", "-3/5";
// gdy po zaokragleniu min == max, pokazuje jedna liczbe. Liczby zaokraglane do calkowitych (bez "-0").
// Inny separator: zmien "%d/%d" w ostatniej linii funkcji (np. "%d-%d" - wtedy przy ujemnych wyjdzie "-5--2").
void formatTempRange(char *buf, size_t n, float tMin, float tMax) {
  int lo = (int)lroundf(tMin), hi = (int)lroundf(tMax);
  if (lo > hi) {
    int t = lo;
    lo = hi;
    hi = t;
  }
  if (lo == hi) snprintf(buf, n, "%d", lo);
  else snprintf(buf, n, "%d/%d", lo, hi);
}

// Male slonce (tarcza + 8 krotkich promieni, ok. 15 px) - godziny slonca w kolumnie prognozy.
void drawSmallSun(int cx, int cy, uint16_t color, uint16_t bg) {
  AaShape s[9];
  s[0] = aaDisc(cx, cy, 3.5f);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4.0f;
    s[i + 1] = aaCapsule(cx + cosf(a) * 5.2f, cy + sinf(a) * 5.2f, cx + cosf(a) * 7.0f, cy + sinf(a) * 7.0f, 0.7f);
  }
  AaLayer l = aaLayer(s, 9, color);
  aaDraw(&l, 1, bg);
}

// Ikony w kolumnach prognozy: wspolna ramka FORECAST_ICON_PX dla wszystkich (proporcje sa w samych bitmapach).
// Os kolumny: y=275.
static const int FORECAST_ICON_PX = 68;

void drawForecast(const ThemeColors &th) {
  gfx->fillRect(0, 214, LCD_H_RES, LCD_V_RES - 214, th.bg);
  if (forecastCount == 0) return;

  // Z linia godzin slonca temperatura i opady sa podniesione, zeby trzecia linia zmiescila sie nad napisem komfortu.
  const bool showSun = settingsStore.settings.showSunHours;
  const int tempY = showSun ? 318 : 325;
  const int rainY = showSun ? 342 : 350;      // gorna krawedz tekstu procentow; kropelka 1 px nizej
  const int sunY  = 366;
  int colWidth = LCD_H_RES / forecastCount;
  for (uint8_t i = 0; i < forecastCount; i++) {
    int cx = colWidth * i + colWidth / 2;

    uint16_t lw = textWidth(forecast[i].label.c_str(), FONT_MED);
    printAt(forecast[i].label.c_str(), cx - lw / 2, 218, FONT_MED, th.text, th.bg);

    // ramka nie moze zamalowac nazwy dnia (do y~231) ani temperatury pod ikona
    drawWeatherIconBmp(gfx, forecast[i].icon, cx, 275, FORECAST_ICON_PX, th.bg, 233, tempY - 2);

    // Linia 1: zakres temperatur "min/max" (najpierw minimum, potem maksimum), np. "9/19".
    // Separator to zawsze ukosnik ("9/19", "-5/-2", "-3/5").
    // Cala linia w jednym kolorze: bialy, a przy ostrzezeniu IMGW na ten dzien - kolor ostrzezenia.
    char tempLine[24];
    formatTempRange(tempLine, sizeof(tempLine), forecast[i].tempMin, forecast[i].tempMax);
    uint16_t cTemp = th.text;
    int tx = cx - textWidth(tempLine, FONT_MED) / 2;
    printAt(tempLine, tx, tempY, FONT_MED, cTemp, th.bg);

    // Linia 2: prawdopodobienstwo opadow ("--%" gdy model go nie podaje)
    char rainStr[8];
    if (forecast[i].rainProb >= 0) snprintf(rainStr, sizeof(rainStr), "%d%%", forecast[i].rainProb);
    else snprintf(rainStr, sizeof(rainStr), "--%%");
    uint16_t rainW = textWidth(rainStr, FONT_SMALL);
    const int kDropW = 7, kDropGap = 6;   // odstep kropelki od procentow
    int rx = cx - (kDropW + kDropGap + rainW) / 2;
    // kropelka niebieska, gdy jest prognoza; szara, gdy brak danych ("--%")
    drawRainDrop(rx + kDropW / 2, rainY + 1, forecast[i].rainProb > 0 ? DROP_COLOR : th.muted, th.bg);   // 0% / brak danych = szara
    printAt(rainStr, rx + kDropW + kDropGap, rainY, FONT_SMALL, th.muted, th.bg);

    // Linia 3 (opcjonalna): godziny ze sloncem w dobie, np. "6 h" ("-- h" gdy model nie podaje)
    if (showSun) {
      char sunVal[8];
      if (forecast[i].sunHours >= 0) snprintf(sunVal, sizeof(sunVal), "%d", forecast[i].sunHours);
      else snprintf(sunVal, sizeof(sunVal), "--");
      const int kSunW = 15, kSunGap = 5, kHGap = kPctUnitGap;   // kHGap: odstep miedzy liczba a "h"
      int sw = widthWithUnit(sunVal, "h", kHGap);
      int sx = cx - (kSunW + kSunGap + sw) / 2;
      drawSmallSun(sx + kSunW / 2, sunY + 6, forecast[i].sunHours > 0 ? 0xFE60 : th.muted, th.bg);   // szare slonce przy 0 h i braku danych
      printWithUnit(sunVal, "h", kHGap, sx + kSunW + kSunGap, sunY, th.muted, th.bg);
    }

    if (i > 0) {
      // pionowy separator kolumn: do linii z procentami opadow albo (z godzinami slonca) do linii slonca
      gfx->fillRect(colWidth * i - 1, 214, 2, showSun ? 170 : 152, th.divider);
    }
  }
}

// ---------------------------------------------------------------------
//  Ekran szczegolow dnia (po dotknieciu kolumny prognozy): nazwa dnia,
//  podsumowanie i godzinowa tabela - godzina, ikona, temperatura, opady, wiatr.
// ---------------------------------------------------------------------
static const int DETAIL_FIRST_HOUR = 6;                  // pierwsza pokazywana godzina
static const int DETAIL_LAST_HOUR = 23;                  // ostatnia pokazywana godzina
static const unsigned long DETAIL_TIMEOUT_MS = 30000;    // samoczynny powrot na ekran glowny
// "Dzisiaj": tabela od BIEZACEJ godziny do jutra TODAY_END_HOUR:00 (wlacznie), ale nie wiecej
// niz TODAY_MAX_ROWS wierszy (np. o 22:00 -> 22..06, w poludnie -> kolejne 18 godzin)
static const int TODAY_END_HOUR = 6;
static const int TODAY_MAX_ROWS = 18;

void drawDayDetail(const ThemeColors &th, uint8_t d) {
  gfx->fillRect(0, 0, LCD_H_RES, LCD_V_RES, th.bg);
  bool isToday = (d == DAY_TODAY);
  if (isToday ? todayDay.date[0] == 0 : d >= forecastCount) return;
  const ForecastDay &fd = isToday ? todayDay : forecast[d];
  const HourPoint *hours = isToday ? hourlyToday : hourly[d];
  bool kmh = settingsStore.settings.windUnit == "kmh";

  // dzisiaj: podswietlamy wiersz biezacej godziny
  int nowHour = -1;
  if (isToday) {
    struct tm ti;
    if (getLocalTime(&ti, 50)) nowHour = ti.tm_hour;
  }

  // wypisuje tekst wysrodkowany na pionowej osi x (naglowek i wartosci kolumny
  // dziela te sama os, wiec nie zlewaja sie z sasiednimi kolumnami)
  auto centered = [&](const char *t, int axisX, int y, const GFXfont *f, uint16_t col, uint16_t bg) {
    printAt(t, axisX - textWidth(t, f) / 2, y, f, col, bg);
  };

  // tytul wielkimi literami, wysrodkowany w poziomie: nazwa dnia (np. WTOREK),
  // a dla dzisiejszego dnia po prostu DZISIAJ
  char title[16];
  strncpy(title, isToday ? L("Dzisiaj", "Today") : dayNameFull(fd.wday), sizeof(title) - 1);
  title[sizeof(title) - 1] = 0;
  uppercasePL(title, sizeof(title));
  centered(title, LCD_H_RES / 2, 10, FONT_DAY_TITLE, th.accent, th.bg);

  // podsumowanie dnia (data, max/min, najwieksze prawdopodobienstwo opadow), tez wysrodkowane
  int mon = 0, day = 0;
  sscanf(fd.date, "%*d-%d-%d", &mon, &day);
  char sub[64];
  int len = langEn() && mon >= 1 && mon <= 12
                ? snprintf(sub, sizeof(sub), "%s %d   max %.0f   min %.0f", MONTH_SHORT_EN[mon - 1], day, fd.tempMax, fd.tempMin)
                : snprintf(sub, sizeof(sub), "%d.%02d   max %.0f   min %.0f", day, mon, fd.tempMax, fd.tempMin);
  if (fd.rainProb >= 0 && len > 0 && len < (int)sizeof(sub))
    snprintf(sub + len, sizeof(sub) - len, "   %s %d%%", L("opady", "rain"), fd.rainProb);
  centered(sub, LCD_H_RES / 2, 42, FONT_SMALL, th.muted, th.bg);

  // Bez linii z nazwa ostrzezenia IMGW - ostrzezenie przed wiatrem widac po kolorze kolumny wiatru w godzinach
  // jego obowiazywania (pozostale ostrzezenia: "Przeglad dnia"). dy zostaje jako miejsce na ewentualna linie.
  const int dy = 0;

  gfx->drawFastHLine(14, 66 + dy, LCD_H_RES - 28, th.divider);

  // kolumny: godzina (do lewej) i ikona, a temp / opady / wiatr wysrodkowane na swoich osiach
  const int xTime = 10, xIcon = 76, cTemp = 116, cRain = 178, cWind = 262;
  printAt(L("godz.", "time"), xTime, 70 + dy, FONT_SMALL, th.muted, th.bg);
  centered("temp.", cTemp, 70 + dy, FONT_SMALL, th.muted, th.bg);
  centered(L("opady", "rain"), cRain, 70 + dy, FONT_SMALL, th.muted, th.bg);
  centered(kmh ? L("wiatr km/h", "wind km/h") : L("wiatr m/s", "wind m/s"), cWind, 70 + dy, FONT_SMALL, th.muted, th.bg);
  gfx->drawFastHLine(14, 87 + dy, LCD_H_RES - 28, th.divider);

  // wiersze godzinowe - wysokosc wiersza liczona z liczby godzin, wiec zmiana
  // DETAIL_FIRST_HOUR / DETAIL_LAST_HOUR sama dopasowuje uklad
  const int top = 91 + dy, bottom = LCD_V_RES - 6;
  // zakres godzin: inne dni - stale DETAIL_FIRST..LAST_HOUR; dzisiaj - od biezacej
  // godziny do jutra TODAY_END_HOUR (max TODAY_MAX_ROWS wierszy), godziny > 23 = jutro
  bool rolling = isToday && nowHour >= 0;
  int firstHour = rolling ? nowHour : DETAIL_FIRST_HOUR;
  int rows = rolling ? (24 - nowHour) + TODAY_END_HOUR + 1 : DETAIL_LAST_HOUR - DETAIL_FIRST_HOUR + 1;
  if (rolling && rows > TODAY_MAX_ROWS) rows = TODAY_MAX_ROWS;
  int rowH = (bottom - top) / rows;
  if (rowH > 30) rowH = 30;       // przy malej liczbie wierszy nie rozciagamy ich w nieskonczonosc
  int iconR = rowH / 3 - 1;
  if (iconR < 4) iconR = 4;
  for (int i = 0; i < rows; i++) {
    int hAbs = firstHour + i;                   // >= 24 oznacza jutro
    bool nextDay = rolling && hAbs >= 24;
    int h = nextDay ? hAbs - 24 : hAbs;
    int y0 = top + i * rowH;
    if (nextDay && hAbs == 24 && i > 0)         // przejscie przez polnoc - cienka kreska
      gfx->drawFastHLine(14, y0, LCD_H_RES - 28, th.muted);
    int ty = y0 + (rowH - 12) / 2;   // gorna krawedz tekstu (cyfry maja ok. 12 px)
    int cy = y0 + rowH / 2;
    char buf[24];

    // biezaca godzina: tlo wiersza w kolorze separatora; wszystko w tym wierszu
    // (tekst, "wygryzienie" ksiezyca) rysujemy z tym tlem, zeby nie bylo czarnych plam
    bool isNowRow = (rolling && i == 0);   // pierwszy wiersz = biezaca godzina
    uint16_t rowBg = isNowRow ? th.divider : th.bg;
    if (isNowRow) gfx->fillRect(8, y0, LCD_H_RES - 16, rowH, rowBg);
    ThemeColors rowTh = th;
    rowTh.bg = rowBg;

    // godziny z jutra (po polnocy) w kolorze akcentu, zeby odroznic je od dzisiejszych
    snprintf(buf, sizeof(buf), "%02d:00", h);
    printAt(buf, xTime, ty, FONT_SMALL, nextDay ? th.accent : th.text, rowBg);

    // dane godziny: dzisiejsze albo (po polnocy) z pierwszego dnia prognozy = jutro
    const HourPoint *dayHours = nextDay ? (forecastCount > 0 ? hourly[0] : nullptr) : hours;
    if (!dayHours || !dayHours[h].valid) {   // model nie podal danych na te godzine
      centered("--", cTemp, ty, FONT_SMALL, th.muted, rowBg);
      continue;
    }
    const HourPoint &hp = dayHours[h];
    drawWeatherIconBmp(gfx, wmoToIcon(hp.code, hp.isDay), xIcon, cy, min(iconR * 3 + 6, rowH), rowBg);   // bitmapa nie moze wyjsc poza wiersz (ma tlo wiersza)

    snprintf(buf, sizeof(buf), "%d", hp.temp);
    centered(buf, cTemp, ty, FONT_SMALL, th.text, rowBg);

    // opady: kropelka na STALYM miejscu, procenty zaraz za nia (do lewej) - dzieki
    // temu kropelki ukladaja sie w prosta kolumne niezaleznie od liczby cyfr
    char rb[8];
    if (hp.rain >= 0) snprintf(rb, sizeof(rb), "%d%%", hp.rain);
    else snprintf(rb, sizeof(rb), "--");
    drawRainDrop(cRain - 20, cy - 5, hp.rain > 0 ? DROP_COLOR : th.muted, rowBg);   // 0% / brak danych = szara
    printAt(rb, cRain - 12, ty, FONT_SMALL, th.muted, rowBg);

    if (kmh) snprintf(buf, sizeof(buf), "%s %.0f", windDirLabel(hp.windDir), hp.windMs * 3.6f);
    else snprintf(buf, sizeof(buf), "%s %.1f", windDirLabel(hp.windDir), hp.windMs);
    // godzina objeta ostrzezeniem IMGW przed wiatrem - kierunek i predkosc w kolorze ostrzezenia
    uint8_t wl = windWarnAt(nextDay ? forecast[0].date : fd.date, h, &hp);
    centered(buf, cWind, ty, FONT_SMALL, wl ? alertColor(wl) : th.muted, rowBg);
  }
}

// ---------------------------------------------------------------------
//  Pasek komfortu pogodowego — kolorowy gradient zamiast "buziek",
//  oparty o temperature odczuwalna (apparent_temperature) z Open-Meteo.
// ---------------------------------------------------------------------
struct ComfortStop { float temp; uint16_t color; };
static const ComfortStop COMFORT_STOPS[] = {
  { -10, rgb565(0x2A4FB0) }, // zimno - ciemny niebieski
  {   0, rgb565(0x3D82DC) }, // niebieski
  {  10, rgb565(0x4FC8C8) }, // turkusowy
  {  20, rgb565(0x5AC85A) }, // zielony (komfortowo)
  {  30, rgb565(0xE6C83C) }, // zolty
  {  40, rgb565(0xDC4628) }, // czerwony - upal
};
static const uint8_t COMFORT_STOP_COUNT = 6;

uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t) {
  uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  uint8_t r = r1 + (uint8_t)((r2 - r1) * t);
  uint8_t g = g1 + (uint8_t)((g2 - g1) * t);
  uint8_t b = b1 + (uint8_t)((b2 - b1) * t);
  return (r << 11) | (g << 5) | b;
}

// Zwraca zestaw kolorow. Jedna stala paleta (motywy kolorystyczne zostaly usuniete).
ThemeColors currentTheme() {
  return getThemeColors();
}

uint16_t comfortColorForTemp(float t) {
  if (t <= COMFORT_STOPS[0].temp) return COMFORT_STOPS[0].color;
  if (t >= COMFORT_STOPS[COMFORT_STOP_COUNT - 1].temp) return COMFORT_STOPS[COMFORT_STOP_COUNT - 1].color;
  for (uint8_t i = 0; i < COMFORT_STOP_COUNT - 1; i++) {
    if (t >= COMFORT_STOPS[i].temp && t <= COMFORT_STOPS[i + 1].temp) {
      float frac = (t - COMFORT_STOPS[i].temp) / (COMFORT_STOPS[i + 1].temp - COMFORT_STOPS[i].temp);
      return lerpColor565(COMFORT_STOPS[i].color, COMFORT_STOPS[i + 1].color, frac);
    }
  }
  return COMFORT_STOPS[COMFORT_STOP_COUNT - 1].color;
}

// Etykieta komfortu. Temperature zaokraglamy do pelnego stopnia, a potem stosujemy
// przedzialy calkowite: Mrozno (do -1), Zimno (0-10), Chlodno (11-17),
// Komfortowo (18-24), Cieplo (25-30), Upalnie (31 i wiecej). Napisy z polskimi
// znakami (UTF-8) - rysowane przez printCentered/printAt z czcionka FONT_SMALL.
const char *comfortLabel(float t) {
  int r = (int)lroundf(t);
  if (r <= -1) return L("Mroźno", "Freezing");
  if (r <= 10) return L("Zimno", "Cold");
  if (r <= 17) return L("Chłodno", "Cool");
  if (r <= 24) return L("Komfortowo", "Comfortable");
  if (r <= 30) return L("Ciepło", "Warm");
  return L("Upalnie", "Hot");
}

// Temperatury z internetu (current, hourly, forecast) przychodza w jednostce wybranej w panelu
// (C albo F). Progi komfortu, mrozu i gololedzi sa w stopniach Celsjusza - przed porownaniem
// zamieniamy temperature na C. Czujnik BME280 zawsze podaje C.
bool tempIsF() { return settingsStore.settings.units == "imperial"; }
float toC(float t) { return tempIsF() ? (t - 32.0f) * 5.0f / 9.0f : t; }

// Humidex (uproszczony, wzor Environment Canada) — temperatura odczuwalna
// uwzgledniajaca wilgotnosc. W chlodne/suche dni praktycznie = temperatura
// rzeczywista, roznica rosnie przy cieple + wilgoci.
float computeHumidex(float tempC, float rh) {
  float e = (rh / 100.0) * 6.112 * expf((17.62 * tempC) / (243.12 + tempC));
  float h = tempC + 0.5555 * (e - 10.0);
  // Humidex opisuje PARNOSC (wilgotnosc podnosi odczuwalna temperature). W suchym
  // powietrzu (e < 10 hPa, np. ogrzewany pokoj) wzor daje jednak wartosc NIZSZA od
  // rzeczywistej temperatury (19 C przy 30% wilgotnosci -> 17 C), co zanizalo etykiete
  // komfortu. Dlatego wilgotnosc moze tylko podnosic odczuwalna temperature.
  return (h > tempC) ? h : tempC;
}

// Drobna korekta za cisnienie — to uproszczona heurystyka, nie standardowy
// wskaznik: nizsze cisnienie czesto towarzyszy niestabilnej/duszej pogodzie,
// wiec lekko przesuwamy pasek w strone "mniej komfortowo". Wysokie cisnienie
// celowo nie daje bonusu, zeby nie zmyslac efektu w druga strone.
// Wiatr oziebia (wind chill) - standardowy wskaznik (Environment Canada / NWS), wazny
// tylko dla T <= 10 C i wiatru >= 4.8 km/h. Zwraca temperature odczuwalna [C].
float windChill(float tempC, float windKmh) {
  float v = powf(windKmh, 0.16f);
  return 13.12f + 0.6215f * tempC - 11.37f * v + 0.3965f * tempC * v;
}

float pressureAdjustment(float hpa) {
  if (hpa > 0 && hpa < 1000) return -1.5;
  return 0.0;
}

// Drobna korekta za slonce — kolejna uproszczona heurystyka: bezposrednie
// slonce na skorze grzeje bardziej niz pokazuje sam termometr, a humidex
// tego nie uwzglednia (liczy tylko temperature+wilgotnosc). Dzialamy na
// podstawie kodu ikony (mapowanego z kodow WMO): pelne slonce (01) wiekszy bonus,
// male zachmurzenie/odrobina slonca (02) mniejszy, tylko w dzien ('d').
// Pochmurno/noc - brak bonusu.
float sunAdjustment(const String &icon) {
  if (icon.length() < 3 || icon.charAt(2) != 'd') return 0.0;
  String group = icon.substring(0, 2);
  if (group == "01") return 2.0;
  if (group == "02") return 1.0;
  return 0.0;
}

// ---------------------------------------------------------------------
//  Wskazowka nad paskiem komfortu: "<etykieta komfortu> - <zjawisko>", np. "Chłodno - możliwy deszcz".
//  Zjawiska grozne (grad, burza, gołoledź, śnieg, deszcz ze śniegiem, oblodzenie, silny wiatr) - SAM pomaranczowy
//  napis (bez etykiety), z wyprzedzeniem do 12 h, a wieczorem do rana; zwykle (deszcz, mgła) - najblizsze 4 h.
//  Szczegoly przy buildWeatherHint(). Gdy nic takiego - zachmurzenie wg aktualnej ikony: słonecznie (w dzien) / bezchmurnie (w nocy),
//  częściowe zachmurzenie, przejaśnienia (tylko MSN; w nocy częściowe zachmurzenie), pochmurno. Inne ikony bez wskazowki - sama etykieta komfortu.
// ---------------------------------------------------------------------
// Rodzaj zjawiska wg kodu pogody WMO (godzinowka Open-Meteo albo kody przeliczone z MSN).
enum HintKind : uint8_t { HK_NONE = 0, HK_RAIN, HK_SHOWER, HK_SLEET, HK_SNOW, HK_STORM, HK_HAIL };
uint8_t hintKindOf(uint8_t c) {   // zwraca HintKind (uint8_t - bezpieczne dla prototypow Arduino)
  if (c == 96 || c == 99) return HK_HAIL;                       // burza z gradem
  if (c == 95) return HK_STORM;                                 // burza
  if (c == 71 || c == 73 || c == 75 || c == 77 || c == 85 || c == 86) return HK_SNOW;   // snieg, przelotny snieg
  if (c == 56 || c == 57 || c == 66 || c == 67) return HK_SLEET; // marznaca mzawka/deszcz, deszcz ze sniegiem
  if (c == 80 || c == 81 || c == 82) return HK_SHOWER;          // przelotny deszcz
  if ((c >= 51 && c <= 55) || (c >= 61 && c <= 65)) return HK_RAIN;   // mzawka, deszcz
  return HK_NONE;
}

// Wpisuje do buf tekst wskazowki; false = brak wskazowki. hourNow = biezaca godzina (0-23).
// hintWindLevel: stopien ostrzezenia przed wiatrem, gdy wskazowka to wiatr (0 = brak / inna wskazowka).
// hintHazard: wskazowka to zjawisko grozne (grad, burza, golodedz, oblodzenie, snieg, deszcz ze sniegiem albo
// ostrzezenie o wietrze) - drawComfortText pokazuje ja wtedy SAMA (bez etykiety komfortu) w kolorze hintColor.
uint8_t hintWindLevel = 0;
bool hintHazard = false;
uint16_t hintColor = 0xFFFF;

// Dwa okna czasowe:
//  - KROTKIE (HINT_HORIZON = 4 h od biezacej godziny) - zwykle zjawiska: deszczowo / możliwy deszcz /
//    przelotne opady, możliwa mgła, silny wiatr, a bez nich zachmurzenie wg aktualnej ikony.
//  - DLUGIE dla zjawisk groznych: 12 h, a wieczorem (od HAZARD_EVENING_H) az do HAZARD_MORNING_H nastepnego dnia -
//    zeby wieczorem bylo widac poranna golodedz. Jesli zjawisko zaczyna sie poza krotkim oknem, napis dostaje
//    godzine poczatku, np. "Ryzyko gołoledzi od 5:00".
// Zjawiska grozne (od najwazniejszego): możliwy grad > możliwa burza > ryzyko gołoledzi (marznacy deszcz/mzawka albo
// deszcz przy temperaturze <= 1 st.) > opady śniegu > deszcz ze śniegiem > ryzyko oblodzenia (wczesniej mokro - deszcz
// w ostatnich 3 h albo prognozowany - a potem temperatura <= 0 st.: mokre drogi zamarzna, choc juz nie pada).
// Liczy sie tylko godzina z szansa opadow >= HINT_MIN_PROB % (albo bez danych o szansie).
// "deszczowo" = deszcz w co najmniej polowie z najblizszych 12 godzin.
static const int HINT_HORIZON = 4;          // krotkie okno [h]
static const int HINT_MIN_PROB = 40;
static const int HINT_RAINY_HOURS = 12;
static const int HAZARD_HORIZON = 12;       // dlugie okno w ciagu dnia [h]
static const int HAZARD_EVENING_H = 18;     // od tej godziny dlugie okno siega do rana
static const int HAZARD_MORNING_H = 10;     // ... do tej godziny nastepnego dnia (wylacznie)
static const int ICING_PAST_HOURS = 3;      // ile godzin wstecz szukac deszczu przy ryzyku oblodzenia

enum HazardKind : uint8_t { HZ_NONE = 0, HZ_ICING, HZ_SLEET, HZ_SNOW, HZ_ICE, HZ_STORM, HZ_HAIL };   // rosnaco wg waznosci

bool buildWeatherHint(int hourNow, char *buf, size_t n) {
  buf[0] = 0;
  hintWindLevel = 0;
  hintHazard = false;
  hintColor = 0xFFFF;
  if (hourNow < 0 || hourNow > 23) hourNow = 0;
  String g0 = current.icon.substring(0, 2);
  auto hourAt = [&](int k) -> const HourPoint * {   // k godzin od teraz (dzis, potem jutro); k < 0 - wczesniej dzis
    int hh = hourNow + k;
    if (hh < 0) return nullptr;
    if (hh < 24) return &hourlyToday[hh];
    if (forecastCount > 0 && hh < 48) return &hourly[0][hh - 24];
    return nullptr;
  };
  auto likely = [](const HourPoint *hp) { return hp->rain < 0 || hp->rain >= HINT_MIN_PROB; };
  auto isWet = [](uint8_t kd) { return kd == HK_RAIN || kd == HK_SHOWER || kd == HK_SLEET; };

  // ===== 1. zjawiska grozne - dlugie okno =====
  int hz = HAZARD_HORIZON;
  if (hourNow >= HAZARD_EVENING_H) hz = (24 - hourNow) + HAZARD_MORNING_H;   // wieczorem: do rana
  uint8_t best = HZ_NONE;
  int bestK = 0;
  auto take = [&](uint8_t kind, int k) { if (kind > best) { best = kind; bestK = k < 0 ? 0 : k; } };
  bool wetBefore = false;                         // czy wczesniej (od ICING_PAST_HOURS wstecz) bylo/bedzie mokro
  for (int k = -ICING_PAST_HOURS; k < hz; k++) {
    const HourPoint *hp = hourAt(k);
    if (!hp || !hp->valid || hp->code == 255) continue;
    uint8_t kd = hintKindOf(hp->code);
    bool lk = likely(hp);
    if (k >= 0 && lk) {
      bool freezing = (hp->code == 56 || hp->code == 57 || hp->code == 66 || hp->code == 67);
      if (kd == HK_HAIL) take(HZ_HAIL, k);
      else if (kd == HK_STORM) take(HZ_STORM, k);
      else if (freezing || (isWet(kd) && toC(hp->temp) <= 1)) take(HZ_ICE, k);   // deszcz na zmarznietym / przy zerze
      else if (kd == HK_SNOW) take(HZ_SNOW, k);
      else if (kd == HK_SLEET) take(HZ_SLEET, k);
    }
    // oblodzenie: mokro (deszcz) a potem mroz - liczymy tez godziny miniona (ostatnie ICING_PAST_HOURS)
    if (k >= 0 && wetBefore && toC(hp->temp) <= 0) take(HZ_ICING, k);
    if (lk && isWet(kd)) wetBefore = true;
  }
  if (best != HZ_NONE) {
    const char *name = "";
    switch (best) {
      case HZ_HAIL:  name = L("Możliwy grad", "Possible hail"); break;
      case HZ_STORM: name = L("Możliwa burza", "Possible storm"); break;
      case HZ_ICE:   name = L("Ryzyko gołoledzi", "Black ice risk"); break;
      case HZ_SNOW:  name = L("Opady śniegu", "Snow"); break;
      case HZ_SLEET: name = L("Deszcz ze śniegiem", "Sleet"); break;
      default:       name = L("Ryzyko oblodzenia", "Icy roads risk"); break;
    }
    if (bestK >= HINT_HORIZON) {                  // dalej niz "za chwile" - z godzina poczatku
      int hh = (hourNow + bestK) % 24;
      char when[12];
      if (settingsStore.settings.use24h) snprintf(when, sizeof(when), "%d:00", hh);
      else snprintf(when, sizeof(when), "%d %s", hh % 12 == 0 ? 12 : hh % 12, hh < 12 ? "AM" : "PM");
      snprintf(buf, n, "%s %s %s", name, L("od", "from"), when);
    } else {
      snprintf(buf, n, "%s", name);
    }
    hintHazard = true;
    hintColor = rgb565(0xFF8C1A);                 // pomaranczowy - jak przypomnienie o wywozie dzien wczesniej
    return true;
  }

  // ===== 2. zwykle zjawiska - krotkie okno =====
  bool rain = false, steady = false;
  bool fog = (g0 == "50");
  for (int k = 0; k < HINT_HORIZON; k++) {
    const HourPoint *hp = hourAt(k);
    if (!hp) break;
    if (!hp->valid || hp->code == 255) continue;
    if (hp->code == 45 || hp->code == 48) fog = true;
    if (!likely(hp)) continue;
    uint8_t kd = hintKindOf(hp->code);
    if (kd == HK_RAIN || kd == HK_SHOWER) rain = true;
    if (kd == HK_RAIN) steady = true;
  }
  int rainyHours = 0;                             // "deszczowo": deszcz w co najmniej polowie z najblizszych 12 h
  for (int k = 0; k < HINT_RAINY_HOURS; k++) {
    const HourPoint *hp = hourAt(k);
    if (!hp) break;
    if (!hp->valid || hp->code == 255) continue;
    uint8_t kd = hintKindOf(hp->code);
    if ((kd == HK_RAIN || kd == HK_SHOWER) && likely(hp)) rainyHours++;
  }

  const char *txt = nullptr;
  if (rain) {
    if (rainyHours >= HINT_RAINY_HOURS / 2) txt = L("deszczowo", "rainy");
    else if (steady) txt = L("możliwy deszcz", "possible rain");
    else txt = L("przelotne opady", "showers");
  }
  if (!txt) {
    // bez opadow: mgla, wiatr (IMGW albo z prognozy - jak w godzinowce), zachmurzenie
    uint8_t windLvl = 0;
    for (int k = 0; k < HINT_HORIZON; k++) {
      int hh = hourNow + k;
      const char *d = (hh < 24) ? todayDay.date : (forecastCount > 0 ? forecast[0].date : "");
      uint8_t l = windWarnAt(d, hh % 24, hourAt(k));
      if (l > windLvl) windLvl = l;
    }
    if (fog) txt = L("możliwa mgła", "possible fog");
    else if (windLvl) {
      // ostrzezenie o wietrze - jak zjawisko grozne: sam napis w kolorze stopnia ostrzezenia
      hintWindLevel = windLvl;
      hintHazard = true;
      hintColor = alertColor(windLvl);
      snprintf(buf, n, "%s", L("Silny wiatr", "Strong wind"));
      return true;
    } else if (g0 == "01") {
      // zwykly opis zachmurzenia; w dzien (ikona "01d") "słonecznie", w nocy "bezchmurnie"
      bool day = current.icon.length() >= 3 && current.icon.charAt(2) == 'd';
      txt = day ? L("słonecznie", "sunny") : L("bezchmurnie", "clear");
    } else if (g0 == "02") {
      txt = L("częściowe zachmurzenie", "partly cloudy");
    } else if (g0 == "03") {
      // ikona "03" daje tylko zrodlo MSN; "przejaśnienia" po zmroku brzmia dziwnie
      bool night = current.icon.length() >= 3 && current.icon.charAt(2) == 'n';
      txt = night ? L("częściowe zachmurzenie", "partly cloudy") : L("przejaśnienia", "sunny spells");
    } else if (g0 == "04") {
      txt = L("pochmurno", "cloudy");
    }
  }
  if (!txt) return false;
  snprintf(buf, n, "%s", txt);
  return true;
}

// Linia nad paskiem komfortu: etykieta (akcent) + " - " + wskazowka; calosc wysrodkowana
// ---------------------------------------------------------------------
//  Wywoz smieci - przypomnienie nad paskiem komfortu (terminy z panelu WWW, settings.wasteDates).
//  Linia: data + opcjonalnie frakcja. Daty: "23.10", "23.10.2026", "23/10", "2026-10-23".
//  Dzien przed wywozem (caly dzien): "JUTRO WYWOZ: <frakcje>" albo "JUTRO WYWOZ SMIECI" (pomaranczowy),
//  w dniu wywozu do WASTE_TODAY_UNTIL_H: "DZIS WYWOZ: ..." (czerwony).
// ---------------------------------------------------------------------
static const int WASTE_TODAY_UNTIL_H = 12;
char wasteShown[72] = "";        // aktualne przypomnienie (puste = brak) - do odswiezenia przy zmianie dnia / godziny
const bool wastePhase = true;    // przypomnienie wyswietlane caly czas (zamiast etykiety komfortu)
char comfortLabelShown[24] = ""; // ostatnia etykieta komfortu (do przerysowania samej linii tekstu)

void fitText(char *buf, size_t n, int maxW, const GFXfont *f);   // definicja nizej

// Parsuje date z poczatku linii. Zwraca liczbe zuzytych znakow (0 = brak daty); year = 0 -> kazdy rok.
static int parseWasteDate(const char *s, int &day, int &mon, int &year) {
  int num[3] = {0, 0, 0}, digits[3] = {0, 0, 0}, cnt = 0;
  const char *p = s;
  while (cnt < 3) {
    if (!isdigit((unsigned char)*p)) break;
    while (isdigit((unsigned char)*p) && digits[cnt] < 4) { num[cnt] = num[cnt] * 10 + (*p - '0'); digits[cnt]++; p++; }
    cnt++;
    if (cnt < 3 && (*p == '.' || *p == '-' || *p == '/') && isdigit((unsigned char)p[1])) p++;
    else break;
  }
  if (cnt < 2) return 0;
  if (digits[0] == 4) {                       // RRRR-MM-DD
    if (cnt < 3) return 0;
    year = num[0]; mon = num[1]; day = num[2];
  } else {                                    // DD.MM[.RRRR]
    day = num[0]; mon = num[1];
    year = (cnt == 3) ? (digits[2] == 2 ? 2000 + num[2] : num[2]) : 0;
  }
  if (day < 1 || day > 31 || mon < 1 || mon > 12) return 0;
  return (int)(p - s);
}

// Buduje napis przypomnienia; false = dzis nic do pokazania. isToday = wywoz dzisiaj (inny kolor).
// Format listy (settings.wasteDates), linia po linii:
//   "2026"                              - rok dla kolejnych dat bez roku (do nastepnej takiej linii)
//   "6.10 13.10 20.10 zmieszane, bio"   - jedna lub wiecej dat (spacje / przecinki), potem frakcje
//   "2026-10-27 papier", "27.10.26 szklo" - data z rokiem
//   "# komentarz"                       - linie bez daty sa pomijane
// Daty bez roku i bez linii z rokiem obowiazuja co roku.
bool buildWasteReminder(char *out, size_t n, bool &isToday) {
  const String &list = settingsStore.settings.wasteDates;
  if (!list.length()) return false;
  struct tm t;
  if (!getLocalTime(&t, 50)) return false;
  struct tm tom = t;                          // jutro (mktime normalizuje koniec miesiaca/roku)
  tom.tm_mday += 1; tom.tm_hour = 12; tom.tm_isdst = -1;
  mktime(&tom);

  char fracToday[56] = "", fracTom[56] = "";
  bool today = false, tomorrow = false;
  int defYear = 0;                            // rok z linii "2026" (0 = kazdy rok)
  auto addFrac = [](char *dst, size_t dn, const char *f) {
    if (!*f) return;
    if (strstr(dst, f)) return;               // ta sama frakcja wpisana dwa razy
    if (*dst) strlcat(dst, ", ", dn);
    strlcat(dst, f, dn);
  };
  const char *s = list.c_str();
  while (*s) {
    const char *e = strchr(s, '\n');
    size_t len = e ? (size_t)(e - s) : strlen(s);
    char line[200];
    size_t l = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
    memcpy(line, s, l); line[l] = 0;
    s += len; if (*s == '\n') s++;

    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "rok", 3) == 0) { p += 3; while (*p == ' ') p++; }
    {  // linia z samym rokiem
      int y = 0, dg = 0; char *q = p;
      while (isdigit((unsigned char)*q)) { y = y * 10 + (*q - '0'); dg++; q++; }
      while (*q == ' ' || *q == '\t' || *q == ':') q++;
      if (dg == 4 && *q == 0) { defYear = y; continue; }
    }
    // wszystkie daty z poczatku linii
    bool hitToday = false, hitTom = false, any = false;
    for (;;) {
      int d, m, y;
      int used = parseWasteDate(p, d, m, y);
      if (!used) break;
      any = true;
      if (y == 0) y = defYear;
      if (d == t.tm_mday && m == t.tm_mon + 1 && (y == 0 || y == t.tm_year + 1900)) hitToday = true;
      if (d == tom.tm_mday && m == tom.tm_mon + 1 && (y == 0 || y == tom.tm_year + 1900)) hitTom = true;
      p += used;
      while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') p++;
    }
    if (!any) continue;                       // linia bez daty (np. komentarz) - pomijamy
    char *f = p;
    while (*f == ' ' || *f == '\t' || *f == '-' || *f == ':' || *f == ',') f++;
    char *end = f + strlen(f);
    while (end > f && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    if (hitToday) { today = true; addFrac(fracToday, sizeof(fracToday), f); }
    if (hitTom) { tomorrow = true; addFrac(fracTom, sizeof(fracTom), f); }
  }

  const char *frac;
  const char *when;
  if (today && t.tm_hour < WASTE_TODAY_UNTIL_H) { isToday = true; frac = fracToday; when = L("Dziś", "Today"); }
  else if (tomorrow) { isToday = false; frac = fracTom; when = L("Jutro", "Tomorrow"); }
  else return false;
  // z frakcjami krocej ("JUTRO WYWOZ: ZMIESZANE, BIO"), zeby zmiescilo sie w jednej linii
  if (*frac) snprintf(out, n, L("%s wywóz: %s", "Bins %s: %s"), when, frac);
  else snprintf(out, n, L("%s wywóz śmieci", "Bin collection %s"), when);
  uppercasePL(out, n);
  return true;
}

void drawComfortText(const ThemeColors &th, const char *label, int hourNow, int topY) {
  // przypomnienie o wywozie smieci - wyswietlane caly czas zamiast etykiety komfortu i wskazowki
  {
    char w[72];
    bool isToday = false;
    if (buildWasteReminder(w, sizeof(w), isToday)) {
      snprintf(wasteShown, sizeof(wasteShown), "%s", w);
      if (wastePhase) {
        fitText(w, sizeof(w), LCD_H_RES - 16, FONT_SMALL);
        printCentered(w, topY, FONT_SMALL, isToday ? rgb565(0xFF4D4D) : rgb565(0xFF8C1A), th.bg);
        return;
      }
    } else {
      wasteShown[0] = 0;
    }
  }
  char hint[48];
  uint16_t hc = th.text;
  bool has = settingsStore.settings.showHint && buildWeatherHint(hourNow, hint, sizeof(hint));
  if (!has) {
    printCentered(label, topY, FONT_SMALL, th.accent, th.bg);
    return;
  }
  if (hintHazard) {                            // zjawisko grozne / ostrzezenie o wietrze - sam napis, bez etykiety komfortu
    fitText(hint, sizeof(hint), LCD_H_RES - 16, FONT_SMALL);
    printCentered(hint, topY, FONT_SMALL, hintColor, th.bg);
    return;
  }
  const int gap = 6;
  const int wd = 6;                            // myslnik rysujemy sami (glif "-" ladowalby przy gornej krawedzi tekstu)
  int wl = textWidth(label, FONT_SMALL), wh = textWidth(hint, FONT_SMALL);
  int total = wl + gap + wd + gap + wh;
  if (total > LCD_H_RES - 16) {                // za dlugie - sama wskazowka (wazniejsza niz etykieta)
    printCentered(hint, topY, FONT_SMALL, hc, th.bg);
    return;
  }
  int x = (LCD_H_RES - total) / 2;
  printAt(label, x, topY, FONT_SMALL, th.accent, th.bg);
  x += wl + gap;
  gfx->drawFastHLine(x, topY + 8, wd, th.muted);
  gfx->drawFastHLine(x, topY + 9, wd, th.muted);
  x += wd + gap;
  printAt(hint, x, topY, FONT_SMALL, hc, th.bg);
}

// Przerysowanie samej linii tekstu nad paskiem (bez paska i znacznika) - przy zmianie fazy przypomnienia.
// Czyscimy y 398..429; znacznik paska zaczyna sie nizej (y ok. 433).
void drawComfortLine(const ThemeColors &th) {
  if (!comfortLabelShown[0]) return;
  gfx->fillRect(0, 398, LCD_H_RES, 32, th.bg);
  struct tm ti;
  int h = getLocalTime(&ti, 50) ? ti.tm_hour : 12;
  drawComfortText(th, comfortLabelShown, h, 408);
}

void drawComfortBar(const ThemeColors &th) {
  const int barX = 30, barY = 444, barW = 260, barH = 16;
  gfx->fillRect(0, 398, LCD_H_RES, LCD_V_RES - 398, th.bg);

  if (!current.valid && !bmeReady) return;

  // Priorytet: lokalny czujnik w pokoju (bardziej trafny dla "czy tu jest
  // komfortowo"), a dane z internetu tylko jako zapasowe zrodlo, gdy
  // czujnik jeszcze nie jest podlaczony. Wskaznik = humidex (temp+wilgotnosc)
  // + drobna korekta za cisnienie (patrz pressureAdjustment()).
  float sunBonus = current.valid ? sunAdjustment(current.icon) : 0.0;

  float comfortTemp;
  if (bmeReady) {
    comfortTemp = computeHumidex(localTemp, localHumidity) + pressureAdjustment(localPressure) + sunBonus;
  } else {
    // Bez czujnika: podstawa to RZECZYWISTA temperatura powietrza (stacja IMGW albo
    // Open-Meteo). "Odczuwalna" z API uwzglednia wiatr i potrafi byc o kilka stopni
    // nizsza od temperatury, wiec np. 19 C dawalo etykiete "Chlodno". Dlatego:
    //  - w chlodzie (T <= 10 C, wiatr >= 4.8 km/h) wiatr obniza ocene (wind chill),
    //  - w pozostalych przypadkach odczuwalna z API moze ocene tylko PODNIESC
    //    (parnosc, slonce), nigdy obnizyc ponizej rzeczywistej temperatury.
    // Do tego korekty: cisnienie i slonce (jak dotad).
    float windKmh = current.windSpeed * 3.6f;
    float tC = toC(current.temp), feelsC = toC(current.feelsLike);   // przy F - zamiana na C
    float feel;
    if (tC <= 10.0f && windKmh >= 4.8f) feel = windChill(tC, windKmh);
    else feel = (feelsC > tC) ? feelsC : tC;
    comfortTemp = feel + pressureAdjustment(current.pressure) + sunBonus;
  }

  // diagnostyka w Serial Monitorze: skad wzielo sie "komfort" i z jakich skladnikow
  if (bmeReady)
    Serial.printf("Komfort: zrodlo=BME280 T=%.1f RH=%.0f%% cisn=%.0f -> humidex %.1f + cisn %+.1f + slonce %+.1f = %.1f (%s)\n",
                  localTemp, localHumidity, localPressure, computeHumidex(localTemp, localHumidity),
                  pressureAdjustment(localPressure), sunBonus, comfortTemp, comfortLabel(comfortTemp));
  else
    Serial.printf("Komfort: zrodlo=API (brak BME280) T=%.1f odczuwalna(API)=%.1f wiatr=%.1f m/s cisn=%.0f -> cisn %+.1f + slonce %+.1f => %.1f (%s)\n",
                  current.temp, current.feelsLike, current.windSpeed, current.pressure,
                  pressureAdjustment(current.pressure), sunBonus, comfortTemp, comfortLabel(comfortTemp));
  struct tm hourInfo;
  int hourNow = getLocalTime(&hourInfo, 50) ? hourInfo.tm_hour : 12;
  snprintf(comfortLabelShown, sizeof(comfortLabelShown), "%s", comfortLabel(comfortTemp));
  drawComfortText(th, comfortLabelShown, hourNow, 408);

  float tempMinRange = COMFORT_STOPS[0].temp;
  float tempMaxRange = COMFORT_STOPS[COMFORT_STOP_COUNT - 1].temp;

  for (int x = 0; x < barW; x++) {
    float frac = (float)x / (barW - 1);
    float t = tempMinRange + frac * (tempMaxRange - tempMinRange);
    gfx->drawFastVLine(barX + x, barY, barH, comfortColorForTemp(t));
  }
  gfx->drawRect(barX - 1, barY - 1, barW + 2, barH + 2, th.text);

  float clamped = constrain(comfortTemp, tempMinRange, tempMaxRange);
  float markerFrac = (clamped - tempMinRange) / (tempMaxRange - tempMinRange);
  int markerX = barX + (int)(markerFrac * (barW - 1));

  int triY = barY - 3;
  aaFillTriangle(markerX, triY, markerX - 5, triY - 8, markerX + 5, triY - 8, th.text, th.bg);
}

// Male kreseczki zasiegu WiFi w prawym gornym rogu (4 paski, rosnace
// wysokoscia, wypelnione zaleznie od sily sygnalu RSSI).
void drawWifiBars(const ThemeColors &th) {
  const int x = 288, yBase = 24, barW = 4, gap = 2;
  // bez polaczenia RSSI() zwraca 0, co dawaloby 4 kreski. Na baterii (Wi-Fi wylaczone celowo)
  // pokazujemy zasieg z ostatniego polaczenia (tym samym kolorem co biezacy).
  bool live = (WiFi.status() == WL_CONNECTED);
  bool stale = !live && onBattery && lastRssi != 0;
  int rssi = live ? WiFi.RSSI() : (stale ? lastRssi : -127);
  int bars;
  if (rssi >= -55) bars = 4;
  else if (rssi >= -65) bars = 3;
  else if (rssi >= -75) bars = 2;
  else if (rssi >= -85) bars = 1;
  else bars = 0;

  gfx->fillRect(x - 2, 6, 26, 20, th.bg);

  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 4;
    int bx = x + i * (barW + gap);
    int by = yBase - h;
    uint16_t color = (i < bars) ? th.text : th.divider;
    gfx->fillRect(bx, by, barW, h, color);
  }
}

uint16_t aqiColor(int aqi) {
  switch (aqi) {
    case 1: return rgb565(0x5AC85A);  // dobre - zielony
    case 2: return rgb565(0xA8D84A);  // dobre/umiark. - zolto-zielony
    case 3: return rgb565(0xE6C83C);  // umiarkowane - zolty
    case 4: return rgb565(0xE67E3C);  // zle - pomaranczowy
    case 5: return rgb565(0xDC4628);  // bardzo zle - czerwony
    default: return 0;
  }
}

// Kolorowa kropka jakosci powietrza (AQI z Open-Meteo, skala 1-5) - bez
// etykiety, umieszczana inline obok strzalki trendu cisnienia.
void drawAqiDot(int cx, int cy) {
  if (currentAqi <= 0) return;
  aaFillCircle(cx, cy, 4, aqiColor(currentAqi), currentTheme().bg);
}

void redrawAll() {
  ThemeColors th = currentTheme();
  drawStaticLayout(th);
  drawClock(th);
  drawWifiBars(th);
  drawCurrentWeather(th);
  drawForecast(th);
  drawComfortBar(th);
}

// ---------------------------------------------------------------------
//  Ekrany: glowny / szczegoly dnia + obsluga dotyku
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
//  Ekran informacyjny (dotkniecie kresek zasiegu w prawym gornym rogu):
//  siec Wi-Fi, zrodlo pogody i godziny danych, wschod i zachod slonca.
//  Ten sam styl co szczegoly dnia: tytul w kolorze akcentu, naglowki sekcji z cienka linia,
//  etykiety przygaszone po lewej, wartosci jasne po prawej.
// ---------------------------------------------------------------------
// Skraca tekst (na granicy znaku UTF-8) tak, aby miescil sie w maxW pikselach; dodaje ".." na koncu.
void fitText(char *buf, size_t n, int maxW, const GFXfont *f) {
  if (textWidth(buf, f) <= maxW) return;
  size_t len = strlen(buf);
  while (len > 1) {
    len--;
    while (len > 0 && ((uint8_t)buf[len] & 0xC0) == 0x80) len--;   // nie tnij w srodku znaku
    buf[len] = 0;
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s..", buf);
    if (textWidth(tmp, f) <= maxW) { snprintf(buf, n, "%s", tmp); return; }
  }
}

// Opis jakosci sygnalu po RSSI [dBm] oraz liczba kresek 0-4 (te same progi co drawWifiBars).
const char *wifiQualityLabel(int rssi) {
  if (rssi >= -50) return L("doskonały", "excellent");
  if (rssi >= -60) return L("bardzo dobry", "very good");
  if (rssi >= -70) return L("dobry", "good");
  if (rssi >= -80) return L("słaby", "weak");
  return L("bardzo słaby", "very weak");
}

// Dwa przyciski na dole ekranu informacji: "Restart" (zielony) i "Tryb AP" (czerwony - przejscie w tryb
// konfiguracji, jak "Przejdź w tryb AP" w panelu WWW). Kazdy wymaga dwoch dotkniec: pierwsze "uzbraja"
// przycisk (wypelnia sie kolorem, napis "Dotknij ponownie") na INFO_ARM_MS, drugie w tym czasie wykonuje
// akcje - przypadkowe dotkniecie nic nie zrobi. Dotkniecie poza przyciskami = powrot do ekranu glownego.
static const int INFO_BTN_Y = 414, INFO_BTN_W = 142, INFO_BTN_H = 48;
static const int INFO_BTN_X[2] = {14, LCD_H_RES - 14 - INFO_BTN_W};
static const unsigned long INFO_ARM_MS = 4000;
int8_t infoArmedBtn = -1;                  // -1 = zaden przycisk nie czeka na potwierdzenie
unsigned long infoArmedAt = 0;

bool infoBtnArmed(int i) { return infoArmedBtn == i && millis() - infoArmedAt < INFO_ARM_MS; }

void drawInfoButton(int i, const ThemeColors &th) {
  const char *const kLabel[2] = {"Restart", L("Tryb AP", "AP mode")};
  const uint16_t col = (i == 0) ? rgb565(0x50C864) : rgb565(0xFF4D4D);
  bool armed = infoBtnArmed(i);
  int x = INFO_BTN_X[i];
  uint16_t fill = armed ? col : lerpColor565(th.bg, th.divider, 0.55f);   // jak tlo kart na innych ekranach
  gfx->fillRect(x, INFO_BTN_Y, INFO_BTN_W, INFO_BTN_H, th.bg);
  gfx->fillRoundRect(x, INFO_BTN_Y, INFO_BTN_W, INFO_BTN_H, 10, fill);
  gfx->drawRoundRect(x, INFO_BTN_Y, INFO_BTN_W, INFO_BTN_H, 10, col);
  const char *t = armed ? L("Dotknij ponownie", "Tap again") : kLabel[i];
  const GFXfont *f = armed ? FONT_SMALL : FONT_DAY_TITLE;
  // wysokosc wersalika: FreeSans9pt 13 px, FreeSansBold12pt 17 px
  int ty = INFO_BTN_Y + (INFO_BTN_H - (armed ? 13 : 17)) / 2;
  printAt(t, x + (INFO_BTN_W - textWidth(t, f)) / 2, ty, f, armed ? th.bg : col, fill);
}

void drawInfoButtons(const ThemeColors &th) {
  drawInfoButton(0, th);
  drawInfoButton(1, th);
}

int infoButtonAt(int x, int y) {           // indeks przycisku pod palcem (z zapasem) albo -1
  if (y < INFO_BTN_Y - 8 || y >= INFO_BTN_Y + INFO_BTN_H + 10) return -1;
  for (int i = 0; i < 2; i++)
    if (x >= INFO_BTN_X[i] - 6 && x < INFO_BTN_X[i] + INFO_BTN_W + 6) return i;
  return -1;
}

void doRestart(const ThemeColors &th, bool toAp) {
  if (toAp) {                              // jednorazowy znacznik - przy starcie od razu siec StacjaPogody-Setup
    Preferences p;
    p.begin("wclock", false);
    p.putBool("forceap", true);
    p.end();
  }
  gfx->fillRect(0, 0, LCD_H_RES, LCD_V_RES, th.bg);
  printCentered(toAp ? L("Tryb konfiguracji...", "Setup mode...") : "Restart...", 228, FONT_DAY_TITLE, th.accent, th.bg);
  delay(300);
  WiFi.disconnect(true);
  delay(200);
  ESP.restart();
}

void drawInfoScreen(const ThemeColors &th) {
  gfx->fillRect(0, 0, LCD_H_RES, LCD_V_RES, th.bg);
  AppSettings &st = settingsStore.settings;
  const int xl = 14, xr = LCD_H_RES - 14;

  // naglowek sekcji: tekst wersalikami w kolorze akcentu + cienka linia do prawej krawedzi (lineEnd)
  auto heading = [&](const char *t, int y, int lineEnd) {
    char b[32];
    snprintf(b, sizeof(b), "%s", t);
    uppercasePL(b, sizeof(b));
    printAt(b, xl, y, FONT_SMALL, th.accent, th.bg);
    int w = textWidth(b, FONT_SMALL);
    gfx->drawFastHLine(xl + w + 8, y + 9, lineEnd - (xl + w + 8), th.divider);
  };
  // wiersz: etykieta po lewej (przygaszona), wartosc po prawej (jasna, skracana gdy za dluga)
  auto row = [&](const char *label, const char *value, int y) {
    printAt(label, xl, y, FONT_SMALL, th.muted, th.bg);
    char v[64];
    snprintf(v, sizeof(v), "%s", value);
    fitText(v, sizeof(v), xr - xl - textWidth(label, FONT_SMALL) - 12, FONT_SMALL);
    printAt(v, xr - textWidth(v, FONT_SMALL), y, FONT_SMALL, th.text, th.bg);
  };

  // tytul
  char title[24];
  snprintf(title, sizeof(title), "%s", L("Informacje", "Information"));
  uppercasePL(title, sizeof(title));
  printAt(title, (LCD_H_RES - textWidth(title, FONT_DAY_TITLE)) / 2, 10, FONT_DAY_TITLE, th.accent, th.bg);

  // --- Siec Wi-Fi --- (wiersze co 22 px, zeby zmiescil sie model plytki w sekcji Urzadzenie)
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  bool stale = !wifiOk && onBattery && lastRssi != 0;     // ostatni pomiar sprzed wylaczenia Wi-Fi
  int rssi = wifiOk ? WiFi.RSSI() : (stale ? lastRssi : -100);
  int bars = (rssi >= -55) ? 4 : (rssi >= -65) ? 3 : (rssi >= -75) ? 2 : (rssi >= -85) ? 1 : 0;
  heading(L("Sieć Wi-Fi", "Wi-Fi network"), 52, 258);
  for (int i = 0; i < 4; i++) {            // kreski zasiegu z prawej strony naglowka
    int h = 5 + i * 4;
    uint16_t c = (i < bars && (wifiOk || stale)) ? th.accent : th.divider;
    gfx->fillRect(xr - 38 + i * 10, 70 - h, 7, h, c);
  }
  char buf[64];
  const char *none = L("brak", "none");
  row(L("Sieć", "Network"), wifiOk ? WiFi.SSID().c_str() : (onBattery ? L("wyłączona (bateria)", "off (battery)") : L("brak połączenia", "not connected")), 74);
  if (wifiOk) snprintf(buf, sizeof(buf), "%d dBm  (%s)", rssi, wifiQualityLabel(rssi));
  else if (stale) snprintf(buf, sizeof(buf), "%d dBm  (%s %s)", rssi, L("o", "at"), lastRssiTime);
  else snprintf(buf, sizeof(buf), "%s", none);
  row(L("Sygnał", "Signal"), buf, 96);
  if (wifiOk) snprintf(buf, sizeof(buf), "%d", (int)WiFi.channel());
  else snprintf(buf, sizeof(buf), "%s", none);
  row(L("Kanał", "Channel"), buf, 118);
  row(L("Adres IP", "IP address"), wifiOk ? WiFi.localIP().toString().c_str() : none, 140);
  {  // MAC z eFuse - dziala tez przy wylaczonym Wi-Fi
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    row("MAC", buf, 162);
  }

  // --- Pogoda ---
  gfx->drawFastHLine(xl, 186, LCD_H_RES - 2 * xl, th.divider);
  heading(L("Pogoda", "Weather"), 192, xr);
  bool msn = (strcmp(sourceLabel, "MSN") == 0);
  row(L("Źródło", "Source"), msn ? L("MSN Pogoda", "MSN Weather") : "Open-Meteo", 214);
  if (sourceImgw) snprintf(buf, sizeof(buf), "IMGW, %s", imgwStationName);
  else snprintf(buf, sizeof(buf), "%s", msn ? "MSN" : L("model Open-Meteo", "Open-Meteo model"));
  row(L("Pomiary", "Readings"), buf, 236);
  if (st.countryCode.length()) snprintf(buf, sizeof(buf), "%s, %s", st.city.c_str(), st.countryCode.c_str());
  else snprintf(buf, sizeof(buf), "%s", st.city.c_str());
  row(L("Miasto", "City"), buf, 258);
  // godzina pomiaru: IMGW - pelna godzina stacji; Open-Meteo - czas odczytu modelu; MSN - czas pobrania
  snprintf(buf, sizeof(buf), "%s", sourceImgw ? imgwHourStr : (msn ? fetchTimeStr : omObsTime));
  row(L("Pomiar z godz.", "Reading time"), buf, 280);

  // --- Urzadzenie ---
  gfx->drawFastHLine(xl, 304, LCD_H_RES - 2 * xl, th.divider);
  heading(L("Urządzenie", "Device"), 310, xr);
  snprintf(buf, sizeof(buf), "v%s", FW_VERSION);
  row(L("Wersja firmware", "Firmware version"), buf, 332);
  row("Hardware", "ONX3248G035", 354);
  // zasilanie: USB (bez ogniwa) / USB + stan ogniwa / bateria z napieciem i poziomem
  if (!battery.ready) snprintf(buf, sizeof(buf), "%s", L("brak odczytu", "no reading"));
  else if (!battery.present) snprintf(buf, sizeof(buf), "%s", L("brak napięcia baterii", "no battery voltage"));
  else if (battery.external) snprintf(buf, sizeof(buf), "USB");
  else if (battery.usbFull) snprintf(buf, sizeof(buf), L("USB, bateria pełna %.2f V", "USB, battery full %.2f V"), battery.mv / 1000.0f);
  else if (battery.charging) snprintf(buf, sizeof(buf), L("USB, ładowanie %.2f V", "USB, charging %.2f V"), battery.mv / 1000.0f);
  else snprintf(buf, sizeof(buf), L("bateria %.2f V, %u%%", "battery %.2f V, %u%%"), battery.mv / 1000.0f, battery.percent);
  row(L("Zasilanie", "Power"), buf, 376);

  drawInfoButtons(th);
}

// ---------------------------------------------------------------------
//  Ekran "Przeglad dnia" (dotkniecie paska komfortu): wykres temperatury z opadami na 24 h,
//  indeks UV, pylki, faza Ksiezyca oraz wschod i zachod slonca.
//  Styl jak ekran informacyjny: tytul w kolorze akcentu, naglowki sekcji z cienka linia.
// ---------------------------------------------------------------------
// Faza Ksiezyca 0..1 (0 = now, 0,25 = pierwsza kwadra, 0,5 = pelnia). Srednia dlugosc miesiaca synodycznego
// (29,530589 d) liczona od nowiu 6.01.2000 18:14 UTC - dokladnosc ok. +-0,5 doby, wystarcza do ekranu.
float moonPhaseAt(time_t t) {
  const double kSyn = 29.530588853;
  double d = ((double)t - 947182440.0) / 86400.0;   // 947182440 = 2000-01-06 18:14 UTC
  double p = fmod(d, kSyn);
  if (p < 0) p += kSyn;
  return (float)(p / kSyn);
}

const char *moonPhaseName(float p) {
  const float e = 1.0f / 29.53f;   // +-1 doba wokol nowiu, kwadr i pelni
  if (p < e || p >= 1.0f - e) return L("Nów", "New moon");
  if (p < 0.25f - e) return L("Rosnący sierp", "Waxing crescent");
  if (p < 0.25f + e) return L("Pierwsza kwadra", "First quarter");
  if (p < 0.5f - e) return L("Rosnący garb", "Waxing gibbous");
  if (p < 0.5f + e) return L("Pełnia", "Full moon");
  if (p < 0.75f - e) return L("Malejący garb", "Waning gibbous");
  if (p < 0.75f + e) return L("Ostatnia kwadra", "Last quarter");
  return L("Malejący sierp", "Waning crescent");
}

// Tarcza Ksiezyca renderowana pikselowo: kula oswietlona wg fazy (miekki terminator), morza i krater Tycho jak
// na prawdziwym Ksiezycu (polkula polnocna: przy rosnacym oswietlona prawa strona), delikatne ciemnienie przy
// brzegu, wygladzony brzeg, slabo widoczna ciemna czesc ("swiatlo popielate"). Liczone raz na odswiezenie ekranu.
void drawMoonPhase(int cx, int cy, int r, float p, const ThemeColors &th) {
  static uint16_t buf[61 * 61];                 // do r = 29 (ekran glowny); "Przeglad dnia" uzywa r = 20
  if (r > 29) r = 29;
  const int n = 2 * r + 3, c0 = r + 1;
  // morza: srodek (x, y w jednostkach promienia, y w dol), promien, sila
  static const float kMaria[][4] = {
    {-0.36f, -0.44f, 0.30f, 0.34f},   // Mare Imbrium
    { 0.16f, -0.40f, 0.18f, 0.32f},   // Mare Serenitatis
    { 0.40f, -0.10f, 0.22f, 0.32f},   // Mare Tranquillitatis
    { 0.74f, -0.28f, 0.10f, 0.30f},   // Mare Crisium
    { 0.62f,  0.14f, 0.13f, 0.26f},   // Mare Fecunditatis
    {-0.70f, -0.06f, 0.30f, 0.26f},   // Oceanus Procellarum
    {-0.26f,  0.36f, 0.17f, 0.26f},   // Mare Nubium
    {-0.58f,  0.38f, 0.10f, 0.24f},   // Mare Humorum
  };
  const float phi = 2.0f * PI * p;
  const float sx = sinf(phi), sz = -cosf(phi);   // kierunek Slonca: p=0 now (za tarcza), 0.5 pelnia (z przodu)
  const bool waxing = (p < 0.5f);
  const float sunX = waxing ? fabsf(sx) : -fabsf(sx);
  const int bgR = ((th.bg >> 11) & 31) * 255 / 31, bgG = ((th.bg >> 5) & 63) * 255 / 63, bgB = (th.bg & 31) * 255 / 31;
  for (int iy = 0; iy < n; iy++) {
    for (int ix = 0; ix < n; ix++) {
      float fx = (ix - c0) / (float)r, fy = (iy - c0) / (float)r;
      float d2 = fx * fx + fy * fy;
      float dist = sqrtf(d2) * r;                         // odleglosc od srodka w pikselach
      float cov = r + 0.5f - dist;                        // pokrycie brzegu (wygladzanie)
      if (cov <= 0) { buf[iy * n + ix] = th.bg; continue; }
      if (cov > 1) cov = 1;
      float z = sqrtf(fmaxf(0.0f, 1.0f - d2));
      // albedo: morza, Tycho, ciemnienie przy brzegu
      float alb = 1.0f;
      for (unsigned k = 0; k < sizeof(kMaria) / sizeof(kMaria[0]); k++) {
        float dx = fx - kMaria[k][0], dy = fy - kMaria[k][1];
        alb -= kMaria[k][3] * expf(-(dx * dx + dy * dy) / (2.0f * kMaria[k][2] * kMaria[k][2]) * 2.2f);
      }
      { float dx = fx + 0.12f, dy = fy - 0.66f; alb += 0.18f * expf(-(dx * dx + dy * dy) / 0.004f); }   // Tycho
      alb *= 0.80f + 0.20f * z;
      if (alb < 0.25f) alb = 0.25f;
      if (alb > 1.15f) alb = 1.15f;
      // oswietlenie: miekki terminator
      float ndl = fx * sunX + z * sz;
      float t = (ndl + 0.05f) / 0.28f; if (t < 0) t = 0; if (t > 1) t = 1;
      float lightF = t * t * (3.0f - 2.0f * t);
      float lit = lightF * (0.60f + 0.40f * (ndl > 0 ? ndl : 0));
      float lum = 0.09f + 0.91f * lit;                    // 0.09 = swiatlo popielate
      float rr = 244 * alb * lum, gg = 238 * alb * lum, bb = 222 * alb * lum;
      if (lit < 0.5f) { bb += 6 * (1 - lit * 2); }        // ciemna czesc lekko chlodna
      float a = cov;
      int R = (int)(bgR + (rr - bgR) * a + 0.5f), G = (int)(bgG + (gg - bgG) * a + 0.5f), B = (int)(bgB + (bb - bgB) * a + 0.5f);
      if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
      if (R < 0) R = 0; if (G < 0) G = 0; if (B < 0) B = 0;
      buf[iy * n + ix] = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
    }
  }
  gfx->draw16bitRGBBitmap(cx - c0, cy - c0, buf, n, n);
}

// Poziom stezenia pylku 0-4 (brak, niskie, umiarkowane, wysokie, bardzo wysokie) wg PRZYBLIZONYCH progow
// [ziarna/m3]. Rodzaje: 0 olcha, 1 brzoza, 2 trawy, 3 bylica, 4 ambrozja.
int pollenLevel(int kind, float v) {
  static const float kT[5][4] = {{1, 15, 90, 1500}, {1, 15, 90, 1500}, {1, 20, 90, 200}, {1, 10, 50, 150}, {1, 10, 50, 150}};
  int lvl = 0;
  for (int i = 0; i < 4; i++)
    if (v >= kT[kind][i]) lvl = i + 1;
  return lvl;
}

// Kategoria UV wg WHO po zaokragleniu do calosci (tak jak na ekranie): 1 niskie (0-2), 2 umiarkowane (3-5),
// 3 wysokie (6-7), 4 bardzo wysokie (8-10), 5 ekstremalne (11+).
int uvCategory(int u) {
  return u <= 2 ? 1 : u <= 5 ? 2 : u <= 7 ? 3 : u <= 10 ? 4 : 5;
}

// Kolor skali 1-5: zielony, zolty, pomaranczowy, czerwony, fioletowy (te same barwy co kropka AQI).
uint16_t scaleColor5(int lvl) {
  switch (lvl) {
    case 1: return aqiColor(1);
    case 2: return aqiColor(3);
    case 3: return aqiColor(4);
    case 4: return aqiColor(5);
    default: return rgb565(0xB464D2);
  }
}

// Ekran "Przeglad dnia" w ukladzie kart: opady na 24 h (slupki), slonce (luk drogi slonca z biezacym polozeniem,
// wschod/zachod, dlugosc dnia) i Ksiezyc, pylki (pasek poziomu dla kazdego gatunku).
void drawOutlookScreen(const ThemeColors &th) {
  gfx->fillRect(0, 0, LCD_H_RES, LCD_V_RES, th.bg);
  const uint16_t cardCol = lerpColor565(th.bg, th.divider, 0.55f);
  const uint16_t sunCol = 0xFE60;          // jak w ikonach pogody
  char buf[64];
  auto cardBox = [&](int x, int y, int w, int h) { gfx->fillRoundRect(x, y, w, h, 10, cardCol); };
  auto label = [&](const char *t, int x, int y) {
    char b[32];
    snprintf(b, sizeof(b), "%s", t);
    uppercasePL(b, sizeof(b));
    printAt(b, x, y, FONT_SMALL, th.accent, cardCol);
  };
  auto rightText = [&](const char *t, int xr, int y, uint16_t c) {
    printAt(t, xr - textWidth(t, FONT_SMALL), y, FONT_SMALL, c, cardCol);
  };
  auto centerText = [&](const char *t, int cx, int y, uint16_t c) {
    printAt(t, cx - textWidth(t, FONT_SMALL) / 2, y, FONT_SMALL, c, cardCol);
  };

  char title[24];
  snprintf(title, sizeof(title), "%s", L("Przegląd dnia", "Day overview"));
  uppercasePL(title, sizeof(title));
  printAt(title, (LCD_H_RES - textWidth(title, FONT_DAY_TITLE)) / 2, 10, FONT_DAY_TITLE, th.accent, th.bg);

  // ===== Karta: opady na 24 h (y 40..216) - slupek na godzine = szansa opadow 0-100 % =====
  cardBox(10, 40, 300, 176);
  label(L("Opady - 24 h", "Rain - 24 h"), 22, 50);
  struct tm ti = {};
  int hh = getLocalTime(&ti, 50) ? ti.tm_hour : 0;
  const int N = 24;
  int rn[N];
  bool dayF[N], ok[N];
  int nOk = 0, iPeak = -1;
  for (int i = 0; i < N; i++) {
    int hr = hh + i;
    const HourPoint *hp = nullptr;
    if (hr < 24) hp = &hourlyToday[hr];
    else if (forecastCount > 0 && hr - 24 < 24) hp = &hourly[0][hr - 24];   // hourly[0] = jutro
    ok[i] = (hp && hp->valid && hp->rain >= 0);
    rn[i] = ok[i] ? hp->rain : 0;
    dayF[i] = (hp && hp->valid) ? hp->isDay : true;
    if (ok[i]) {
      nOk++;
      if (iPeak < 0 || rn[i] > rn[iPeak]) iPeak = i;
    }
  }
  const int X0 = 22, X1 = 298, PT = 90, RB = 186;   // PT nizej - opis "100%" nie nachodzi na naglowek
  const float dx = (X1 - X0) / 24.0f;
  auto xI = [&](int i) { return (int)lroundf(X0 + i * dx); };
  if (nOk < 2) {
    centerText(L("Brak danych o opadach", "No rain data"), 160, 120, th.muted);
  } else {
    // podsumowanie w naglowku: najwyzsza szansa i godzina
    if (iPeak >= 0 && rn[iPeak] > 0) snprintf(buf, sizeof(buf), L("max %d%% o %02d:00", "max %d%% at %02d:00"), rn[iPeak], (hh + iPeak) % 24);
    else snprintf(buf, sizeof(buf), "%s", L("bez opadów", "no rain"));
    rightText(buf, X1, 50, th.muted);
    // noc - lekko podswietlone tlo; siatka 50 % i 100 % z malym opisem z prawej
    uint16_t nightCol = lerpColor565(cardCol, th.accent, 0.08f);
    for (int i = 0; i < 24; i++)
      if (!dayF[i]) gfx->fillRect(xI(i), PT - 6, xI(i + 1) - xI(i), RB - PT + 6, nightCol);
    uint16_t grid = lerpColor565(cardCol, th.muted, 0.25f);
    for (int g = 50; g <= 100; g += 50) {
      int y = RB - g * (RB - PT) / 100;
      gfx->drawFastHLine(X0, y, X1 - X0, grid);
      snprintf(buf, sizeof(buf), "%d%%", g);
      rightText(buf, X1, y - 15, grid);
    }
    // slupki z zaokraglona gora
    for (int i = 0; i < 24; i++) {
      if (!ok[i] || rn[i] <= 0) continue;
      int h = (rn[i] * (RB - PT) + 50) / 100;
      if (h < 2) h = 2;
      int x = xI(i), w = xI(i + 1) - x - 3;
      gfx->fillRoundRect(x + 1, RB - h, w, h, h >= 6 ? 3 : 1, DROP_COLOR);
    }
    gfx->drawFastHLine(X0, RB, X1 - X0, th.divider);
    // os czasu: "teraz" i pelne godziny co 6 h
    printAt(L("teraz", "now"), X0, RB + 6, FONT_SMALL, th.accent, cardCol);
    for (int i = 5; i < 24; i++) {
      int hr = (hh + i) % 24;
      if (hr % 6 != 0) continue;
      snprintf(buf, sizeof(buf), "%02d", hr);
      centerText(buf, xI(i), RB + 6, th.muted);
    }
  }

  // ===== Karta: slonce i Ksiezyc (y 224..340) =====
  cardBox(10, 224, 300, 116);
  label(L("Słońce", "Sun"), 22, 234);
  label(L("Księżyc", "Moon"), 170, 234);
  gfx->drawFastVLine(160, 240, 92, th.divider);   // weszy blok slonca - wiecej miejsca na opis Ksiezyca
  {
    // luk drogi slonca (polelipsa) od wschodu do zachodu, kropka w biezacym polozeniu slonca
    const int cx = 85, by = 314, rx = 60, ry = 50;
    int srH, srM, ssH, ssM;
    bool haveSun = sscanf(sunriseStr, "%d:%d", &srH, &srM) == 2 && sscanf(sunsetStr, "%d:%d", &ssH, &ssM) == 2;
    float f = -1;                                // 0 = wschod, 1 = zachod; < 0 lub > 1 = noc
    if (haveSun) {
      int sr = srH * 60 + srM, ss = ssH * 60 + ssM, nowM = ti.tm_hour * 60 + ti.tm_min;
      if (ss > sr) f = (float)(nowM - sr) / (float)(ss - sr);
    }
    bool isDayNow = (f >= 0 && f <= 1);
    {
      // wygladzony luk drogi slonca jako linia PRZERYWANA (szara, kreski ok. 5 px co 9 px dlugosci luku)
      // i zolta kropka slonca w biezacym polozeniu - jeden przebieg, bez szwow
      const int kMaxDash = 32;
      static AaShape dash[kMaxDash];
      int nd = 0;
      {
        const float dashLen = 5.0f, period = 9.0f;
        float px = cx - rx, py = by - 0.5f, along = 0, dStart = -1;
        float sxD = px, syD = py;
        for (int i = 1; i <= 240 && nd < kMaxDash; i++) {          // probkowanie luku i odmierzanie dlugosci
          float a = PI + PI * i / 240.0f;
          float x = cx + rx * cosf(a), y = by - 0.5f + ry * sinf(a);
          along += sqrtf((x - px) * (x - px) + (y - py) * (y - py));
          float ph = fmodf(along, period);
          if (ph < dashLen && dStart < 0) { dStart = along; sxD = px; syD = py; }   // poczatek kreski
          if ((ph >= dashLen || i == 240) && dStart >= 0) {                        // koniec kreski
            dash[nd++] = aaCapsule(sxD, syD, x, y, 0.9f);
            dStart = -1;
          }
          px = x; py = y;
        }
      }
      float sxf = cx + rx * cosf(PI + PI * f), syf = by + ry * sinf(PI + PI * f);
      AaShape sun = aaDisc(sxf, syf, 6.5f);
      AaLayer layers[2] = { aaLayer(dash, (uint8_t)nd, th.muted),
                            aaLayer(&sun, 1, sunCol) };
      aaDraw(layers, isDayNow ? 2 : 1, cardCol);
    }
    gfx->drawFastHLine(18, by, 134, th.muted);     // horyzont
    // dlugosc dnia w srodku luku, wschod i zachod pod koncami
    if (haveSun) {
      int len = (ssH * 60 + ssM) - (srH * 60 + srM);
      snprintf(buf, sizeof(buf), "%d h %d min", len / 60, len % 60);
      centerText(buf, cx, by - 18, th.muted);
    }
    printAt(sunriseStr, 22, by + 5, FONT_DAY_TITLE, th.text, cardCol);
    printAt(sunsetStr, 150 - textWidth(sunsetStr, FONT_DAY_TITLE), by + 5, FONT_DAY_TITLE, th.text, cardCol);
  }
  {
    // Ksiezyc: tarcza fazy, nazwa fazy i "pelnia za N dni"
    const int tx = 170, tw = 298 - tx, mcx = tx + tw / 2;   // mcx = srodek bloku Ksiezyca (miedzy kreska a brzegiem karty)
    time_t nowT = time(nullptr);
    if (nowT > 1700000000) {
      float ph = moonPhaseAt(nowT);
      ThemeColors mth = th;
      mth.bg = cardCol;                          // tarcza rysowana na tle karty (wygladzony brzeg)
      drawMoonPhase(mcx, 276, 20, ph, mth);
      snprintf(buf, sizeof(buf), "%s", moonPhaseName(ph));
      fitText(buf, sizeof(buf), tw, FONT_SMALL);
      centerText(buf, mcx, 302, th.text);
      float toFullF = ph < 0.5f ? 0.5f - ph : 1.5f - ph;     // pelnia przy fazie 0,5
      int days = (int)lroundf(toFullF * 29.530589f);
      if (days <= 0) snprintf(buf, sizeof(buf), "%s", L("pełnia dziś", "full moon today"));
      else if (days == 1) snprintf(buf, sizeof(buf), "%s", L("pełnia jutro", "full tomorrow"));
      else snprintf(buf, sizeof(buf), L("pełnia za %d dni", "full in %d days"), days);
      fitText(buf, sizeof(buf), tw, FONT_SMALL);
      centerText(buf, mcx, 320, th.muted);
    } else {
      centerText(L("brak czasu", "no time"), mcx, 290, th.muted);
    }
  }

  // ===== Karta: pylki (y 348..470) - wiersz na gatunek: nazwa, pasek 4 segmentow, slowo =====
  cardBox(10, 348, 300, 122);
  label(L("Pyłki", "Pollen"), 22, 358);
  if (airExtras.pollenValid) {
    const char *const kPollenNames[5] = {L("Olcha", "Alder"), L("Brzoza", "Birch"), L("Trawy", "Grass"),
                                         L("Bylica", "Mugwort"), L("Ambrozja", "Ragweed")};
    const char *const kLvlNames[5] = {L("brak", "none"), L("niskie", "low"), L("średnie", "medium"),
                                      L("wysokie", "high"), L("b. wysokie", "v. high")};
    for (int k = 0; k < 5; k++) {
      int y = 380 + k * 17;
      int lvl = pollenLevel(k, airExtras.pollen[k]);
      printAt(kPollenNames[k], 22, y, FONT_SMALL, lvl ? th.text : th.muted, cardCol);
      for (int sg = 0; sg < 4; sg++)
        gfx->fillRoundRect(100 + sg * 36, y + 4, 32, 7, 3, sg < lvl ? scaleColor5(sg + 1) : th.divider);
      rightText(kLvlNames[lvl], 298, y, lvl ? th.text : th.muted);
    }
  } else {
    centerText(L("brak danych o pyłkach", "no pollen data"), 160, 404, th.muted);
  }
}

// ---------------------------------------------------------------------
//  Ekran "Powietrze i odczucia" (dotkniecie paska komfortu): jakosc powietrza (AQI, PM2,5, PM10),
//  temperatura odczuwalna, wilgotnosc oraz krotki opis pogody na jutro (porownanie z dzisiaj).
// ---------------------------------------------------------------------
const char *aqiWord(int level) {
  switch (level) {
    case 1: return L("Bardzo dobra", "Very good");
    case 2: return L("Dobra", "Good");
    case 3: return L("Umiarkowana", "Moderate");
    case 4: return L("Zła", "Poor");
    case 5: return L("Bardzo zła", "Very poor");
    default: return L("Brak danych", "No data");
  }
}

// Poziom 1-5 dla stezenia pylu [ug/m3] wg progow EEA: b[0..3] = gorne granice poziomow 1-4
int pmLevel(float v, const float *b) {
  for (int i = 0; i < 4; i++) if (v <= b[i]) return i + 1;
  return 5;
}

// Opis wilgotnosci powietrza na zewnatrz. "Parno" tylko gdy jest naprawde ciepło i wilgotno -
// o tym decyduje punkt rosy (>= 18 C, wzor Magnusa), a nie sama wilgotnosc: zima 90% to normalka,
// a nie parnosc. tempC = temperatura powietrza w st. C.
const char *humidityWord(int h, float tempC) {
  float a = 17.62f, b = 243.12f;
  float g = logf((h > 0 ? h : 1) / 100.0f) + a * tempC / (b + tempC);
  float dew = b * g / (a - g);                   // punkt rosy [C]
  if (dew >= 18.0f) return L("parno", "muggy");
  if (h < 30) return L("sucho", "dry");
  if (h < 45) return L("dość sucho", "rather dry");
  if (h <= 75) return L("komfortowo", "comfortable");
  if (h < 90) return L("wilgotno", "humid");
  return L("bardzo wilgotno", "very humid");
}

// Budowa opisu "co bedzie jutro": do 3 krotkich linii, od najwazniejszej. Zwraca liczbe linii.
// lvl[i]: 0 = informacja, 1 = uwaga (kolor ostrzegawczy)
int buildTomorrowLines(char lines[3][56], uint8_t lvl[3], uint16_t cols[3], const ThemeColors &th) {
  int n = 0;
  auto add = [&](uint8_t level, uint16_t col, const char *fmt, int a) {
    if (n >= 3) return;
    snprintf(lines[n], 56, fmt, a);
    lvl[n] = level;
    cols[n] = col;
    n++;
  };
  if (forecastCount == 0 || !forecast[0].date[0]) {
    snprintf(lines[0], 56, "%s", L("Brak danych prognozy na jutro", "No forecast for tomorrow"));
    lvl[0] = 0; cols[0] = th.muted;
    return 1;
  }
  const ForecastDay &t = forecast[0];
  String g = t.icon.substring(0, 2);
  String tg = todayDay.icon.substring(0, 2);
  bool badTomorrow = (g == "11" || g == "13" || (g == "10" && t.rainProb >= 50));
  bool goodToday = (tg == "01" || tg == "02" || tg == "03" || tg == "04");

  // (bez linii z nazwa ostrzezenia IMGW - ostrzezenie przed wiatrem pokazuje linia "Silny wiatr" ponizej)
  // 2. zalamanie / opady
  if (badTomorrow && goodToday) add(1, aqiColor(4), L("Załamanie pogody", "Weather turning worse"), 0);
  if (g == "11") { if (!(badTomorrow && goodToday)) add(1, aqiColor(4), L("Burze", "Thunderstorms"), 0); }
  else if (g == "13") add(1, aqiColor(3), L("Opady śniegu", "Snow"), 0);
  else if (g == "10" && t.rainProb >= 50) add(0, th.accent, L("Deszczowo (do %d%%)", "Rainy (up to %d%%)"), t.rainProb);
  else if (g == "09" && t.rainProb >= 50) add(0, th.accent, L("Mżawka (do %d%%)", "Drizzle (up to %d%%)"), t.rainProb);
  // 3. wiatr - to samo ostrzezenie co na pozostalych ekranach (IMGW albo z prognozy), w kolorze ostrzezenia,
  //    zawsze tym samym napisem: "Silny wiatr, porywy do ..." (najsilniejsze porywy jutra, a bez porywow - wiatr)
  if (t.windAlertLevel > 0) {
    float gmax = 0, wmax = 0;
    for (int h = 0; h < 24; h++) {
      if (!hourly[0][h].valid) continue;
      if (hourly[0][h].gustMs > gmax) gmax = hourly[0][h].gustMs;
      if (hourly[0][h].windMs > wmax) wmax = hourly[0][h].windMs;
    }
    bool kmh = (settingsStore.settings.windUnit == "kmh");
    uint16_t wc = alertColor(t.windAlertLevel);
    float v = (gmax > 0) ? gmax : wmax;
    int vv = (int)lroundf(kmh ? v * 3.6f : v);
    if (vv <= 0) add(1, wc, L("Silny wiatr", "Strong wind"), 0);
    else if (gmax > 0) add(1, wc, kmh ? L("Silny wiatr, porywy do %d km/h", "Strong wind, gusts to %d km/h")
                                     : L("Silny wiatr, porywy do %d m/s", "Strong wind, gusts to %d m/s"), vv);
    else add(1, wc, kmh ? L("Silny wiatr, do %d km/h", "Strong wind, up to %d km/h")
                        : L("Silny wiatr, do %d m/s", "Strong wind, up to %d m/s"), vv);
  }
  // 4. przymrozek
  if (toC(t.tempMin) <= 0.0f && todayDay.date[0] && toC(todayDay.tempMin) > 0.0f) add(0, th.accent, L("Przymrozek (min %d st.)", "Frost (low %d deg.)"), (int)lroundf(t.tempMin));
  // 5. temperatura wzgledem dzisiaj
  if (todayDay.date[0] && todayDay.tempMax > -900 && t.tempMax > -900) {
    int d = (int)lroundf(t.tempMax) - (int)lroundf(todayDay.tempMax);
    int d3 = tempIsF() ? 5 : 3;              // prog ok. 3 st. C (= 5 st. F)
    if (d >= d3) add(0, th.text, L("Cieplej niż dzisiaj (o %d st.)", "Warmer than today (by %d deg.)"), d);
    else if (d <= -d3) add(0, th.text, L("Chłodniej niż dzisiaj (o %d st.)", "Cooler than today (by %d deg.)"), -d);
    else if (n == 0) add(0, th.text, L("Podobna temperatura jak dzisiaj", "Similar temperature to today"), 0);
  }
  // 6. slonce
  if (t.sunHours >= 6 && t.rainProb < 30) add(0, th.text, L("Słonecznie, ok. %dh słońca", "Sunny, about %dh of sun"), t.sunHours);
  if (n == 0) add(0, th.text, L("Bez większych zmian pogody", "No major weather changes"), 0);
  return n;
}

// Ekran "Powietrze i odczucia" w ukladzie kart: u gory karta jakosci powietrza (polokragly wskaznik AQI + PM2,5
// i PM10 z paskami), pod nia 4 kafelki 2x2 (UV, odczuwalna, wilgotnosc, cisnienie), na dole karta "Jutro".
void drawAirScreen(const ThemeColors &th) {
  gfx->fillRect(0, 0, LCD_H_RES, LCD_V_RES, th.bg);
  const uint16_t cardCol = lerpColor565(th.bg, th.divider, 0.55f);   // tlo kart - lekko jasniejsze od tla
  char buf[64];
  auto cardBox = [&](int x, int y, int w, int h) { gfx->fillRoundRect(x, y, w, h, 10, cardCol); };
  auto label = [&](const char *t, int x, int y) {      // naglowek karty: wersaliki w kolorze akcentu
    char b[32];
    snprintf(b, sizeof(b), "%s", t);
    uppercasePL(b, sizeof(b));
    printAt(b, x, y, FONT_SMALL, th.accent, cardCol);
  };
  auto rightText = [&](const char *t, int xr, int y, const GFXfont *f, uint16_t c) {
    printAt(t, xr - textWidth(t, f), y, f, c, cardCol);
  };

  char title[32];
  snprintf(title, sizeof(title), "%s", L("Powietrze i odczucia", "Air and comfort"));
  uppercasePL(title, sizeof(title));
  printAt(title, (LCD_H_RES - textWidth(title, FONT_DAY_TITLE)) / 2, 10, FONT_DAY_TITLE, th.accent, th.bg);

  // ===== Karta: jakosc powietrza (y 40..170) =====
  cardBox(10, 40, 300, 130);
  label(L("Jakość", "Quality"), 22, 50);
  if (currentAqi > 0) {
    uint16_t col = aqiColor(currentAqi);
    // polokragly wskaznik: tlo luku + wypelnienie proporcjonalne do wartosci (0..100 = caly luk)
    const int gx = 76, gy = 136, rO = 46, rI = 37;
    float v = airExtras.aqiValue >= 0 ? airExtras.aqiValue : currentAqi * 20.0f;
    float frac = v / 100.0f;
    if (frac > 1) frac = 1;
    {
      // wygladzony: tlo luku i wypelnienie w jednym przebiegu (bez szwu na koncu wypelnienia)
      AaShape band = aaRing(gx, gy, (rO + rI) / 2.0f, (rO - rI) / 2.0f + 0.5f);
      AaLayer layers[2] = { aaLayerClip(&band, 1, th.divider, gx, gy + 0.5f, 180, 360),
                            aaLayerClip(&band, 1, col, gx, gy + 0.5f, 180, 180 + 180 * frac) };
      aaDraw(layers, frac > 0.01f ? 2 : 1, cardCol);
    }
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(v));
    printAt(buf, gx - textWidth(buf, FONT_VALUE) / 2, gy - 24, FONT_VALUE, col, cardCol);   // po srodku miedzy lukiem a napisem AQI
    printAt("AQI", gx - textWidth("AQI", FONT_SMALL) / 2, gy + 2, FONT_SMALL, th.muted, cardCol);
    // slowo + PM2,5 / PM10 z paskami (pelny pasek = poziom "zly" wg normy)
    printAt(aqiWord(currentAqi), 146, 70, FONT_DAY_TITLE, col, cardCol);
    static const float kPm25[4] = {5, 15, 50, 90};
    static const float kPm10[4] = {15, 45, 120, 195};
    struct PmRow { const char *name; float v; const float *b; } pm[2] = {
      {"PM2,5", airExtras.pm25, kPm25}, {"PM10", airExtras.pm10, kPm10}};
    for (int i = 0; i < 2; i++) {
      int y = 98 + i * 34;
      printAt(pm[i].name, 146, y, FONT_SMALL, th.muted, cardCol);
      const int bx = 146, bw = 152;
      gfx->fillRoundRect(bx, y + 18, bw, 5, 2, th.divider);
      if (!airExtras.pmValid) { rightText(L("brak", "none"), 298, y, FONT_SMALL, th.muted); continue; }
      int lv = pmLevel(pm[i].v, pm[i].b);
      snprintf(buf, sizeof(buf), "%d ug/m3", (int)lroundf(pm[i].v));
      rightText(buf, 298, y, FONT_SMALL, th.text);
      float f = pm[i].v / pm[i].b[2];
      if (f > 1) f = 1;
      int fw = (int)(bw * f);
      if (fw < 4 && pm[i].v > 0) fw = 4;
      if (fw > 0) gfx->fillRoundRect(bx, y + 18, fw, 5, 2, aqiColor(lv));
    }
  } else {
    printAt(L("Brak danych o jakości powietrza", "No air quality data"), 22, 96, FONT_SMALL, th.muted, cardCol);
  }

  // ===== 4 kafelki 2x2 (y 178..354): UV, odczuwalna, wilgotnosc, cisnienie =====
  const int tw = 146, thh = 84;
  auto tile = [&](int i, const char *lb) -> int {      // rysuje kafelek, zwraca jego x; y z indeksu
    int x = 10 + (i % 2) * 154, y = 178 + (i / 2) * 92;
    cardBox(x, y, tw, thh);
    label(lb, x + 12, y + 8);
    return x;
  };
  auto tileY = [&](int i) { return 178 + (i / 2) * 92; };
  auto subText = [&](const char *t, int x, int y) {
    char b[48];
    snprintf(b, sizeof(b), "%s", t);
    fitText(b, sizeof(b), tw - 24, FONT_SMALL);
    printAt(b, x + 12, y + 62, FONT_SMALL, th.muted, cardCol);
  };
  // 1. UV
  {
    int x = tile(0, L("Indeks UV", "UV index")), y = tileY(0);
    if (airExtras.uvValid) {
      int u = (int)lroundf(airExtras.uvNow);
      int cat = uvCategory(u);
      snprintf(buf, sizeof(buf), "%d", u);
      printAt(buf, x + 12, y + 33, FONT_VALUE, scaleColor5(cat), cardCol);
      const char *const kUvNames[5] = {L("niskie", "low"), L("umiarkowane", "medium"), L("wysokie", "high"),
                                       L("b. wysokie", "v. high"), L("ekstremalne", "extreme")};
      if (airExtras.uvMax < 0.5f) snprintf(buf, sizeof(buf), "%s", u == 0 ? L("dziś bez UV", "no UV today") : kUvNames[cat - 1]);
      else snprintf(buf, sizeof(buf), "%s, max %d", u == 0 ? L("brak", "none") : kUvNames[cat - 1], (int)lroundf(airExtras.uvMax));
      subText(buf, x, y);
    } else {
      printAt(L("brak", "none"), x + 12, y + 35, FONT_SMALL, th.muted, cardCol);
    }
  }
  // 2. odczuwalna
  {
    int x = tile(1, L("Odczuwalna", "Feels like")), y = tileY(1);
    int feel = (int)lroundf(current.feelsLike);
    snprintf(buf, sizeof(buf), "%d", feel);
    printAt(buf, x + 12, y + 33, FONT_VALUE, th.text, cardCol);
    int w = textWidth(buf, FONT_VALUE);
    {                                                         // znak stopnia (jak dwa okregi r=3 i r=2)
      AaShape deg = aaRing(x + 12 + w + 5, y + 36, 2.5f, 1.0f);
      AaLayer l = aaLayer(&deg, 1, th.text);
      aaDraw(&l, 1, cardCol);
    }
    printAt("C", x + 12 + w + 11, y + 33, FONT_VALUE, th.text, cardCol);
    char cl[24];
    snprintf(cl, sizeof(cl), "%s", comfortLabel(toC(current.feelsLike)));
    cl[0] = (char)tolower((unsigned char)cl[0]);        // etykiety zaczynaja sie od litery ASCII
    subText(cl, x, y);                           // np. "zimno"
  }
  // 3. wilgotnosc
  {
    int x = tile(2, L("Wilgotność", "Humidity")), y = tileY(2);
    snprintf(buf, sizeof(buf), "%d%%", current.humidity);
    printAt(buf, x + 12, y + 33, FONT_VALUE, th.text, cardCol);
    subText(humidityWord(current.humidity, toC(current.temp)), x, y);
  }
  // 4. cisnienie (lokalny czujnik albo API) z trendem - jak w dolnej linii ekranu glownego
  {
    int x = tile(3, L("Ciśnienie", "Pressure")), y = tileY(3);
    float pr = bmeReady ? localPressure : current.pressure;
    int8_t tr = bmeReady ? pressureTrend() : apiPressureTrend();
    if (current.hasPressTrend) tr = current.pressTrend;
    if (pr > 0) {
      snprintf(buf, sizeof(buf), "%.0f", pr);
      printAt(buf, x + 12, y + 33, FONT_VALUE, th.text, cardCol);
      int w = textWidth(buf, FONT_VALUE);
      if (tr != 0) drawArrowTri(x + 12 + w + 12, y + 37, tr > 0, th.accent, cardCol);
      subText(tr > 0 ? L("hPa, rośnie", "hPa, rising") : tr < 0 ? L("hPa, spada", "hPa, falling") : L("hPa, stabilne", "hPa, steady"), x, y);
    } else {
      printAt(L("brak", "none"), x + 12, y + 35, FONT_SMALL, th.muted, cardCol);
    }
  }

  // ===== Karta: jutro (y 362..470) =====
  cardBox(10, 362, 300, 108);
  label(L("Jutro", "Tomorrow"), 22, 372);
  char lines[3][56];
  uint8_t lvl[3];
  uint16_t cols[3];
  int n = buildTomorrowLines(lines, lvl, cols, th);
  for (int i = 0; i < n; i++) {
    int y = 397 + i * 24;
    uint16_t dc = (cols[i] == th.muted) ? th.muted : (lvl[i] ? cols[i] : th.accent);
    aaFillCircle(26, y + 7, 4, dc, cardCol);
    char lb[56];
    snprintf(lb, sizeof(lb), "%s", lines[i]);
    fitText(lb, sizeof(lb), 298 - 40, FONT_SMALL);
    printAt(lb, 40, y, FONT_SMALL, cols[i], cardCol);
  }
}

// (ScreenMode / screenMode zdefiniowane wyzej, przy trybie oszczedzania energii)
uint8_t detailDay = 0;
char detailDate[11] = "";          // data otwartego dnia (indeksy moga sie przesunac po polnocy)
unsigned long detailShownAt = 0;
bool wasTouched = false;
unsigned long lastTapMs = 0;
// Wymuszone pobranie pogody o polnocy: do tego czasu dane "dzisiaj" sa wczorajsze i blok aktualnej
// pogody nie otwiera "Przegladu dnia". Gdy pobranie sie nie uda - ponowienie co 2 min, az dane beda z nowej doby.
bool fetchRequested = false;

// Surowe wspolrzedne kontrolera dotyku -> piksele ekranu. Domyslnie 1:1
// (kontroler raportuje w ukladzie panelu 320x480). Jesli dotkniecie trafia
// "obok" (np. odwrocone), zmien te flagi - surowe i przeliczone wspolrzedne
// sa wypisywane w Serial Monitorze przy kazdym dotknieciu.
static const bool TOUCH_SWAP_XY = false;
static const bool TOUCH_FLIP_X = false;
static const bool TOUCH_FLIP_Y = false;

void mapTouch(int rawX, int rawY, int &x, int &y) {
  x = TOUCH_SWAP_XY ? rawY : rawX;
  y = TOUCH_SWAP_XY ? rawX : rawY;
  if (TOUCH_FLIP_X) x = LCD_H_RES - 1 - x;
  if (TOUCH_FLIP_Y) y = LCD_V_RES - 1 - y;
}

// Czy dane "dzisiaj" z API dotycza biezacej daty lokalnej? Zaraz po polnocy, do
// najblizszego odswiezenia pogody (max 10 min), jeszcze nie - wtedy nie otwieramy
// ekranu, zeby nie pokazac wczorajszych godzin jako dzisiejszych.
bool todayDataCurrent() {
  if (!todayDay.date[0]) return false;
  struct tm ti;
  if (!getLocalTime(&ti, 50)) return true;   // brak czasu - nie blokujemy
  char cur[11];
  snprintf(cur, sizeof(cur), "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  return strncmp(cur, todayDay.date, 10) == 0;
}

void openDayDetail(uint8_t d) {
  bool isToday = (d == DAY_TODAY);
  if (isToday ? !todayDataCurrent() : d >= forecastCount) return;
  screenMode = SCREEN_DETAIL;
  detailDay = d;
  strncpy(detailDate, isToday ? todayDay.date : forecast[d].date, 10);
  detailDate[10] = 0;
  detailShownAt = millis();
  drawDayDetail(currentTheme(), d);
}

void closeDayDetail() {
  screenMode = SCREEN_MAIN;
  redrawAll();
}

void openInfoScreen() {
  screenMode = SCREEN_INFO;
  infoArmedBtn = -1;
  detailShownAt = millis();        // ten sam samoczynny powrot co przy szczegolach dnia
  drawInfoScreen(currentTheme());
}

void openAirScreen() {
  screenMode = SCREEN_AIR;
  detailShownAt = millis();
  drawAirScreen(currentTheme());
}

void openOutlookScreen() {
  if (!todayDataCurrent()) return;   // zaraz po polnocy (do wymuszonego pobrania) dane "dzisiaj" sa jeszcze wczorajsze
  screenMode = SCREEN_OUTLOOK;
  detailShownAt = millis();          // ten sam samoczynny powrot co przy szczegolach dnia
  drawOutlookScreen(currentTheme());
}

// Po odswiezeniu pogody w trakcie ogladania szczegolow: odswiez tabele, a jesli
// otwarty dzien zniknal z prognozy (np. po polnocy) - wroc na ekran glowny.
void refreshDayDetailAfterFetch() {
  if (todayDay.date[0] && strncmp(todayDay.date, detailDate, 10) == 0) {
    detailDay = DAY_TODAY;
    drawDayDetail(currentTheme(), DAY_TODAY);
    return;
  }
  for (uint8_t i = 0; i < forecastCount; i++) {
    if (strncmp(forecast[i].date, detailDate, 10) == 0) {
      detailDay = i;
      drawDayDetail(currentTheme(), i);
      return;
    }
  }
  closeDayDetail();
}

void handleTap(int x, int y) {
  if (screenMode == SCREEN_OUTLOOK) {   // "Przeglad dnia": kolejne dotkniecie -> godzinowka na dzis
    openDayDetail(DAY_TODAY);           // (jesli dane dzisiejsze nieaktualne - nic sie nie zmieni, wiec wracamy)
    if (screenMode != SCREEN_DETAIL) closeDayDetail();
    return;
  }
  if (screenMode == SCREEN_INFO && infoButtonAt(x, y) >= 0) {   // Restart / Tryb AP (dwa dotkniecia)
    int i = infoButtonAt(x, y);
    if (infoBtnArmed(i)) doRestart(currentTheme(), i == 1);
    int prev = infoArmedBtn;
    infoArmedBtn = i;
    infoArmedAt = millis();
    detailShownAt = millis();          // nie wracaj samoczynnie w trakcie potwierdzania
    if (prev >= 0 && prev != i) drawInfoButton(prev, currentTheme());   // drugi przycisk wraca do normalnego wygladu
    drawInfoButton(i, currentTheme());
    return;
  }
  if (screenMode != SCREEN_MAIN) {   // szczegoly dnia / informacje: dotkniecie gdziekolwiek = powrot
    closeDayDetail();
    return;
  }
  // kreski zasiegu w prawym gornym rogu (z zapasem na palec) -> ekran informacyjny
  if (x >= 240 && y < 56) {
    openInfoScreen();
    return;
  }
  // ekran glowny: blok aktualnej pogody (miedzy kreska pod zegarem a kreska nad
  // prognoza) -> ekran "Przeglad dnia" (opady 24 h, UV, Ksiezyc, pylki, slonce)
  if (y >= 94 && y < 204) {
    openOutlookScreen();
    return;
  }
  // pasek komfortu (pod prognoza) -> ekran "Powietrze i odczucia"
  if (y > 396) {
    openAirScreen();
    return;
  }
  // kolumny prognozy (miedzy kreska pod pogoda a paskiem komfortu)
  if (forecastCount > 0 && y >= 204 && y <= 396) {
    int colWidth = LCD_H_RES / forecastCount;
    int col = x / colWidth;
    if (col >= forecastCount) col = forecastCount - 1;
    if (col < 0) col = 0;
    openDayDetail(col);
  }
}

// ---------------------------------------------------------------------
//  Przyciemnianie nocne
// ---------------------------------------------------------------------
int minutesSinceMidnight(int h, int m) { return h * 60 + m; }

bool isNightNow() {
  AppSettings &s = settingsStore.settings;
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 50)) return false;

  int nowMin = minutesSinceMidnight(timeInfo.tm_hour, timeInfo.tm_min);
  int startH, startM, endH, endM;
  sscanf(s.nightStart.c_str(), "%d:%d", &startH, &startM);
  sscanf(s.nightEnd.c_str(), "%d:%d", &endH, &endM);
  int startMin = minutesSinceMidnight(startH, startM);
  int endMin = minutesSinceMidnight(endH, endM);

  if (startMin <= endMin) {
    return (nowMin >= startMin && nowMin < endMin);
  }
  return (nowMin >= startMin || nowMin < endMin);
}

// Dotyk w nocy tymczasowo rozjasnia ekran - dopoki boost trwa, nie
// nadpisujemy jasnosci normalnym harmonogramem.
unsigned long touchBoostUntil = 0;
static const unsigned long TOUCH_BOOST_MS = 8000;

void applyBacklightSchedule() {
  if (touchBoostUntil != 0 && millis() < touchBoostUntil) return;

  AppSettings &s = settingsStore.settings;
  uint8_t pct = isNightNow() ? s.brightnessNight : s.brightnessDay;
  if (onBattery && pct > s.brightnessBattery) pct = s.brightnessBattery;   // oszczedzanie na baterii
  setBacklightPercent(pct);
}

// ---------------------------------------------------------------------
//  setup / loop
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  settingsStore.begin();
  g_lang = settingsStore.settings.lang;          // jezyk ekranu i panelu (Lang.h)

  Wire.begin(NEXTION_IIC_SDA_PIN, NEXTION_IIC_SCL_PIN, 400000);
  lcdResetByExpander();
  analogReadResolution(12);   // ADC baterii (GPIO4)

  updateBattery();            // pierwszy pomiar baterii - ikona od razu przy pierwszym rysowaniu
  lastBatteryRead = millis();

  if (!gfx->begin(LCD_SPI_FREQ)) {
    Serial.println("Blad inicjalizacji ekranu");
  }
  gfx->setRotation(0);
  initBacklight();

  touchReady = touch.begin(&Wire, CST826_ADDR);
  {
    // Znany quirk: Adafruit_CST8XX.begin() czasem zwraca false mimo ze
    // czujnik fizycznie dziala - sprawdzamy ID rejestru bezposrednio,
    // tak jak w oficjalnym przykladzie 01_touch_test.
    Wire.beginTransmission(CST826_ADDR);
    Wire.write((uint8_t)0xAA);
    uint8_t touchId = 0xFF;
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(CST826_ADDR, (uint8_t)1) == 1) {
      touchId = Wire.read();
    }
    Serial.printf("CST826 ID: 0x%02X, begin()=%d\n", touchId, touchReady);
    if (!touchReady) {
      Serial.println("CST826: wymuszam touchReady=true (znany quirk biblioteki)");
      touchReady = true;
    }
  }
  initSensor();

#if PL_FONT_TEST
  plFontTest();
  delay(30000);
#endif

  // "Przejdź w tryb AP" z panelu WWW: jednorazowy znacznik w pamieci - od razu siec StacjaPogody-Setup
  bool forceAp = false;
  {
    Preferences p;
    p.begin("wclock", false);
    forceAp = p.getBool("forceap", false);
    if (forceAp) p.remove("forceap");
    p.end();
  }

  bool connected = false;
  if (forceAp) {
    Serial.println("WiFi: tryb konfiguracji na zadanie z panelu WWW");
  } else {
    drawConnectingScreen();
    bootConnectUi = true;
    connected = connectWiFi();
    bootConnectUi = false;
  }
  if (!connected) {
    startApMode();
  } else {
    apMode = false;
    configTzTime(settingsStore.settings.tz.c_str(), "pool.ntp.org", "time.google.com");
    struct tm t;
    getLocalTime(&t, 5000);

    drawConnectedScreen();
    delay(2000);

    fetchWeather();      // geokodowanie miasta odbywa sie w srodku (leniwie)
    fetchAirQuality();
    sampleSensor();
    lastWeatherFetch = millis();
    lastPressureSample = millis();
  }

  webPanel.begin(&settingsStore, apMode, weatherStatus);

  if (!apMode) {
    onBattery = false;
    applyPowerMode(batteryPowered());   // na baterii: po pierwszym pobraniu Wi-Fi gasnie
    redrawAll();
    applyBacklightSchedule();
  }
}

// mDNS: <nazwa>.local wskazuje na stacje. Uruchamiany przy kazdym (ponownym) polaczeniu z siecia
// i wylaczany po jego utracie - na baterii Wi-Fi jest co chwile wylaczane i wlaczane.
void mdnsService() {
  static bool mdnsUp = false;
  bool conn = !apMode && WiFi.status() == WL_CONNECTED;
  if (conn && !mdnsUp) {
    MDNS.end();
    if (MDNS.begin(settingsStore.settings.hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS: http://%s.local\n", settingsStore.settings.hostname.c_str());
    } else {
      Serial.println("mDNS: blad uruchomienia");
    }
    mdnsUp = true;                       // nawet przy bledzie - nie probujemy w kazdym obiegu petli
  } else if (!conn && mdnsUp) {
    MDNS.end();
    mdnsUp = false;
  }
}

void loop() {
  if (webPanel.rebootRequested()) {
    delay(300);
    ESP.restart();
  }
  mdnsService();

  if (apMode) {
    delay(50);
    return;
  }

  unsigned long now = millis();

  // Dotyk - stan odczytujemy raz na obieg petli:
  //  1) w nocy dotkniecie tymczasowo rozjasnia ekran na kilka sekund,
  //  2) POCZATEK dotkniecia = "tap": otwiera szczegoly dnia / wraca na glowny.
  //     400 ms odstepu miedzy tapami eliminuje drgania (podwojne zliczenia).
  bool touchedNow = touchReady && touch.touched();
  if (touchedNow && isNightNow()) {
    setBacklightPercent(onBattery ? settingsStore.settings.brightnessBattery : settingsStore.settings.brightnessDay);
    touchBoostUntil = now + TOUCH_BOOST_MS;
  }
  if (touchedNow && !wasTouched && now - lastTapMs > 400) {
    lastTapMs = now;
    CST_TS_Point p = touch.getPoint(0);
    int tapX, tapY;
    mapTouch(p.x, p.y, tapX, tapY);
    Serial.printf("Dotyk: raw=(%d,%d) -> ekran=(%d,%d)\n", (int)p.x, (int)p.y, tapX, tapY);
    handleTap(tapX, tapY);
  }
  wasTouched = touchedNow;
  // UWAGA: tu musi byc swiezy millis(), a nie 'now' z poczatku petli - openDayDetail()
  // ustawia detailShownAt PO tym 'now', wiec (now - detailShownAt) na liczbach bez
  // znaku dawaloby ogromna wartosc i ekran zamykalby sie natychmiast po otwarciu.
  if (screenMode != SCREEN_MAIN && millis() - detailShownAt >= DETAIL_TIMEOUT_MS) closeDayDetail();
  if (screenMode == SCREEN_INFO && infoArmedBtn >= 0 && !infoBtnArmed(infoArmedBtn)) {   // minal czas na potwierdzenie
    int i = infoArmedBtn;
    infoArmedBtn = -1;
    drawInfoButton(i, currentTheme());
  }
  // po uplywie czasu boostu wracamy do normalnego harmonogramu
  if (touchBoostUntil != 0 && now >= touchBoostUntil) {
    touchBoostUntil = 0;
    applyBacklightSchedule();
  }

  if (now - lastBatteryRead >= BATTERY_READ_MS) {
    lastBatteryRead = now;
    if (updateBattery() && screenMode == SCREEN_MAIN) drawBatteryIcon(currentTheme());
    // zmiana trybu zasilania dopiero po 2 zgodnych pomiarach (ok. 4 s)
    bool wantBat = batteryPowered();
    if (wantBat != onBattery) {
      if (++powerModeVotes >= 2) { powerModeVotes = 0; applyPowerMode(wantBat); }
    } else {
      powerModeVotes = 0;
    }
  }

  // niski poziom baterii - migajace czerwone wypelnienie ikony
  {
    static unsigned long lastBlink = 0;
    if (batteryLowAlarm() && screenMode == SCREEN_MAIN) {
      if (now - lastBlink >= BATTERY_BLINK_MS) {
        lastBlink = now;
        batteryBlinkOff = !batteryBlinkOff;
        drawBatteryIcon(currentTheme());
      }
    } else {
      batteryBlinkOff = false;
    }
  }

  // na zasilaczu: po utracie polaczenia co 2 min szukamy dostepnej sieci z listy (glowna / zapasowe).
  // Tylko sieci widoczne w skanie - zeby nie blokowac ekranu dlugimi probami "na slepo".
  {
    // opoznione wylaczenie Wi-Fi po przejsciu na baterie
    if (onBattery && wifiOffAt && (long)(now - wifiOffAt) >= 0) {
      wifiOffAt = 0;
      wifiPowerOff();
      if (screenMode == SCREEN_MAIN) drawWifiBars(currentTheme());
    }
    // bez blokowania: po co najmniej 15 s bez polaczenia proba (co 20 s) kolejnej sieci z listy
    static bool wasConnected = true;
    static unsigned long lostAt = 0;
    bool conn = (WiFi.status() == WL_CONNECTED);
    static uint8_t wifiRetries = 0;              // 1. proba - ta sama siec, kolejne - nastepne z listy
    if (conn) { lostAt = 0; wifiRetries = 0; } else if (!lostAt) lostAt = now;
    if (!onBattery && !apMode && !conn && lostAt && now - lostAt >= 15000UL && now - wifiBeginAt >= 20000UL &&
        !Update.isRunning()) {
      wifiBeginNet(wifiRetries == 0 ? wifiNetIdx : wifiNextNet(wifiNetIdx));
      if (wifiRetries < 250) wifiRetries++;
    }
    // przerysowanie kresek tylko przy ZMIANIE stanu polaczenia (wczesniej na baterii warunek byl spelniony
    // w kazdym obiegu petli - kreski migaly)
    if (conn != wasConnected && screenMode == SCREEN_MAIN) drawWifiBars(currentTheme());
    wasConnected = conn;
  }

  if (now - lastClockTick >= 60000) {
    lastClockTick = now;
    if (screenMode == SCREEN_MAIN) {   // na ekranie szczegolow nie rysujemy po wierzchu
      ThemeColors th = currentTheme();
      drawClock(th);
      drawWifiBars(th);
      // przypomnienie o wywozie smieci pojawia sie / znika o polnocy i w poludnie - wtedy przerysowujemy pasek komfortu
      char w[72] = "";
      bool isToday;
      if (!buildWasteReminder(w, sizeof(w), isToday)) w[0] = 0;
      if (strcmp(w, wasteShown) != 0) drawComfortBar(th);
    }
    applyBacklightSchedule();
    // 00:00 - nowa doba, a dane "dzisiaj" wciaz z wczoraj: wymuszone pobranie (takze na baterii)
    if (todayDay.date[0] && !todayDataCurrent()) fetchRequested = true;
  }

  unsigned long fetchInterval = onBattery ? (unsigned long)settingsStore.settings.batteryFetchMin * 60000UL
                                         : (unsigned long)settingsStore.settings.fetchMin * 60000UL;   // panel WWW, 10..60 min
  // Na baterii Wi-Fi jest wylaczone: najpierw laczenie W TLE (petla i dotyk dzialaja), a pobranie dopiero
  // po polaczeniu; co 10 s proba kolejnej sieci, po 30 s rezygnacja do nastepnego terminu.
  static bool battNetWait = false;
  static unsigned long battNetStart = 0;
  bool doFetch = false, netOk = true;
  if (!onBattery) battNetWait = false;
  // wymuszone pobranie (polnoc) nie czesciej niz co 2 min od poprzedniego - przy bledzie nie mielimy w kolko
  bool dueNow = (now - lastWeatherFetch >= fetchInterval) ||
                (fetchRequested && now - lastWeatherFetch >= 120000UL);
  if (onBattery && !battNetWait && dueNow) {
    battNetWait = true;
    battNetStart = now;
    if (WiFi.status() != WL_CONNECTED) wifiBeginNet(wifiNetIdx);   // jeszcze polaczone (zaraz po zaniku) - nie ruszamy
  }
  if (battNetWait) {
    if (WiFi.status() == WL_CONNECTED) { battNetWait = false; doFetch = true; }
    else if (now - battNetStart >= 30000UL) { battNetWait = false; doFetch = true; netOk = false; }
    else if (now - wifiBeginAt >= 10000UL) wifiBeginNet(wifiNextNet(wifiNetIdx));
  } else if (!onBattery && dueNow) {
    doFetch = true;
  }
  if (doFetch) {
    lastWeatherFetch = now;
    if (onBattery && netOk)                  // przy okazji synchronizacja zegara z NTP
      configTzTime(settingsStore.settings.tz.c_str(), "pool.ntp.org", "time.google.com");
    if (onBattery && screenMode == SCREEN_MAIN) drawWifiBars(currentTheme());
    bool weatherOk = netOk && fetchWeather();
    if (weatherOk) {
      fetchRequested = false;
      Serial.printf("Pogoda pobrana, wolna pamiec: %u B (najwiekszy blok %u B)\n",
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    } else {
      Serial.printf("Pogoda: blad pobierania, wolna pamiec: %u B (najwiekszy blok %u B)\n",
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }
    if (weatherOk) {
      if (screenMode == SCREEN_MAIN) {
        ThemeColors th = currentTheme();
        drawCurrentWeather(th);
        drawForecast(th);
        drawComfortBar(th);
      } else if (screenMode == SCREEN_DETAIL) {
        refreshDayDetailAfterFetch();
      } else if (screenMode == SCREEN_OUTLOOK) {
        drawOutlookScreen(currentTheme());   // swieza krzywa i dane po pobraniu
      } else if (screenMode == SCREEN_AIR) {
        drawAirScreen(currentTheme());
      } else {
        drawInfoScreen(currentTheme());   // swieze godziny po pobraniu danych
      }
    }
    if (netOk && fetchAirQuality()) {
      if (screenMode == SCREEN_MAIN) {
        ThemeColors th = currentTheme();
        drawCurrentWeather(th);
      } else if (screenMode == SCREEN_OUTLOOK) {
        drawOutlookScreen(currentTheme());   // nowe UV i pylki
      } else if (screenMode == SCREEN_AIR) {
        drawAirScreen(currentTheme());       // nowe AQI, PM2,5 i PM10
      }
    }
    if (onBattery) {                         // po pobraniu Wi-Fi z powrotem wylaczone
      wifiOffAt = 0;
      wifiPowerOff();
      if (screenMode == SCREEN_MAIN) drawWifiBars(currentTheme());
    }
  }

  if (now - lastPressureSample >= PRESSURE_SAMPLE_MS) {
    lastPressureSample = now;
    sampleSensor();
    if (screenMode == SCREEN_MAIN) {
      ThemeColors th = currentTheme();
      drawCurrentWeather(th);
      drawComfortBar(th);
    }
  }

  delay(20);
}
