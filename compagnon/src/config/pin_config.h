#pragma once

// ── AMOLED CO5300 QSPI (pins Waveshare officielles testées) ──────────────
#define LCD_CS      12
#define LCD_SCLK    38
#define LCD_SDIO0    4
#define LCD_SDIO1    5
#define LCD_SDIO2    6
#define LCD_SDIO3    7
#define LCD_RESET    2
#define LCD_WIDTH  480   // résolution physique CO5300
#define LCD_HEIGHT 480   // résolution physique CO5300

// Zone active LVGL (centrée dans LCD_WIDTH x LCD_HEIGHT, fond noir autour)
// offset X = (480-460)/2 = 10, offset Y = (480-470)/2 = 5
#define LV_SCREEN_W 460
#define LV_SCREEN_H 470
#define LV_SCREEN_X  10   // marge gauche/droite
#define LV_SCREEN_Y   5   // marge haut/bas

// ── I2C partagé (Touch CST9220 + IMU QMI8658 + PMIC AXP2101) ──────────
#define IIC_SDA     15
#define IIC_SCL     14

// ── Touch CST9220 ────────────────────────────────────────────────────
// TP_RST = LCD_RESET = pin 2 (réinitialise écran ET touch)
#define TOUCH_RES    2
#define TOUCH_INT   11

// ── PMIC AXP2101 ───────────────────────────────────────────────────────
#define AXP_INT     13
#define XPOWERS_CHIP_AXP2101

// ── IMU QMI8658 ──────────────────────────────────────────────────────────
// Adresse I2C : 0x6B (SA0=HIGH) ou 0x6A — autodetect dans imu.cpp

// ── Boutons physiques ───────────────────────────────────────────────────
#define BTN_LEFT    18   // carousel ◄ (appui = défile gauche)
#define BTN_RIGHT    0   // carousel ► / ouvrir (pin BOOT, actif LOW)

// ── Audio ES7210 (microphone) + ES8311 (sortie HP) ────────────────────
#define PIN_ES7210_BCLK   9
#define PIN_ES7210_LRCK  45
#define PIN_ES7210_DIN   10
#define PIN_ES7210_MCLK  16
#define PIN_ES8311_DOUT   8
#define PA               46   // amplificateur classe-D
