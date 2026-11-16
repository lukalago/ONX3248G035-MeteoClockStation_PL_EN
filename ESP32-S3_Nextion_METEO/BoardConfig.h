#pragma once
// Piny i adresy I2C — przepisane 1:1 z oficjalnego przykładu
// OpenNextion/OpenNextion-SKU-ONX3248G035 (Example Programs/ESP-Arduino/01_touch_test)
// NIE ZMIENIAĆ bez ponownego sprawdzenia z oficjalnym repo.

#include <Arduino.h>

static const uint8_t NEXTION_IIC_SCL_PIN = 7;
static const uint8_t NEXTION_IIC_SDA_PIN = 8;

static const uint16_t LCD_H_RES = 320;
static const uint16_t LCD_V_RES = 480;

static const int8_t LCD_SCLK_PIN = 5;
static const int8_t LCD_MOSI_PIN = 1;
static const int8_t LCD_MISO_PIN = -1;
static const uint8_t LCD_DC_PIN = 3;
static const uint8_t LCD_CS_PIN = 2;
static const uint8_t LCD_BL_PIN = 6;

static const uint8_t CST826_ADDR = 0x15;
static const uint8_t PCF8574_ADDR = 0x38;

static const uint8_t PCF8574_PIN_LCD_RST = 6;

// BME280 na tej samej magistrali I2C (Grove), domyślny adres — biblioteka
// sama spróbuje 0x76, a w razie niepowodzenia 0x77 (patrz initSensor() w .ino)
static const uint8_t BME280_ADDR_PRIMARY = 0x76;
static const uint8_t BME280_ADDR_SECONDARY = 0x77;

// Bateria Li-ion (1 ogniwo, ladowarka IP4054 na plycie) — z oficjalnego przykladu
// Example Programs/ESP-Arduino/08_battery_test i schematu V1.2:
//  - napiecie ogniwa przez dzielnik 1,05 MOhm / 2,15 MOhm na GPIO4 (ADC1),
//    Vbat = Vadc * (Rgora + Rdol) / Rdol
//  - stan ladowania na wyjsciu P2 ekspandera PCF8574 (CHG_N); pin musi byc
//    ustawiony w stan wysoki (quasi-dwukierunkowy), zeby dalo sie go czytac.
static const uint8_t  PCF8574_PIN_CHG_N = 2;
static const uint8_t  BATTERY_ADC_PIN = 4;
static const uint32_t BATTERY_RESISTOR_UPPER = 1050000;
static const uint32_t BATTERY_RESISTOR_LOWER = 2150000;
// Oficjalny przyklad traktuje stan WYSOKI na CHG_N jako "laduje" (mimo nazwy _N).
// Jesli blyskawica na ikonie pokazuje sie odwrotnie (gdy ladowarka NIE laduje),
// zmien na false.
static const bool BATTERY_CHG_ACTIVE_HIGH = true;

// Opcjonalna przerobka: wykrywanie zasilacza USB. Dzielnik z linii VBUS (anoda D2) 4,7k / 4,7k do masy,
// srodek dzielnika na wolne wejscie P7 ekspandera PCF8574 (nozka 12 U4). Wlaczane w panelu WWW
// (Praca na baterii -> "Detekcja zasilacza (przerobka)"): stacja od razu wie, czy jest zasilacz.
static const uint8_t PCF8574_PIN_USB_SENSE = 7;
