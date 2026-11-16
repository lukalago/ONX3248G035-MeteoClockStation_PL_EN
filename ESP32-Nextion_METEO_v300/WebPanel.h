#pragma once
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "Settings.h"
#include "Lang.h"        // jezyk panelu: L("polski", "English")
#include <Preferences.h>

class WebPanel {
public:
  // status: wskaznik na bufor tekstowy aktualizowany przez szkic (np. skad
  // pochodza pomiary) - tylko do odczytu, pokazywany w panelu.
  // status2: drugi bufor (np. godzina ostatniego rozladowania baterii) - pokazywany pod statusem.
  void begin(SettingsStore *store, bool apMode, const char *status = nullptr, const char *status2 = nullptr) {
    _store = store;
    _status = status;
    _status2 = status2;
    _apMode = apMode;
    _server = new AsyncWebServer(80);

    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
      request->send(200, "text/html; charset=utf-8", buildPage());
    });

    _server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
      handleSave(request);
    });

    // Przejscie w tryb konfiguracji (AP): znacznik "forceap" w pamieci + restart. Szkic przy starcie
    // odczytuje znacznik, kasuje go (jednorazowy) i od razu uruchamia siec StacjaPogody-Setup zamiast
    // laczyc sie z zapisanymi sieciami. Zapisane sieci i ustawienia zostaja.
    _server->on("/apmode", HTTP_POST, [this](AsyncWebServerRequest *request) {
      Preferences p;
      p.begin("wclock", false);
      p.putBool("forceap", true);
      p.end();
      String page = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head>";
      page += "<body style='background:#000;color:#eee;font-family:sans-serif;padding:20px'>";
      page += String("<h3 style='color:#07c8d8'>") + L("Stacja przechodzi w tryb konfiguracji...", "The station is switching to setup mode...") + "</h3>";
      page += L("<p>Połącz się z siecią Wi-Fi <b>StacjaPogody-Setup</b> (kod QR na ekranie stacji) i otwórz <b>http://192.168.4.1</b>.</p>",
                "<p>Join the Wi-Fi network <b>StacjaPogody-Setup</b> (QR code on the station screen) and open <b>http://192.168.4.1</b>.</p>");
      page += L("<p style='color:#8a8a8a'>Zapisane sieci i ustawienia zostają. Po zapisie w panelu albo po wyłączeniu i włączeniu zasilania stacja wróci do zwykłej pracy.</p>",
                "<p style='color:#8a8a8a'>Saved networks and settings are kept. After saving in the panel, or after switching the power off and on, the station returns to normal operation.</p>");
      page += "</body></html>";
      request->send(200, "text/html; charset=utf-8", page);
      _rebootRequested = true;
    });

    // Sam restart (przycisk "Tylko restart" w panelu) - bez zapisu ustawien
    _server->on("/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
      String page = String("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
      page += "<meta http-equiv='refresh' content='12;url=/'></head>";
      page += "<body style='background:#000;color:#eee;font-family:sans-serif;padding:20px;text-align:center'>";
      page += String("<h3 style='color:#50c864'>") + L("Restart stacji...", "Restarting the station...") + "</h3>";
      page += String("<p style='color:#8a8a8a'>") + L("Strona odświeży się sama za kilkanaście sekund.", "This page will reload in a few seconds.") + "</p>";
      page += "</body></html>";
      request->send(200, "text/html; charset=utf-8", page);
      _rebootRequested = true;
    });

    // OTA: endpoint przyjmujacy plik .bin (Sketch -> Export Compiled Binary
    // w Arduino IDE). Pierwszy argument to handler koncowej odpowiedzi,
    // drugi (onBody/onUpload) to strumieniowe zapisywanie przychodzacych
    // danych pliku wprost do partycji OTA, kawalek po kawalku.
    _server->on("/update", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        bool ok = !Update.hasError();
        String msg = ok ? L("Aktualizacja OK. Urządzenie zrestartuje się za chwilę...", "Update OK. The station will restart in a moment...")
                        : L("Błąd aktualizacji - sprawdź plik .bin i spróbuj ponownie.", "Update failed - check the .bin file and try again.");
        AsyncWebServerResponse *response = request->beginResponse(ok ? 200 : 500, "text/plain; charset=utf-8", msg);
        response->addHeader("Connection", "close");
        request->send(response);
        if (ok) _otaRebootPending = true;
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (index == 0) {
          Serial.printf("OTA start: %s\n", filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        }
        if (len) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
          }
        }
        if (final) {
          if (Update.end(true)) {
            Serial.printf("OTA OK: %u B\n", (unsigned)(index + len));
          } else {
            Update.printError(Serial);
          }
        }
      });

    _server->begin();
  }

  bool rebootRequested() { return _rebootRequested || _otaRebootPending; }

