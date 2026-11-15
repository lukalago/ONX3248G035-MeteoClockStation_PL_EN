<img width="640" height="960" alt="ekran_glowny_pl" src="https://github.com/user-attachments/assets/9986f638-dbd9-4402-bbdc-bd3205f76369" />
<img width="2320" height="490" alt="wszystkie_ekrany_en" src="https://github.com/user-attachments/assets/0b4c5746-61a7-4551-8623-4220ee4d0c14" />
<img width="640" height="960" alt="ekran_glowny_en" src="https://github.com/user-attachments/assets/70a3eeeb-822e-480f-b424-a92db7e4d6b8" />
<img width="2320" height="490" alt="wszystkie_ekrany_pl" src="https://github.com/user-attachments/assets/5329e25c-35f6-4d5a-ba54-1fe18d2f1d1e" />


# Stacja pogody ESP32-S3 – Nextion ONX3248G035

Domowa stacja pogody na module **Nextion ONX3248G035** (ESP32-S3, ekran 3,5" 320×480 z dotykiem).
Pokazuje aktualną pogodę, prognozę, jakość powietrza, wschód i zachód słońca, fazę Księżyca, pyłki
oraz przypomina o wywozie śmieci. Napisy po polsku albo po angielsku, a konfiguracja odbywa się przez panel WWW.

*English:* a home weather station for the Nextion ONX3248G035 (ESP32-S3) board. The screen and web panel can be switched to English in the panel (IMGW data: Only PL).

## Funkcje

- **Ekran główny:** zegar, aktualna pogoda z ikoną, temperatura z trendem, min/max dnia, róża wiatru
  albo faza Księżyca, ciśnienie z trendem, szansa opadów, AQI, prognoza na 3–4 dni, pasek komfortu.
- **Szczegóły dnia:** prognoza godzinowa (dotknięcie kolumny prognozy).
- **Przegląd dnia:** opady na 24 h, droga słońca z długością dnia, faza Księżyca, pyłki.
- **Powietrze i odczucia:** AQI, PM2,5 i PM10, indeks UV, temperatura odczuwalna, wilgotność, ciśnienie, „jutro”.
- **Informacje:** Wi-Fi, źródło danych, wersja, zasilanie, przyciski Restart i Tryb AP.
- **Źródła danych:** Open-Meteo (bez klucza), opcjonalnie MSN Pogoda (z ostrzeżeniami IMGW przed wiatrem)
  i pomiary ze stacji IMGW; opcjonalny czujnik BME280.
- **Ostrzeżenie o silnym wietrze:** z IMGW albo z prognozy, kolorowanie wiatru na ekranach.
- **Przypomnienie o wywozie śmieci:** lista dat w panelu WWW (przykład: `docs/przyklad_harmonogram_smieci.txt`).
- **Praca na akumulatorze Li-ion:** ikona baterii, tryb oszczędzania (przyciemnienie, Wi-Fi tylko na pobranie).
- **Panel WWW:** wszystkie ustawienia, 3 sieci Wi-Fi (główna + 2 zapasowe), adres `meteo.local` (nazwa do zmiany), aktualizacja OTA, przejście w tryb AP.
- **Język polski / angielski (English):** ekran i panel WWW, przełącznik na górze panelu. Ostrzeżenia i stacje IMGW są dostępne tylko po polsku.
- **Tryb konfiguracji (AP)** z kodem QR do połączenia z siecią stacji.

## Sprzęt

- Nextion ONX3248G035 (ESP32-S3, ST7796, dotyk CST826, ekspander PCF8574, ładowarka IP4054).
  Schemat i przykłady producenta: <https://github.com/OpenNextion/OpenNextion-SKU-ONX3248G035>
- Opcjonalnie: czujnik BME280 na I²C (złącze Grove), ogniwo Li-ion.
- Opcjonalna przeróbka „detekcja zasilacza”: dzielnik 4,7 kΩ / 4,7 kΩ z linii VBUS (anoda D2)
  na wejście P7 ekspandera PCF8574 (nóżka 12 U4), włączana w panelu WWW.

## Szybki start bez kompilacji

Gotowy plik `ESP32-Nextion_METEO_v300_OTA.bin` jest w zakładce **Releases**. Jeśli na module działa już wcześniejsza wersja stacji, wgraj go przez panel WWW (sekcja „Aktualizacja oprogramowania”). Pierwsze wgranie na czysty moduł wymaga Arduino IDE (poniżej).

Pełna instrukcja budowy: [`docs/Stacja_pogody_ESP32_dokumentacja.docx`](docs/Stacja_pogody_ESP32_dokumentacja.docx).

## Kompilacja (Arduino IDE)

1. Zainstaluj obsługę płytek **esp32** (Espressif) i wybierz płytkę **ESP32S3 Dev Module**
   ze schematem partycji obsługującym OTA.
2. Zainstaluj biblioteki: **GFX Library for Arduino** (Arduino_GFX), **Adafruit GFX**, **Adafruit CST8XX**,
   **Adafruit PCF8574**, **Adafruit BME280**, **ArduinoJson**, **ESPAsyncWebServer** (z **AsyncTCP**).
3. Otwórz `ESP32-Nextion_METEO_v300/ESP32-Nextion_METEO_v300.ino`, skompiluj i wgraj.

## Pierwsze uruchomienie

Bez zapisanej sieci stacja tworzy punkt dostępowy **StacjaPogody-Setup**. Połącz się z nim telefonem,
otwórz w przeglądarce adres pokazany na ekranie, wybierz sieć Wi-Fi, wpisz miasto i zapisz.
Później panel jest dostępny pod adresem IP stacji (ekran informacji: dotknięcie kresek Wi-Fi).

## Narzędzia

- `tools/gen_icons.py` – generator ikon pogody (`WeatherIconsBmp.h`). Wymaga Pythona i biblioteki Pillow:
  `python gen_icons.py WeatherIconsBmp.h`
- `tools/gen_fonts_aa.py` – generator wygładzonych czcionek `FreeSans…Pl.h` (4 bity krycia na piksel, polskie znaki). Wymaga Pillow i czcionek GNU FreeFont (FreeSans.ttf, FreeSansBold.ttf):
  `python gen_fonts_aa.py <folder_z_czcionkami_Pl.h> [folder_wyjściowy]`

## Uwagi

- Dostęp do MSN Pogoda jest nieoficjalny i może przestać działać – stacja wraca wtedy do Open-Meteo.
- Hasła Wi-Fi i ustawienia są zapisywane w pamięci urządzenia, nie w kodzie.

## Biblioteki dołączone do szkicu

- `StQrCodeGen.c` / `StQrCodeGen.h` – kody QR na ekranie trybu konfiguracji; kopia biblioteki [QR Code generator](https://github.com/nayuki/QR-Code-generator) (Project Nayuki, licencja MIT) z prefiksem nazw zmienionym na `stqr_`.

## Licencja

Kod projektu: licencja MIT (plik `LICENSE`) – można go używać, zmieniać i rozpowszechniać, także w ramach własnych projektów, z zachowaniem informacji o autorze. Czcionki `FreeSans…Pl.h` pochodzą z GNU FreeFont (GPLv3 z wyjątkiem dla czcionek), biblioteka QR – Project Nayuki (MIT). Dane pogodowe: Open-Meteo (CC BY 4.0), IMGW-PIB, MSN Pogoda (dostęp nieoficjalny).
