#pragma once

// ── Ecran AMOLED CO5300 (QSPI) ───────────────────────────────────────────
#define LCD_CS      10
#define LCD_SCLK     9
#define LCD_SDIO0    8
#define LCD_SDIO1    7
#define LCD_SDIO2    6
#define LCD_SDIO3    5
#define LCD_RESET   45
#define LCD_WIDTH  480
#define LCD_HEIGHT 480
#define SCREEN_W   480
#define SCREEN_H   480

// ── I2C partagé (AXP2101 + CST816 touch) ─────────────────────────────────
#define IIC_SDA  15
#define IIC_SCL  14

// ── Touch CST816 ──────────────────────────────────────────────────────────
#define TOUCH_RES  16   // pin RESET du CST816 (actif LOW)
#define TOUCH_INT  21   // pin INT du CST816  (optionnel)

// ── AXP2101 ───────────────────────────────────────────────────────────────
#define AXP_INT    13   // IRQ AXP2101 -> GPIO13
