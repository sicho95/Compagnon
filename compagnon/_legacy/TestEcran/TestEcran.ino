/**
 * TestEcran.ino - Sketch minimal de diagnostic
 * Objectif : confirmer que le CO5300 s'allume independamment de
 *            LVGL / WiFi / XPowers / touch.
 *
 * Si l'ecran affiche du rouge : le hardware est OK, le probleme est
 *   dans le firmware principal (LVGL / WiFi / XPowers).
 * Si l'ecran reste noir ici aussi : probleme hardware (pin_config,
 *   alimentation, driver).
 *
 * Flasher ce sketch SEPARE (pas NestorOS) via :
 *   Fichier > Ouvrir > compagnon/TestEcran/TestEcran.ino
 */

#include <Arduino_GFX_Library.h>

// ── Pins Waveshare ESP32-S3 Touch AMOLED 2.16" ─────────────────────────
#define LCD_CS     10
#define LCD_SCLK    1
#define LCD_SDIO0   2
#define LCD_SDIO1   3
#define LCD_SDIO2   4
#define LCD_SDIO3   5
#define LCD_RESET  45
#define LCD_WIDTH  480
#define LCD_HEIGHT 480

// ── Objets GFX crees localement dans setup() via pointeurs ────────────
Arduino_DataBus *bus = nullptr;
Arduino_CO5300  *gfx = nullptr;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== TestEcran minimal ===");
  Serial.printf("Heap libre: %lu\n", (unsigned long)ESP.getFreeHeap());
  Serial.printf("PSRAM libre: %lu\n", (unsigned long)ESP.getFreePsram());

  // Reset hardware
  Serial.println("Reset LCD...");
  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, LOW);  delay(20);
  digitalWrite(LCD_RESET, HIGH); delay(120);

  // Creer bus et gfx ICI (pas en global)
  Serial.println("Creer bus QSPI...");
  bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK,
    LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

  Serial.println("Creer CO5300...");
  gfx = new Arduino_CO5300(
    bus, LCD_RESET,
    0, LCD_WIDTH, LCD_HEIGHT,
    0, 0, 0, 0);

  Serial.println("gfx->begin()...");
  if (!gfx->begin()) {
    Serial.println("ECHEC gfx->begin() !");
    Serial.println("Verifier les pins et la lib Arduino_GFX_Library.");
    while (1) { delay(1000); Serial.println("BLOQUE."); }
  }

  Serial.println("OK - Ecran initialise !");
  gfx->setBrightness(255);

  // Rouge plein -> ecran fonctionne
  gfx->fillScreen(RED);
  Serial.println("Ecran ROUGE affiche - hardware OK !");
}

void loop() {
  // Cycle de couleurs pour confirmer
  delay(1000); gfx->fillScreen(GREEN);  Serial.println("VERT");
  delay(1000); gfx->fillScreen(BLUE);   Serial.println("BLEU");
  delay(1000); gfx->fillScreen(WHITE);  Serial.println("BLANC");
  delay(1000); gfx->fillScreen(RED);    Serial.println("ROUGE");
}