private:
  AsyncWebServer *_server = nullptr;
  SettingsStore *_store = nullptr;
  bool _apMode = false;
  bool _rebootRequested = false;
  bool _otaRebootPending = false;
  const char *_status = nullptr;
  const char *_status2 = nullptr;

  String opt(const String &value, const String &current) {
    return value == current ? " selected" : "";
  }

  String checked(bool v) { return v ? " checked" : ""; }

  // Przelacznik segmentowy: grupa przyciskow radio (ta sama nazwa pola i wartosci co dawna lista
  // rozwijana, wiec zapis ustawien dziala bez zmian). opts = {wartosc, etykieta, wartosc, etykieta, ...}.
  String seg(const char *name, const char *id, const char *const *opts, int n, const String &cur, bool enabled = true) {
    String h = "<div class='seg" + String(enabled ? "" : " off") + "' id='" + id + "'>";
    for (int i = 0; i < n; i++) {
      h += "<label><input type='radio' name='" + String(name) + "' value='" + opts[2 * i] + "'";
      h += checked(cur == opts[2 * i]);
      h += enabled ? "" : " disabled";
      h += "><span>" + String(opts[2 * i + 1]) + "</span></label>";
    }
    return h + "</div>";
  }

  // Jak seg(), ale wyszarza wybrane opcje (bit i w disabledMask = opcja i niedostepna)
  String segMask(const char *name, const char *id, const char *const *opts, int n, const String &cur, uint8_t disabledMask) {
    String h = "<div class='seg' id='" + String(id) + "'>";
    for (int i = 0; i < n; i++) {
      bool off = (disabledMask >> i) & 1;
      h += "<label><input type='radio' name='" + String(name) + "' value='" + opts[2 * i] + "'";
      h += checked(cur == opts[2 * i]);
      h += off ? " disabled" : "";
      h += "><span>" + String(opts[2 * i + 1]) + "</span></label>";
    }
    return h + "</div>";
  }

  // Zwijany opis "Co to znaczy?" - zeby panel nie byl scianom tekstu
  String help(const String &text) {
    return String("<details><summary>") + L("Co to znaczy?", "What does this mean?") + "</summary><div class='hint'>" + text + "</div></details>";
  }

  String htmlEscape(const String &in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      if (c == '&') out += "&amp;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else if (c == '\'') out += "&#39;";
      else if (c == '"') out += "&quot;";
      else out += c;
    }
    return out;
  }

  // Ciemny motyw dopasowany do kolorow ekranu urzadzenia (Klasyczny:
  // czarne tlo, biale napisy, cyjanowy akcent, szary wyciszony/separator).
  String darkStyle() {
    String css;
    css += "body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#000;color:#fff}";
    css += "h2{margin-top:0;text-align:center;color:#fff;font-weight:600}";
    css += "fieldset{background:#151515;border:1px solid #2a2a2a;border-radius:10px;margin-bottom:14px;padding:12px}";
    css += "legend{font-weight:bold;padding:0 6px;color:#07c8d8}";
    css += "label{display:block;font-size:13px;color:#8a8a8a;margin:8px 0 3px}";
    css += "input,select{width:100%;box-sizing:border-box;padding:7px;background:#1e1e1e;border:1px solid #333;color:#fff;border-radius:6px;font-size:14px}";
    css += "select:disabled,input:disabled{opacity:.4;cursor:not-allowed}";
    css += "input[type=file]{color:#8a8a8a}";
    css += "input[type=checkbox]{width:auto}";
    // przelaczniki segmentowe (zamiast list rozwijanych dla wyboru z 2 opcji) i suwak wlacz/wylacz
    css += ".seg{display:flex;background:#1e1e1e;border:1px solid #333;border-radius:8px;overflow:hidden}";
    css += ".seg label{flex:1;margin:0;position:relative;color:#8a8a8a;font-size:14px}";
    css += ".seg input{position:absolute;opacity:0;width:0;height:0;margin:0}";
    css += ".seg span{display:block;text-align:center;padding:9px 4px;cursor:pointer;border-left:1px solid #333}";
    css += ".seg label:first-child span{border-left:none}";
    css += ".seg input:checked+span{background:#07c8d8;color:#000;font-weight:bold}";
    css += ".seg.off{opacity:.4}.seg.off span{cursor:not-allowed}";
    css += ".seg input:disabled+span{opacity:.35;cursor:not-allowed}";
    css += ".sw{display:flex;align-items:center;gap:10px;color:#fff;font-size:14px;margin:8px 0}";
    css += ".sw input{display:none}";
    css += ".trk{width:44px;height:24px;background:#333;border-radius:12px;position:relative;flex:none;transition:background .15s}";
    css += ".trk:after{content:'';position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;background:#aaa;transition:left .15s}";
    css += ".sw input:checked+.trk{background:#07c8d8}.sw input:checked+.trk:after{left:23px;background:#000}";
    css += "details{margin-top:6px}summary{font-size:12px;color:#07c8d8;cursor:pointer}";
    css += "details .hint{margin:4px 0 0;font-size:12px;line-height:1.4}";
    css += ".sub{font-size:12px;color:#07c8d8;letter-spacing:.04em;text-transform:uppercase;margin:16px 0 0;padding-top:12px;border-top:1px solid #2a2a2a}";
    css += ".sub:first-of-type{border-top:none;padding-top:0;margin-top:4px}";
    css += "button{width:100%;padding:12px;background:#07c8d8;color:#000;border:none;border-radius:8px;font-size:15px;font-weight:bold;margin-top:10px}";
    // raczka i lekkie rozjasnienie po najechaniu - widac, ze przycisk jest aktywny (dotyczy wszystkich przyciskow)
    css += "button,summary,.seg span,.sw{cursor:pointer}";
    css += "button{transition:filter .12s,transform .05s}button:hover{filter:brightness(1.15)}button:active{transform:scale(.97)}";
    css += "a{color:#07c8d8}";
    css += ".row{display:flex;gap:10px}.row>div{flex:1}";
    css += ".hint{font-size:11px;color:#6a6a6a;margin-top:3px}";
    css += "p{color:#8a8a8a}";
    return css;
  }

  // Skanuje dostepne sieci WiFi i zwraca gotowy <select> z SSID,
  // posortowany po sile sygnalu, z aktualnie zapisana siecia zaznaczona.
  String buildSsidSelect(const String &currentSsid) {
    int n = WiFi.scanNetworks();
    String html = "<select name='ssid' id='ssidSelect' onchange=\"document.getElementById('ssidManual').value=''\">";

    if (n <= 0) {
      html += String("<option value=''>") + L("(brak wykrytych sieci - wpisz ręcznie poniżej)", "(no networks found - type one in below)") + "</option>";
    } else {
      // proste sortowanie babelkowe po RSSI malejaco (n jest zazwyczaj male, <30)
      int idx[64];
      int count = min(n, 64);
      for (int i = 0; i < count; i++) idx[i] = i;
      for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
          if (WiFi.RSSI(idx[j]) < WiFi.RSSI(idx[j + 1])) {
            int tmp = idx[j]; idx[j] = idx[j + 1]; idx[j + 1] = tmp;
          }
        }
      }
      html += String("<option value=''>") + L("-- wybierz sieć --", "-- choose a network --") + "</option>";
      String seen[64];
      int seenCount = 0;
      for (int i = 0; i < count; i++) {
        String ssid = WiFi.SSID(idx[i]);
        if (ssid.isEmpty()) continue;
        bool dup = false;
        for (int k = 0; k < seenCount; k++) if (seen[k] == ssid) { dup = true; break; }
        if (dup) continue;
        seen[seenCount++] = ssid;

        int rssi = WiFi.RSSI(idx[i]);
        String bars = rssi > -60 ? "****" : (rssi > -70 ? "*** " : (rssi > -80 ? "**  " : "*   "));
        bool locked = WiFi.encryptionType(idx[i]) != WIFI_AUTH_OPEN;
        html += "<option value='" + ssid + "'" + opt(ssid, currentSsid) + ">";
        html += ssid + "  [" + bars + "]" + (locked ? L(" (zabezpieczona)", " (secured)") : L(" (otwarta)", " (open)"));
        html += "</option>";
      }
    }
    html += "</select>";
    return html;
  }

  String buildPage() {
    AppSettings &s = _store->settings;

    String html;
    html.reserve(7000);
    html += String("<!DOCTYPE html><html lang='") + L("pl", "en") + "'><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += String("<title>") + L("Stacja pogody - ustawienia", "Weather station - settings") + "</title>";
    html += "<style>" + darkStyle() + "</style></head><body>";
#ifdef FW_VERSION
    html += String("<h2>") + L("Stacja pogody", "Weather station") + " v" FW_VERSION "</h2>";   // wersja z szkicu (#define FW_VERSION w pliku .ino)
#else
    html += String("<h2>") + L("Stacja pogody", "Weather station") + "</h2>";
#endif
    if (_apMode) {
      html += L("<p><b>Tryb konfiguracji.</b> Wybierz WiFi i wpisz hasło - po zapisie stacja zrestartuje się i połączy z siecią.</p>",
                "<p><b>Setup mode.</b> Choose your Wi-Fi and enter the password - after saving, the station restarts and connects to the network.</p>");
    }
    html += "<form method='POST' action='/save'>";

    // jezyk ekranu i panelu - na samej gorze, zeby latwo go bylo znalezc w obu wersjach
    {
      const char *const kLangOpts[] = {"0", "Polski", "1", "English"};
      html += "<fieldset><legend>Język / Language</legend>";
      html += seg("lang", "lang", kLangOpts, 2, String(s.lang));
      html += help(L("Język napisów na ekranie stacji i w tym panelu. Zmiana działa po zapisie (restart). Ostrzeżenia i stacje IMGW są dostępne tylko po polsku.",
                     "Language of the station screen and this panel. Takes effect after saving (restart). IMGW warnings and stations are available in Polish only."));
      html += "</fieldset>";
    }

    html += "<fieldset><legend>WiFi</legend>";
    html += String("<label>") + L("Sieć", "Network") + "</label>";
    html += buildSsidSelect(s.wifiSsid);
    html += String("<label>") + L("Lub wpisz nazwę sieci ręcznie (jeśli ukryta)", "Or type the network name (if hidden)") + "</label>";
    html += String("<input id='ssidManual' name='ssidManual' placeholder='") + L("zostaw puste, jeśli wybrano powyżej", "leave empty if chosen above") + "'>";
    html += String("<label>") + L("Hasło", "Password") + "</label>";
    html += "<div style='position:relative'>";
    html += "<input type='password' id='passField' name='pass' value='" + s.wifiPassword + "' style='padding-right:64px'>";
    html += "<button type='button' onclick=\"var p=document.getElementById('passField');";
    html += String("var show=p.type==='password';p.type=show?'text':'password';this.textContent=show?'") + L("Ukryj", "Hide") + "':'" + L("Pokaż", "Show") + "';\"";
    html += " style='position:absolute;right:4px;top:4px;width:auto;margin:0;padding:6px 10px;";
    html += String("font-size:12px;font-weight:normal;background:#1e1e1e;color:#07c8d8;border:1px solid #333;border-radius:5px'>") + L("Pokaż", "Show") + "</button>";
    html += "</div>";
    // sieci zapasowe: nazwa + haslo (z przyciskiem Pokaz/Ukryj)
    auto backupNet = [&](int n, const String &ssid, const String &pass) {
      String id = String(n);
      html += String("<div class='row'><div><label>") + L("Sieć zapasowa ", "Backup network ") + String(n - 1) + "</label>";
      html += "<input name='ssid" + id + "' value='" + htmlEscape(ssid) + "' placeholder='" + L("nazwa sieci", "network name") + "'></div>";
      html += String("<div><label>") + L("Hasło", "Password") + "</label><div style='position:relative'>";
      html += "<input type='password' id='pass" + id + "' name='pass" + id + "' value='" + htmlEscape(pass) + "' style='padding-right:64px'>";
      html += "<button type='button' onclick=\"var p=document.getElementById('pass" + id + "');";
      html += String("var show=p.type==='password';p.type=show?'text':'password';this.textContent=show?'") + L("Ukryj", "Hide") + "':'" + L("Pokaż", "Show") + "';\"";
      html += " style='position:absolute;right:4px;top:4px;width:auto;margin:0;padding:6px 10px;";
      html += String("font-size:12px;font-weight:normal;background:#1e1e1e;color:#07c8d8;border:1px solid #333;border-radius:5px'>") + L("Pokaż", "Show") + "</button>";
      html += "</div></div></div>";
    };
    html += String("<div class='sub'>") + L("Sieci zapasowe", "Backup networks") + "</div>";
    backupNet(2, s.wifiSsid2, s.wifiPassword2);
    backupNet(3, s.wifiSsid3, s.wifiPassword3);
    html += help(L("Gdy głównej sieci nie ma w zasięgu, stacja próbuje kolejno sieci zapasowej 1, potem 2. Przy starcie (i przy każdym włączeniu Wi-Fi na baterii) najpierw sprawdza, które sieci są widoczne, i łączy się z pierwszą dostępną według tej kolejności. Na zasilaczu, po utracie połączenia, co 2 minuty szuka dostępnej sieci z listy. Nazwę sieci zapasowej wpisz dokładnie (wielkość liter ma znaczenie); pozostaw puste pole, żeby jej nie używać. Z którą siecią stacja jest połączona, widać na ekranie informacji.",
                   "If the main network is out of range, the station tries backup network 1, then 2. At startup (and each time Wi-Fi is switched on while on battery) it first checks which networks are visible and joins the first available one in this order. On mains power, after losing the connection it looks for an available network from the list every 2 minutes. Type the backup network name exactly (case matters); leave the field empty to not use it. The network the station is connected to is shown on the information screen."));
    html += String("<div class='sub'>") + L("Adres w sieci lokalnej", "Local network address") + "</div>";
    html += String("<label>") + L("Nazwa stacji", "Station name") + "</label><div style='display:flex;align-items:center;gap:6px'>";
    html += "<input name='host' maxlength='32' pattern='[A-Za-z0-9-]{1,32}' value='" + s.hostname + "' style='flex:1'>";
    html += "<span style='color:#8a8a8a'>.local</span></div>";
    html += help(String(L("Panel będzie dostępny pod adresem <b>http://", "The panel will be available at <b>http://")) + s.hostname +
                 L(".local</b> (zamiast numeru IP), a router pokaże stację pod tą nazwą. Dozwolone: litery, cyfry i myślnik (do 32 znaków), wielkość liter bez znaczenia. Adresy .local działają w Windows 10/11, macOS, iOS i Linuksie; na części telefonów z Androidem mogą nie działać - wtedy użyj numeru IP z ekranu informacji. Zmiana działa po zapisie (restart). Przy dwóch stacjach w jednej sieci nadaj im różne nazwy.",
                   ".local</b> (instead of the IP number), and your router will list the station under this name. Allowed: letters, digits and hyphen (up to 32 characters), case does not matter. .local addresses work on Windows 10/11, macOS, iOS and Linux; some Android phones may not support them - then use the IP number from the information screen. Takes effect after saving (restart). With two stations on one network, give them different names."));
    html += "</fieldset>";

    // etykiety zalezne od jezyka - tablice bez "static" (L() wybiera tekst przy kazdym budowaniu strony)
    const char *const kTempOpts[] = {"metric", "°C", "imperial", "°F"};
    const char *const kDaysOpts[] = {"3", L("3 dni", "3 days"), "4", L("4 dni", "4 days")};
    const char *const kWindOpts[] = {"ms", "m/s", "kmh", "km/h"};
    const char *const kSrcOpts[] = {"openmeteo", "Open-Meteo", "msn", L("MSN Pogoda", "MSN Weather")};
    // druga opcja: podpis wg wybranego zrodla (zmienia go tez skrypt na stronie po przelaczeniu zrodla)
    const char *const kNowOptsOm[] = {"hourly", L("z aktualnej godziny", "from the current hour"), "now", L("symbol Open-Meteo", "Open-Meteo symbol")};
    const char *const kNowOptsMsn[] = {"hourly", L("z aktualnej godziny", "from the current hour"), "now", L("symbol MSN", "MSN symbol")};
    const char *const kDayIcoOpts[] = {"noon", L("z godz. 12:00", "at 12:00"), "flow", L("z przebiegu dnia", "from the day's course"), "daily", L("symbol MSN", "MSN symbol")};
    const char *const kMeasOpts[] = {"openmeteo", L("model / źródło", "model / source"), "imgw", L("stacja IMGW (tylko PL)", "IMGW station (Only PL)")};
    bool isMsn = (s.weatherSource == "msn");

    html += String("<fieldset><legend>") + L("Pogoda", "Weather") + "</legend>";
    html += String("<div class='row'><div><label>") + L("Miasto", "City") + "</label><input name='city' value='" + htmlEscape(s.city) + "'></div>";
    html += String("<div><label>") + L("Kraj (kod)", "Country (code)") + "</label><input name='country' value='" + s.countryCode + "' maxlength='2'></div></div>";
    html += String("<div class='row'><div><label>") + L("Szerokość geogr.", "Latitude") + "</label><input name='lat' inputmode='decimal' placeholder='" + L("opcjonalnie, np. 53.4285", "optional, e.g. 53.4285") + "' value='" + htmlEscape(s.lat) + "'></div>";
    html += String("<div><label>") + L("Długość geogr.", "Longitude") + "</label><input name='lon' inputmode='decimal' placeholder='" + L("opcjonalnie, np. 14.5528", "optional, e.g. 14.5528") + "' value='" + htmlEscape(s.lon) + "'></div></div>";
    html += help(L("Gdy wpiszesz obie współrzędne (w stopniach dziesiętnych, np. z Map Google: prawy przycisk myszy na miejscu, pierwsza pozycja z menu), urządzenie użyje ich zamiast szukać miasta po nazwie, a nazwa miasta służy wtedy tylko do wyświetlania (i szukania stacji IMGW). Współrzędne dotyczą zarówno Open-Meteo, jak i MSN. Zostaw puste, żeby miasto było wyszukiwane po nazwie.",
                   "If you enter both coordinates (decimal degrees, e.g. from Google Maps: right-click the place, first item in the menu), the station uses them instead of looking the city up by name; the city name is then only displayed (and used to find an IMGW station). Coordinates apply to both Open-Meteo and MSN. Leave empty to look the city up by name."));
    html += String("<div class='row'><div><label>") + L("Temperatura", "Temperature") + "</label>" + seg("units", "units", kTempOpts, 2, s.units) + "</div>";
    html += String("<div><label>") + L("Dni prognozy", "Forecast days") + "</label>" + seg("fcDays", "fcDays", kDaysOpts, 2, String(s.forecastDays)) + "</div></div>";

    html += String("<label>") + L("Prędkość wiatru", "Wind speed") + "</label>" + seg("wunit", "wunit", kWindOpts, 2, s.windUnit);
    html += String("<div class='row'><div><label>") + L("Pobieranie pogody co (min)", "Weather update every (min)") + "</label><input type='number' min='10' max='60' name='fetchMin' value='" + String(s.fetchMin) + "'></div><div></div></div>";
    html += help(L("Jak często stacja pobiera pogodę i jakość powietrza przy zasilaniu z zasilacza: od 10 do 60 minut. Niezależnie od tego o północy dane pobierane są od razu (nowy dzień). Na baterii obowiązuje osobne ustawienie w sekcji „Praca na baterii”.",
                   "How often the station downloads weather and air quality on mains power: 10 to 60 minutes. Regardless of this, data is downloaded right after midnight (new day). On battery, the separate setting in the “Battery operation” section applies."));

    html += "</fieldset>";

    html += String("<fieldset><legend>") + L("Źródło pogody", "Weather source") + "</legend>";
    html += String("<div class='sub'>") + L("Skąd dane", "Data source") + "</div>";
    html += String("<label>") + L("Pogoda i prognoza", "Weather and forecast") + "</label>" + seg("wsrc", "wsrc", kSrcOpts, 2, s.weatherSource);
    html += help(L("MSN dostarcza bieżącą pogodę, ikony, prognozę dni (min/max, szansa opadów), godzinówkę w widoku dnia i ostrzeżenia IMGW (tylko PL). Trend ciśnienia i jakość powietrza nadal pochodzą z Open-Meteo. Z ostrzeżeń IMGW przekazywanych przez MSN stacja korzysta tylko z ostrzeżeń przed wiatrem: barwią kierunek i prędkość wiatru, strzałkę róży i napis „silny wiatr” nad paskiem komfortu (żółty, pomarańczowy, czerwony). Przy źródle Open-Meteo (albo gdy MSN nie odpowiada) ostrzeżenia IMGW nie są dostępne - wtedy stopień ostrzeżenia o wietrze stacja liczy z prognozy, według progów zbliżonych do kryteriów IMGW: porywy od ok. 70 / 90 / 115 km/h albo średni wiatr od 15 / 20 / 25 m/s. MSN to nieoficjalny, nieudokumentowany dostęp bez przydzielonego klucza - może przestać działać bez ostrzeżenia; wtedy urządzenie samo zostaje przy danych Open-Meteo (powód widać w statusie poniżej).",
                   "MSN provides current weather, icons, daily forecast (min/max, chance of rain), the hourly view and IMGW warnings (IMGW: Only PL). Pressure trend and air quality still come from Open-Meteo. Of the IMGW warnings passed on by MSN, the station only uses wind warnings: they color the wind direction and speed, the wind rose arrow and the “strong wind” note above the comfort bar (yellow, orange, red). With the Open-Meteo source (or when MSN does not respond) IMGW warnings are not available - the station then derives the wind warning level from the forecast, using thresholds close to the IMGW criteria: gusts from about 70 / 90 / 115 km/h or mean wind from 15 / 20 / 25 m/s. MSN is an unofficial, undocumented access without an assigned key - it may stop working without notice; the station then simply stays with Open-Meteo data (the reason is shown in the status below)."));

    html += String("<div class='sub'>") + L("Ikony", "Icons") + "</div>";
    html += String("<label>") + L("Aktualna pogoda", "Current weather") + "</label>" + seg("icosrc", "icosrc", isMsn ? kNowOptsMsn : kNowOptsOm, 2, s.iconSource);
    html += help(L("„Z aktualnej godziny” bierze ikonę z prognozy godzinowej dla bieżącej godziny - zawsze zgadza się z podświetlonym wierszem w widoku DZISIAJ. „Symbol MSN” / „symbol Open-Meteo” to ikona, którą wybrane źródło podaje dla bieżącej pogody; obie mogą się nieco różnić.",
                   "“From the current hour” takes the icon from the hourly forecast for the current hour - it always matches the highlighted row in the TODAY view. “MSN symbol” / “Open-Meteo symbol” is the icon the selected source gives for the current weather; the two may differ slightly."));
    html += String("<label>") + L("Prognoza", "Forecast") + "</label>" + segMask("msnico", "msnico", kDayIcoOpts, 3, (!isMsn && s.msnDayIcon == "daily") ? String("noon") : s.msnDayIcon, isMsn ? 0 : 4);
    html += help(L("Dotyczy ikon w kolumnach prognozy (jutro, pojutrze…). „Z godz. 12:00” pokazuje typową pogodę w południe i zgadza się z tabelą godzinową (zjawisko skrajne w ciągu doby, np. burza, ma pierwszeństwo). „Z przebiegu dnia” działa jak „z godz. 12:00”, ale gdy między 8:00 a 20:00 zapowiadane są co najmniej 2 godziny z deszczem (szansa co najmniej 40%), pokazuje słońce z chmurą i kroplami, a gdy deszcz trwa przez większość dnia (połowę godzin lub więcej), zwykłą ikonę deszczu. Działa przy obu źródłach. „Symbol MSN” to jedna ikona wybrana przez MSN dla całej doby i może różnić się od godzin w tabeli (dostępne tylko przy źródle MSN).",
                   "Applies to the icons in the forecast columns (tomorrow, the day after…). “At 12:00” shows the typical weather at noon and matches the hourly table (an extreme event during the day, e.g. a storm, takes priority). “From the day's course” works like “at 12:00”, but when at least 2 hours of rain are forecast between 8:00 and 20:00 (chance of at least 40%) it shows a sun with a cloud and drops, and when rain lasts most of the day (half of the hours or more) a plain rain icon. Works with both sources. “MSN symbol” is a single icon chosen by MSN for the whole day and may differ from the hours in the table (MSN source only)."));

    html += String("<div class='sub'>") + L("Bieżące pomiary", "Current readings") + "</div>";
    html += String("<label>") + L("Skąd temperatura, wilgotność, ciśnienie i wiatr", "Source of temperature, humidity, pressure and wind") + "</label>" + seg("msrc", "msrc", kMeasOpts, 2, s.measureSource);
    html += String("<label>") + L("Stacja IMGW (miasto) - tylko PL", "IMGW station (city) - Only PL") + "</label><input name='imgwst' id='imgwst' maxlength='40' placeholder='" + L("puste = nazwa miasta", "empty = city name") + "' value='" + htmlEscape(s.imgwStation) + "'" + String(s.measureSource == "imgw" ? "" : " disabled") + ">";
    html += help(L("IMGW podaje prawdziwe pomiary ze stacji, odświeżane około raz na godzinę. Ikona, prognoza i godzinówka pochodzą ze źródła wybranego wyżej (Open-Meteo lub MSN), a jakość powietrza i trend ciśnienia zawsze z Open-Meteo. Nazwa stacji jak w IMGW (np. Częstochowa, Jelenia Góra); wielkość liter i polskie znaki bez znaczenia. <a href='https://danepubliczne.imgw.pl/api/data/synop/format/html' target='_blank'>Lista stacji</a>",
                   "IMGW (Polish weather service, Only PL) provides real measurements from weather stations in Poland, refreshed about once an hour. Icon, forecast and hourly data come from the source selected above (Open-Meteo or MSN), air quality and pressure trend always from Open-Meteo. Station name as in IMGW (e.g. Częstochowa, Jelenia Góra); case and Polish characters do not matter. <a href='https://danepubliczne.imgw.pl/api/data/synop/format/html' target='_blank'>Station list</a>"));


    html += String("<div class='sub'>") + L("Zaawansowane", "Advanced") + "</div>";
    html += String("<label>") + L("Model pogodowy Open-Meteo", "Open-Meteo weather model") + "</label><select name='wmodel'>";
    for (size_t i = 0; i < WEATHER_MODELS_COUNT; i++) {
      html += "<option value='" + String(WEATHER_MODELS[i][0]) + "'" + opt(WEATHER_MODELS[i][0], s.weatherModel) + ">";
      html += String(L(WEATHER_MODELS[i][1], WEATHER_MODELS[i][2])) + "</option>";
    }
    html += "</select>";
    html += help(L("Dotyczy danych z Open-Meteo (przy źródle MSN: trend ciśnienia, jakość powietrza i dane zapasowe). Nie każdy model obejmuje każde miejsce (np. ICON-D2 ma ograniczony obszar) - jeśli po zmianie na ekranie pojawi się „Brak danych pogodowych”, wybierz inny model albo wróć do automatycznego.",
                   "Applies to Open-Meteo data (with the MSN source: pressure trend, air quality and fallback data). Not every model covers every place (e.g. ICON-D2 has a limited area) - if “No weather data” appears on the screen after a change, choose another model or go back to automatic."));
    if (_status && _status[0]) html += "<div class='hint' style='margin-top:10px'>Status: " + htmlEscape(String(_status)) + "</div>";
    if (_status2 && _status2[0]) html += "<div class='hint' style='margin-top:4px'>" + htmlEscape(String(_status2)) + "</div>";
    html += "</fieldset>";

    html += String("<fieldset><legend>") + L("Ekran", "Screen") + "</legend>";
    html += String("<div class='sub'>") + L("Co pokazywać", "What to show") + "</div>";
    html += "<label class='sw'><input type='checkbox' name='shpress' " + checked(s.showPressure) + "><span class='trk'></span>" + L("Ciśnienie (z trendem)", "Pressure (with trend)") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='shrain' " + checked(s.showRain) + "><span class='trk'></span>" + L("Prawdopodobieństwo opadów (kropelka)", "Chance of rain (drop)") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='shaqi' " + checked(s.showAqi) + "><span class='trk'></span>" + L("Jakość powietrza (AQI)", "Air quality (AQI)") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='shwind' " + checked(s.showWind) + "><span class='trk'></span>" + L("Kierunek i prędkość wiatru", "Wind direction and speed") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='shmoon' " + checked(s.rightWidget == "moon") + "><span class='trk'></span>" + L("Księżyc zamiast róży wiatru", "Moon instead of wind rose") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='shhint' " + checked(s.showHint) + "><span class='trk'></span>" + L("Dodatkowa informacja nad paskiem komfortu", "Extra note above the comfort bar") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='shsun' " + checked(s.showSunHours) + "><span class='trk'></span>" + L("Godziny słoneczne w prognozie dni", "Sunshine hours in the daily forecast") + "</label>";
    html += "<label class='sw'><input type='checkbox' name='showbat' " + checked(s.showBattery) + "><span class='trk'></span>" + L("Ikona baterii w lewym górnym rogu", "Battery icon in the top left corner") + "</label>";
    html += help(L("Wyłączone elementy znikają z ekranu, a pozostałe w dolnej linii rozsuwają się. Informacja nad paskiem komfortu w pierwszej kolejności ostrzega o zjawiskach groźnych: możliwy grad, możliwa burza, ryzyko gołoledzi (marznący deszcz albo deszcz przy temperaturze około zera), opady śniegu, deszcz ze śniegiem, ryzyko oblodzenia (po deszczu temperatura spada poniżej zera - mokre drogi zamarzają) i silny wiatr. Takie ostrzeżenie wyświetla się samo, bez etykiety komfortu, na pomarańczowo (wiatr w kolorze stopnia ostrzeżenia), z wyprzedzeniem do 12 godzin, a wieczorem (od 18:00) aż do 10:00 rano - gdy zjawisko zacznie się później niż za 4 godziny, z godziną początku, np. „Ryzyko gołoledzi od 5:00”. Zwykłe zjawiska z najbliższych 4 godzin (szansa opadów od 40%) pokazują się obok etykiety komfortu: deszczowo (deszcz przez większą część najbliższych 12 godzin), możliwy deszcz, przelotne opady, możliwa mgła (np. „Chłodno - przelotne opady”). Gdy nic takiego nie zapowiada się, pokazuje zachmurzenie według aktualnej ikony: słonecznie (w nocy bezchmurnie), częściowe zachmurzenie, przejaśnienia (tylko źródło MSN; w nocy „częściowe zachmurzenie”) albo pochmurno (np. „Zimno - bezchmurnie”). Godziny słoneczne to prognozowana liczba godzin ze słońcem w danym dniu. Przy źródle Open-Meteo pochodzą z danych nasłonecznienia modelu; przy źródle MSN są liczone z ikon godzinówki, tylko w czasie między wschodem a zachodem słońca (słońce liczy się w całości, częściowe zachmurzenie w połowie, częste przejaśnienia w jednej czwartej, reszta jako zero). „-- h” oznacza brak danych. Ikona baterii pokazuje poziom naładowania ogniwa Li-ion podłączonego do złącza baterii na płytce wypełnieniem, jak w telefonie (poziom szacowany z napięcia: 3,3 V = pusta, 4,15 V = pełna, zmienia się powoli, żeby nie skakał po podłączeniu zasilacza). Piorun na ikonie z zielonym wypełnieniem oznacza ładowanie, migające czerwone wypełnienie - niski poziom (15% i mniej). Pełna zielona bateria z piorunem oznacza naładowane ogniwo przy podłączonym zasilaczu. Bez ogniwa (sam zasilacz USB) ikona baterii znika, a na ekranie informacji jest „Zasilanie: USB”. Pełnego ogniwa na zasilaczu nie da się odróżnić od braku ogniwa po samym napięciu, więc stacja zapamiętuje, że widziała ogniwo (ładowanie albo praca z baterii) - po pierwszym takim zdarzeniu rozpoznaje je już na stałe. Dokładne napięcie i procent są na ekranie informacji. „Księżyc zamiast róży wiatru” pokazuje obok temperatury aktualną fazę Księżyca w miejscu róży wiatru. Przełącznik „Kierunek i prędkość wiatru” dotyczy tylko napisu w dolnej linii (np. „SW 5 m/s”) - róży ani Księżyca nie wyłącza. Źródło danych pogodowych jest na ekranie informacji (dotknięcie kresek zasięgu Wi-Fi).",
                   "Disabled items disappear from the screen and the remaining ones in the bottom line spread out. The note above the comfort bar first warns about hazardous weather: possible hail, possible storm, black ice risk (freezing rain, or rain at temperatures around zero), snow, sleet, icy roads risk (after rain the temperature drops below zero - wet roads freeze) and strong wind. Such a warning is shown on its own, without the comfort label, in orange (wind in the warning level color), up to 12 hours ahead, and in the evening (from 18:00) until 10:00 the next morning - when it starts later than 4 hours from now, with the start time, e.g. “Black ice risk from 5:00”. Ordinary weather in the next 4 hours (chance of precipitation from 40%) is shown next to the comfort label: rainy (rain for most of the next 12 hours), possible rain, showers, possible fog (e.g. “Cool - showers”). When nothing like that is expected, it describes the cloud cover from the current icon: sunny (clear at night), partly cloudy, sunny spells (MSN source only; “partly cloudy” at night) or cloudy (e.g. “Cold - clear”). Sunshine hours are the forecast number of sunny hours on a given day. With the Open-Meteo source they come from the model's sunshine data; with the MSN source they are counted from the hourly icons, only between sunrise and sunset (sun counts fully, partly cloudy as half, sunny spells as a quarter, the rest as zero). “-- h” means no data. The battery icon shows the charge level of a Li-ion cell connected to the battery connector on the board, filled like on a phone (estimated from voltage: 3.3 V = empty, 4.15 V = full; it changes slowly so it does not jump when the charger is plugged in). A bolt with green fill means charging, a blinking red fill means low (15% or less). A full green battery with a bolt means a charged cell with the charger connected. Without a cell (USB power only) the battery icon disappears and the information screen shows “Power: USB”. A full cell on the charger cannot be told apart from no cell by voltage alone, so the station remembers that it has seen a cell (charging or running on battery) - after the first such event it recognizes it permanently. Exact voltage and percentage are on the information screen. “Moon instead of wind rose” shows the current Moon phase next to the temperature in place of the wind rose. The “Wind direction and speed” switch only affects the text in the bottom line (e.g. “SW 5 m/s”) - it does not hide the rose or the Moon. The weather data source is shown on the information screen (tap the Wi-Fi signal bars)."));
    html += String("<div class='sub'>") + L("Zegar", "Clock") + "</div>";
    html += "<label class='sw'><input type='checkbox' name='use24h' " + checked(s.use24h) + "><span class='trk'></span>" + L("Format 24-godzinny", "24-hour format") + "</label>";
    html += String("<div class='sub'>") + L("Jasność", "Brightness") + "</div>";
    html += String("<div class='row'><div><label>") + L("Jasność dzień (%)", "Day brightness (%)") + "</label><input type='number' min='5' max='100' name='brDay' value='" + String(s.brightnessDay) + "'></div>";
    html += String("<div><label>") + L("Jasność noc (%)", "Night brightness (%)") + "</label><input type='number' min='0' max='100' name='brNight' value='" + String(s.brightnessNight) + "'></div></div>";
    html += String("<div class='row'><div><label>") + L("Przyciemnianie od", "Dim from") + "</label><input type='time' name='nStart' value='" + s.nightStart + "'></div>";
    html += String("<div><label>") + L("Przyciemnianie do", "Dim until") + "</label><input type='time' name='nEnd' value='" + s.nightEnd + "'></div></div>";
    html += "</fieldset>";

    html += String("<fieldset><legend>") + L("Wywóz śmieci", "Bin collection") + "</legend>";
    {
      String w = s.wasteDates;                      // zawartosc textarea - znaki specjalne HTML
      w.replace("&", "&amp;"); w.replace("<", "&lt;"); w.replace(">", "&gt;");
      html += String("<label>") + L("Terminy wywozu", "Collection dates") + "</label>";
      html += String("<textarea name='waste' rows='8' maxlength='3000' style='width:100%;box-sizing:border-box;padding:7px;background:#1e1e1e;border:1px solid #333;color:#fff;border-radius:6px;font-size:14px;font-family:monospace' placeholder='")
            + L("2026&#10;6.10 13.10 20.10 27.10 zmieszane, bio&#10;12.10 26.10 segregowane", "2026&#10;6.10 13.10 20.10 27.10 general, garden&#10;12.10 26.10 recycling") + "'>" + w + "</textarea>";
    }
    html += help(L("Wpisz terminy z harmonogramu, linia po linii: najpierw jedna albo kilka dat (oddzielonych spacją lub przecinkiem), potem (nieobowiązkowo) rodzaj odpadów, np. „6.10 13.10 20.10 zmieszane, bio”. Linia z samym rokiem (np. „2026”) ustala rok dla dat zapisanych bez roku pod nią. Daty można też pisać z rokiem: 27.10.2026, 27.10.26 albo 2026-10-27. Data bez roku i bez linii z rokiem obowiązuje co roku. Linie bez daty (np. zaczynające się od #) są pomijane - można w nich robić notatki. W dniu poprzedzającym wywóz nad paskiem komfortu pojawia się pomarańczowy napis „JUTRO WYWÓZ: ZMIESZANE, BIO” (albo „JUTRO WYWÓZ ŚMIECI”, gdy nie wpisano rodzaju), a w dniu wywozu do godziny 12:00 czerwony „DZIŚ WYWÓZ: …”. Przypomnienie wyświetla się w tym czasie na stałe, zamiast etykiety komfortu. Puste pole wyłącza przypomnienia.",
                   "Enter the dates from your collection schedule, line by line: first one or more dates (day.month, separated by a space or comma), then (optionally) the type of waste, e.g. “6.10 13.10 20.10 general, garden”. A line with just a year (e.g. “2026”) sets the year for the dates without a year below it. Dates can also include the year: 27.10.2026, 27.10.26 or 2026-10-27. A date without a year and without a year line applies every year. Lines without a date (e.g. starting with #) are skipped - you can use them for notes. On the day before a collection an orange “BINS TOMORROW: GENERAL, GARDEN” appears above the comfort bar (or “BIN COLLECTION TOMORROW” when no type is given), and on the collection day until 12:00 a red “BINS TODAY: …”. During that time the reminder is shown permanently instead of the comfort label. An empty field turns reminders off."));
    html += "</fieldset>";

    html += String("<fieldset><legend>") + L("Praca na baterii", "Battery operation") + "</legend>";
    html += String("<div class='row'><div><label>") + L("Jasność na baterii (%)", "Brightness on battery (%)") + "</label><input type='number' min='5' max='100' name='brBat' value='" + String(s.brightnessBattery) + "'></div>";
    html += String("<div><label>") + L("Pobieranie pogody co (min)", "Weather update every (min)") + "</label><input type='number' min='10' max='120' name='batFetch' value='" + String(s.batteryFetchMin) + "'></div></div>";
    html += help(L("Gdy stacja działa z akumulatora (bez zasilacza), przechodzi w tryb oszczędzania: podświetlenie nie przekracza ustawionej jasności (w nocy zostaje niższa jasność nocna), procesor zwalnia do 80 MHz, a Wi-Fi włącza się tylko na czas pobrania pogody, co ustawioną liczbę minut. W tym trybie panel WWW i aktualizacja OTA są niedostępne - podłącz zasilacz, a Wi-Fi wróci na stałe w ciągu kilkunastu sekund. Stacja działa do całkowitego rozładowania ogniwa - zasilanie odcina wtedy układ ochronny (PCM) ogniwa. Po podłączeniu zasilacza stacja uruchamia się od razu i ładuje ogniwo.",
                   "When the station runs on the battery (no charger), it switches to power saving: the backlight does not exceed the set brightness (the lower night brightness still applies at night), the CPU slows down to 80 MHz, and Wi-Fi is switched on only to download the weather, every set number of minutes. In this mode the web panel and OTA updates are unavailable - plug in the charger and Wi-Fi comes back permanently within a few seconds. The station runs until the cell is completely discharged - the cell's protection circuit (PCM) then cuts the power. When the charger is plugged in, the station starts right away and charges the cell."));
    html += "<label class='sw'><input type='checkbox' name='usbsense' " + checked(s.usbSense) + "><span class='trk'></span>" + L("Detekcja zasilacza (przeróbka)", "Charger detection (hardware mod)") + "</label>";
    html += help(L("Włącz tylko po przeróbce płytki: dzielnik 4,7 kΩ / 4,7 kΩ z linii VBUS (anoda diody D2) do masy, środek dzielnika na wolne wejście P7 ekspandera PCF8574 (nóżka 12 układu U4). Stacja wie wtedy od razu, czy zasilacz jest podłączony - ikona baterii i oszczędzanie reagują w ok. 2 sekundy, bez zgadywania z napięcia ogniwa. Bez przeróbki zostaw wyłączone (wejście P7 bez dzielnika jest zawsze w stanie wysokim i stacja myślałaby, że zasilacz jest stale podłączony).",
                   "Enable only after modifying the board: a 4.7 kΩ / 4.7 kΩ divider from the VBUS line (anode of diode D2) to ground, with the divider midpoint on the free input P7 of the PCF8574 expander (pin 12 of U4). The station then knows immediately whether the charger is connected - the battery icon and power saving react within about 2 seconds, without guessing from the cell voltage. Without the modification leave it off (input P7 without the divider is always high and the station would think the charger is always connected)."));
    html += "</fieldset>";

    html += String("<fieldset><legend>") + L("Czas", "Time") + "</legend>";
    {
      // lista popularnych stref (opis PL, opis EN, regula POSIX TZ ze zmiana czasu); "wlasna" = pole tekstowe
      static const char *const kZones[][3] = {
        {"Polska, Niemcy, Francja, Włochy… (CET/CEST)", "Poland, Germany, France, Italy… (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Wielka Brytania, Irlandia (GMT/BST)",          "United Kingdom, Ireland (GMT/BST)",          "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Portugalia (WET/WEST)",                        "Portugal (WET/WEST)",                        "WET0WEST,M3.5.0/1,M10.5.0"},
        {"Finlandia, Ukraina, Grecja, Rumunia… (EET/EEST)", "Finland, Ukraine, Greece, Romania… (EET/EEST)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Turcja, Białoruś, Moskwa (UTC+3)",             "Turkey, Belarus, Moscow (UTC+3)",            "<+03>-3"},
        {"UTC (bez zmiany czasu)",                       "UTC (no daylight saving)",                   "UTC0"},
        {"USA/Kanada - wschód (New York)",               "USA/Canada - Eastern (New York)",            "EST5EDT,M3.2.0,M11.1.0"},
        {"USA/Kanada - centrum (Chicago)",               "USA/Canada - Central (Chicago)",             "CST6CDT,M3.2.0,M11.1.0"},
        {"USA/Kanada - góry (Denver)",                   "USA/Canada - Mountain (Denver)",             "MST7MDT,M3.2.0,M11.1.0"},
        {"USA - Arizona (bez zmiany czasu)",             "USA - Arizona (no daylight saving)",         "MST7"},
        {"USA/Kanada - Pacyfik (Los Angeles)",           "USA/Canada - Pacific (Los Angeles)",         "PST8PDT,M3.2.0,M11.1.0"},
        {"Alaska",                                       "Alaska",                                     "AKST9AKDT,M3.2.0,M11.1.0"},
        {"Hawaje",                                       "Hawaii",                                     "HST10"},
        {"Brazylia - São Paulo (UTC-3)",                 "Brazil - São Paulo (UTC-3)",                 "<-03>3"},
        {"Republika Południowej Afryki (UTC+2)",         "South Africa (UTC+2)",                       "SAST-2"},
        {"Zjednoczone Emiraty Arabskie (UTC+4)",         "United Arab Emirates (UTC+4)",               "<+04>-4"},
        {"Indie (UTC+5:30)",                             "India (UTC+5:30)",                           "IST-5:30"},
        {"Chiny, Singapur, Australia Zach. (UTC+8)",     "China, Singapore, Western Australia (UTC+8)", "<+08>-8"},
        {"Japonia, Korea (UTC+9)",                       "Japan, Korea (UTC+9)",                       "JST-9"},
        {"Australia - Sydney, Melbourne",                "Australia - Sydney, Melbourne",              "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia - Brisbane (bez zmiany czasu)",      "Australia - Brisbane (no daylight saving)",  "AEST-10"},
        {"Nowa Zelandia",                                "New Zealand",                                "NZST-12NZDT,M9.5.0,M4.1.0/3"},
      };
      const size_t nZones = sizeof(kZones) / sizeof(kZones[0]);
      bool known = false;
      for (size_t i = 0; i < nZones; i++) if (s.tz == kZones[i][2]) known = true;
      html += String("<label>") + L("Strefa czasowa", "Time zone") + "</label>";
      html += "<select name='tzsel' id='tzsel' onchange=\"var c=this.value==='custom';var t=document.getElementById('tzrow');t.style.display=c?'block':'none';\">";
      for (size_t i = 0; i < nZones; i++) {
        html += String("<option value='") + kZones[i][2] + "'" + opt(kZones[i][2], s.tz) + ">" + L(kZones[i][0], kZones[i][1]) + "</option>";
      }
      html += String("<option value='custom'") + (known ? "" : " selected") + ">" + L("Inna (wpisz regułę POSIX TZ)", "Other (enter a POSIX TZ rule)") + "</option>";
      html += "</select>";
      html += String("<div id='tzrow' style='display:") + (known ? "none" : "block") + "'>";
      html += String("<label>") + L("Reguła POSIX TZ", "POSIX TZ rule") + "</label><input name='tz' value='" + htmlEscape(s.tz) + "'></div>";
      html += help(L("Strefa czasowa z automatyczną zmianą czasu letniego i zimowego. Jeśli Twojej strefy nie ma na liście, wybierz „Inna” i wpisz regułę POSIX TZ - listę reguł dla miast znajdziesz np. na <a href='https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv' target='_blank'>github.com/nayarsystems/posix_tz_db</a>. Zmiana działa po zapisie (restart).",
                     "Time zone with automatic daylight saving changes. If your zone is not on the list, choose “Other” and enter a POSIX TZ rule - rules for cities are listed e.g. at <a href='https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv' target='_blank'>github.com/nayarsystems/posix_tz_db</a>. Takes effect after saving (restart)."));
    }
    html += "</fieldset>";


    // jeden rzad przyciskow: Zapisz (akcent), Tryb AP (czerwony), Tylko restart (zielony) - kolory jak na ekranie stacji.
    // Tryb AP i restart wysylaja formularz pod inny adres (formaction), ustawien NIE zapisuja.
    html += "<style>.brow button{margin:0;padding:12px 6px;font-size:14px;white-space:nowrap}</style>";
    html += "<div class='brow' style='display:flex;gap:8px;margin-top:10px'>";
    html += String("<button type='submit' style='flex:1.6'>") + L("Zapisz ustawienia", "Save settings") + "</button>";
    if (!_apMode) {
      html += String("<button type='submit' formaction='/apmode' formnovalidate style='flex:1;background:#ff4d4d;color:#000' onclick=\"return confirm('") +
              L("Przejść w tryb konfiguracji? Stacja rozłączy się z siecią i uruchomi własną sieć StacjaPogody-Setup. Niezapisane zmiany przepadną.",
                "Switch to setup mode? The station will disconnect from the network and start its own StacjaPogody-Setup network. Unsaved changes will be lost.") +
              "');\">" + L("Tryb AP", "AP mode") + "</button>";
    }
    html += String("<button type='submit' formaction='/restart' formnovalidate style='flex:1;background:#50c864;color:#000' onclick=\"return confirm('") +
            L("Zrestartować stację bez zapisywania zmian?", "Restart the station without saving changes?") + "');\">" +
            L("Tylko restart", "Restart only") + "</button>";
    html += "</div>";
    html += help(String(L("„Zapisz ustawienia” zapisuje zmiany i restartuje stację. „Tylko restart” uruchamia stację ponownie bez zapisywania zmian z tej strony. ",
                          "“Save settings” saves the changes and restarts the station. “Restart only” restarts the station without saving changes made on this page. ")) +
                 (_apMode ? String("") : String(L("„Tryb AP”: ", "“AP mode”: ")) + L("Stacja uruchomi się ponownie z własną siecią Wi-Fi <b>StacjaPogody-Setup</b> (bez hasła, kod QR na ekranie), a panel będzie pod adresem http://192.168.4.1. Przydaje się np. przy zmianie routera albo hasła Wi-Fi. Zapisane sieci i ustawienia zostają; po zapisie w panelu albo po wyłączeniu i włączeniu zasilania stacja wraca do zwykłej pracy.",
                     "The station restarts with its own Wi-Fi network <b>StacjaPogody-Setup</b> (no password, QR code on the screen), and the panel will be at http://192.168.4.1. Useful e.g. when changing your router or Wi-Fi password. Saved networks and settings are kept; after saving in the panel, or after switching the power off and on, the station returns to normal operation.")));
    html += "</form>";

    if (!_apMode) {
      html += String("<fieldset style='margin-top:22px'><legend>") + L("Aktualizacja oprogramowania (OTA)", "Firmware update (OTA)") + "</legend>";
      html += "<form id='otaForm'>";
      html += String("<label>") + L("Plik .bin (Szkic -> Eksportuj skompilowany plik binarny w Arduino IDE)", ".bin file (Sketch -> Export Compiled Binary in Arduino IDE)") + "</label>";
      html += "<input type='file' id='otaFile' name='update' accept='.bin' required>";
      // maly, "cichy" przycisk (inny wyglad niz glowny "Zapisz ustawienia") - zeby ich nie mylic
      html += "<button type='submit' style='width:auto;padding:6px 14px;font-size:12px;font-weight:normal;margin-top:6px;";
      html += String("background:#1e1e1e;color:#07c8d8;border:1px solid #333;border-radius:6px'>") + L("Wgraj firmware", "Upload firmware") + "</button>";
      html += "</form>";
      html += "<div id='otaProgressWrap' style='display:none;margin-top:12px'>";
      html += "<div style='background:#2a2a2a;border-radius:6px;overflow:hidden;height:20px'>";
      html += "<div id='otaBar' style='background:#07c8d8;height:100%;width:0%;transition:width .15s'></div>";
      html += "</div>";
      html += "<div id='otaPct' style='text-align:center;font-size:12px;margin-top:4px;color:#8a8a8a'>0%</div>";
      html += "</div>";
      html += "<div id='otaResult' style='margin-top:8px;font-size:13px'></div>";
      html += String("<div class='hint'>") + L("Nie wyłączaj zasilania w trakcie wgrywania - przerwana aktualizacja może wymagać ponownego wgrania przez USB.",
                                                "Do not switch the power off while uploading - an interrupted update may require re-flashing over USB.") + "</div>";
      html += "</fieldset>";

      html += "<script>";
      html += "document.getElementById('otaForm').addEventListener('submit',function(e){";
      html += "e.preventDefault();";
      html += "var f=document.getElementById('otaFile');if(!f.files.length)return;";
      html += "var fd=new FormData();fd.append('update',f.files[0]);";
      html += "var wrap=document.getElementById('otaProgressWrap'),bar=document.getElementById('otaBar'),";
      html += "pct=document.getElementById('otaPct'),res=document.getElementById('otaResult');";
      html += "wrap.style.display='block';res.textContent='';bar.style.width='0%';pct.textContent='0%';";
      html += "var xhr=new XMLHttpRequest();xhr.open('POST','/update',true);";
      html += "xhr.upload.onprogress=function(ev){if(ev.lengthComputable){";
      html += "var p=Math.round(ev.loaded/ev.total*100);bar.style.width=p+'%';pct.textContent=p+'%';}};";
      html += "xhr.onload=function(){";
      html += "bar.style.width='100%';pct.textContent='100%';res.textContent=xhr.responseText;";
      html += "if(xhr.status===200)setTimeout(function(){window.location='/';},4000);";
      html += "};";
      html += String("xhr.onerror=function(){res.textContent='") + L("Błąd połączenia podczas wgrywania.", "Connection error during upload.") + "';};";
      html += "xhr.send(fd);";
      html += "});";
      html += "</script>";
    }

    html += "<script>";
    // zaleznosci miedzy opcjami: "Ikony dni" tylko przy MSN, "Stacja IMGW" tylko przy pomiarach z IMGW
    html += "(function(){function val(n){var r=document.getElementsByName(n);for(var i=0;i<r.length;i++)if(r[i].checked)return r[i].value;return '';}";
    html += "function setOn(id,on){var d=document.getElementById(id);if(!d)return;d.classList.toggle('off',!on);";
    html += "var x=d.querySelectorAll('input');for(var i=0;i<x.length;i++)x[i].disabled=!on;}";
    html += "function t(){var s=document.getElementById('imgwst');if(s)s.disabled=(val('msrc')!=='imgw');";
    html += "var d=document.querySelector('#msnico input[value=daily]');if(d){var m=(val('wsrc')==='msn');d.disabled=!m;";
    html += "if(!m&&d.checked){var n=document.querySelector('#msnico input[value=noon]');if(n)n.checked=true;}}";
    html += String("var q=document.querySelector('#icosrc input[value=now]');if(q&&q.nextSibling)q.nextSibling.textContent=(val('wsrc')==='msn')?'")
            + L("symbol MSN", "MSN symbol") + "':'" + L("symbol Open-Meteo", "Open-Meteo symbol") + "';}";
    html += "var a=document.querySelectorAll('input[name=wsrc],input[name=msrc]');for(var i=0;i<a.length;i++)a[i].addEventListener('change',t);t();})();";
    html += "</script>";
    html += "</body></html>";
    return html;
  }

  void handleSave(AsyncWebServerRequest *request) {
    AppSettings &s = _store->settings;

    // Recznie wpisana nazwa sieci ma pierwszenstwo nad wyborem z listy
    String manualSsid = request->hasParam("ssidManual", true) ? request->getParam("ssidManual", true)->value() : "";
    String pickedSsid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
    if (!manualSsid.isEmpty()) {
      s.wifiSsid = manualSsid;
    } else if (!pickedSsid.isEmpty()) {
      s.wifiSsid = pickedSsid;
    }

    if (request->hasParam("pass", true)) s.wifiPassword = request->getParam("pass", true)->value();
    if (request->hasParam("ssid2", true)) { s.wifiSsid2 = request->getParam("ssid2", true)->value(); s.wifiSsid2.trim(); }
    if (request->hasParam("pass2", true)) s.wifiPassword2 = request->getParam("pass2", true)->value();
    if (request->hasParam("ssid3", true)) { s.wifiSsid3 = request->getParam("ssid3", true)->value(); s.wifiSsid3.trim(); }
    if (request->hasParam("pass3", true)) s.wifiPassword3 = request->getParam("pass3", true)->value();
    if (request->hasParam("city", true)) s.city = request->getParam("city", true)->value();
    if (request->hasParam("country", true)) s.countryCode = request->getParam("country", true)->value();
    // wspolrzedne: oba poprawne (przecinek = kropka, lat -90..90, lon -180..180) albo oba puste -> geokodowanie po nazwie
    if (request->hasParam("lat", true) || request->hasParam("lon", true)) {
      String la = request->hasParam("lat", true) ? request->getParam("lat", true)->value() : String("");
      String lo = request->hasParam("lon", true) ? request->getParam("lon", true)->value() : String("");
      la.trim(); lo.trim(); la.replace(',', '.'); lo.replace(',', '.');
      float fa = la.toFloat(), fo = lo.toFloat();
      bool ok = la.length() && lo.length() && fa >= -90.0f && fa <= 90.0f && fo >= -180.0f && fo <= 180.0f && !(fa == 0.0f && fo == 0.0f);
      s.lat = ok ? String(fa, 4) : String("");
      s.lon = ok ? String(fo, 4) : String("");
    }
    if (request->hasParam("units", true)) s.units = request->getParam("units", true)->value();
    if (request->hasParam("fcDays", true)) s.forecastDays = request->getParam("fcDays", true)->value().toInt();
    if (request->hasParam("wunit", true)) {
      s.windUnit = (request->getParam("wunit", true)->value() == "kmh") ? "kmh" : "ms";
    }
    if (request->hasParam("msnico", true)) {
      { String v = request->getParam("msnico", true)->value(); s.msnDayIcon = (v == "daily" || v == "flow") ? v : "noon"; }
    }
    if (request->hasParam("wsrc", true)) {
      s.weatherSource = (request->getParam("wsrc", true)->value() == "msn") ? "msn" : "openmeteo";
    }
    if (request->hasParam("icosrc", true)) {
      s.iconSource = (request->getParam("icosrc", true)->value() == "now") ? "now" : "hourly";
    }
    if (request->hasParam("msrc", true)) {
      s.measureSource = (request->getParam("msrc", true)->value() == "imgw") ? "imgw" : "openmeteo";
    }
    if (request->hasParam("imgwst", true)) {
      String v = request->getParam("imgwst", true)->value();
      v.trim();
      if (v.length() > 40) v = v.substring(0, 40);
      s.imgwStation = v;
    }
    if (request->hasParam("wmodel", true)) {
      String m = request->getParam("wmodel", true)->value();
      s.weatherModel = isKnownWeatherModel(m) ? m : String("best_match");
    }
    s.showPressure = request->hasParam("shpress", true);
    s.showRain = request->hasParam("shrain", true);
    s.showAqi = request->hasParam("shaqi", true);
    s.showWind = request->hasParam("shwind", true);
    s.rightWidget = request->hasParam("shmoon", true) ? "moon" : "wind";
    s.showHint = request->hasParam("shhint", true);
    s.showBattery = request->hasParam("showbat", true);
    s.showSunHours = request->hasParam("shsun", true);
    s.use24h = request->hasParam("use24h", true);
    // strefa czasowa: wybor z listy, a przy "Inna" - regula wpisana recznie
    {
      String sel = request->hasParam("tzsel", true) ? request->getParam("tzsel", true)->value() : String("custom");
      String manual = request->hasParam("tz", true) ? request->getParam("tz", true)->value() : String("");
      manual.trim();
      if (sel != "custom" && sel.length()) s.tz = sel;
      else if (manual.length()) s.tz = manual;
    }
    if (request->hasParam("host", true)) s.hostname = AppSettings::sanitizeHostname(request->getParam("host", true)->value());
    if (request->hasParam("brDay", true)) s.brightnessDay = request->getParam("brDay", true)->value().toInt();
    if (request->hasParam("brNight", true)) s.brightnessNight = request->getParam("brNight", true)->value().toInt();
    if (request->hasParam("nStart", true)) s.nightStart = request->getParam("nStart", true)->value();
    if (request->hasParam("nEnd", true)) s.nightEnd = request->getParam("nEnd", true)->value();
    if (request->hasParam("waste", true)) {
      String w = request->getParam("waste", true)->value();
      w.replace("\r", "");
      w.trim();
      if (w.length() > 3000) w = w.substring(0, 3000);
      s.wasteDates = w;
    }
    s.usbSense = request->hasParam("usbsense", true);
    if (request->hasParam("lang", true)) s.lang = request->getParam("lang", true)->value() == "1" ? 1 : 0;
    if (request->hasParam("brBat", true)) s.brightnessBattery = constrain((int)request->getParam("brBat", true)->value().toInt(), 5, 100);
    if (request->hasParam("fetchMin", true)) s.fetchMin = constrain((int)request->getParam("fetchMin", true)->value().toInt(), 10, 60);
    if (request->hasParam("batFetch", true)) s.batteryFetchMin = constrain((int)request->getParam("batFetch", true)->value().toInt(), 10, 120);

    _store->save();

    // komunikat w nowo wybranym jezyku (g_lang zmieni sie dopiero po restarcie)
    bool en = (s.lang == 1);
    String page = String("<!DOCTYPE html><html lang='") + (en ? "en" : "pl") + "'><head><meta charset='utf-8'>";
    page += "<meta http-equiv='refresh' content='3;url=/'>";
    page += "<style>" + darkStyle() + "body{text-align:center;padding-top:60px}</style></head><body>";
    page += String("<h3 style='color:#07c8d8'>") + (en ? "Saved. The station will restart..." : "Zapisano. Urządzenie zrestartuje się...") + "</h3></body></html>";
    request->send(200, "text/html; charset=utf-8", page);

    _rebootRequested = true;
  }
};
