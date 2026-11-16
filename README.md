# ESP32-S3 Weather Station with Clock – Open Nextion ONX3248G035

🇬🇧 **English** | 🇵🇱 [Polski](README_PL.md)

<p align="center">
  <img width="320" alt="Main screen (PL)" src="https://github.com/user-attachments/assets/9986f638-dbd9-4402-bbdc-bd3205f76369" />
  &nbsp;&nbsp;
  <img width="320" alt="Main screen (EN)" src="https://github.com/user-attachments/assets/70a3eeeb-822e-480f-b424-a92db7e4d6b8" />
</p>

<p align="center"><i>Main screen in Polish and English</i></p>

**All screens:**

<img width="100%" alt="All screens (PL)" src="https://github.com/user-attachments/assets/5329e25c-35f6-4d5a-ba54-1fe18d2f1d1e" />
<img width="100%" alt="All screens (EN)" src="https://github.com/user-attachments/assets/0b4c5746-61a7-4551-8623-4220ee4d0c14" />

A home weather station built on the **Nextion ONX3248G035** module (ESP32-S3, 3.5" 320×480 touch screen).
It shows the current weather, forecast, air quality, sunrise and sunset, Moon phase and pollen,
and reminds you about bin collection days. The screen and the web panel can be switched between Polish and English;
everything is configured in the web panel.

## Features

- **Main screen:** clock, current weather with icon, temperature with trend, daily min/max, wind rose
  or Moon phase, pressure with trend, chance of rain, AQI, 3–4 day forecast, comfort bar.
- **Day details:** hourly forecast (tap a forecast column).
- **Day overview:** 24 h rain chart, sun path with day length, Moon phase, pollen.
- **Air and comfort:** AQI, PM2.5 and PM10, UV index, feels-like temperature, humidity, pressure, “tomorrow” summary.
- **Information:** Wi-Fi, data source, version, power, Restart and AP mode buttons.
- **Data sources:** Open-Meteo (no API key), optionally MSN Weather (with IMGW wind warnings – Only PL)
  and live readings from IMGW weather stations (Only PL); optional BME280 sensor.
- **Strong wind warning:** from IMGW or from the forecast, wind shown in warning colors.
- **Hazardous weather warnings ahead of time:** black ice, icy roads, snow, sleet, thunderstorm, hail – up to 12 h ahead,
  and in the evening until the next morning (e.g. “Black ice risk from 5:00”).
- **Bin collection reminder:** list of dates in the web panel (example: `docs/przyklad_harmonogram_smieci.txt`).
- **USB-C power with a Li-ion cell as backup:** battery icon, power saving (dimmed screen, Wi-Fi on only to download weather).
- **Web panel:** all settings, 3 Wi-Fi networks (main + 2 backups), `meteo.local` address (name can be changed), OTA update, switch to AP mode.
- **Polish / English language:** screen and web panel, selector at the top of the panel. IMGW warnings and stations are available in Polish only.
- **Setup mode (AP)** with a QR code to join the station's network.

## Hardware

- Nextion ONX3248G035 (ESP32-S3, ST7796, CST826 touch, PCF8574 expander, IP4054 charger).
  Schematic and manufacturer examples: <https://github.com/OpenNextion/OpenNextion-SKU-ONX3248G035>
- Optional: external BME280 sensor on I²C (Grove connector), Li-ion cell.
- Optional “charger detection” mod: a 4.7 kΩ / 4.7 kΩ divider from the VBUS line (anode of D2)
  to input P7 of the PCF8574 expander (pin 12 of U4), enabled in the web panel.

## Documentation

Full build guide (in Polish): [`docs/Stacja_pogody_ESP32_dokumentacja.docx`](docs/Stacja_pogody_ESP32_dokumentacja.docx).

## Building (Arduino IDE)

1. Install the **esp32** board package (Espressif, version 3.x) and select **ESP32S3 Dev Module**:
   Flash Size **16MB**, PSRAM **OPI PSRAM**, Partition Scheme **16M Flash (3MB APP/9.9MB FATFS)** (supports OTA), USB CDC On Boot **Disabled**.
2. Install the libraries: **GFX Library for Arduino** (Arduino_GFX), **Adafruit GFX**, **Adafruit CST8XX**,
   **Adafruit PCF8574**, **Adafruit BME280**, **ArduinoJson** (7.x), **ESPAsyncWebServer** (with **AsyncTCP**).
3. Open `ESP32-S3_Nextion_METEO/ESP32-S3_Nextion_METEO.ino`, compile and upload over USB.
4. Later versions can be uploaded over Wi-Fi: in Arduino IDE use *Sketch → Export Compiled Binary*, then upload the `.bin` file
   in the web panel (“Firmware update” section).

## First start

Without a saved network the station creates the access point **StacjaPogody-Setup** (QR code on the screen).
Join it with your phone, open the address shown on the screen (http://192.168.4.1), choose your Wi-Fi network and save.
To switch the panel and the screen to English, choose **English** in the “Język / Language” section at the top of the panel.
Afterwards the panel is available at **http://meteo.local** (the name can be changed in the panel) or at the station's IP address
(information screen: tap the Wi-Fi bars).

## Tools

- `tools/gen_icons.py` – weather icon generator (`WeatherIconsBmp.h`). Requires Python and Pillow:
  `python gen_icons.py WeatherIconsBmp.h`
- `tools/gen_fonts_aa.py` – generator of the anti-aliased `FreeSans…Pl.h` fonts (4-bit alpha per pixel, Polish characters). Requires Pillow and the GNU FreeFont fonts (FreeSans.ttf, FreeSansBold.ttf):
  `python gen_fonts_aa.py <folder_with_Pl.h_fonts> [output_folder]`

## Notes

- Access to MSN Weather is unofficial and may stop working – the station then falls back to Open-Meteo.
- Wi-Fi passwords and settings are stored in the device memory, not in the code.

## Bundled libraries

- `StQrCodeGen.c` / `StQrCodeGen.h` – QR codes on the setup screen; a copy of the [QR Code generator](https://github.com/nayuki/QR-Code-generator) library (Project Nayuki, MIT license) with the name prefix changed to `stqr_`.

## License

Project code: MIT license (`LICENSE` file) – you may use, modify and distribute it, also in your own projects, keeping the author notice. The `FreeSans…Pl.h` fonts are derived from GNU FreeFont (GPLv3 with font exception), the QR library is by Project Nayuki (MIT). Weather data: Open-Meteo (CC BY 4.0), IMGW-PIB, MSN Weather (unofficial access).
