#include "pmu.h"
#include "display.h"
#include "../config/pin_config.h"
#include <XPowersLib.h>
#include <Wire.h>
#include <esp_sleep.h>

static XPowersAXP2101 pmu;
static bool           pmu_ok     = false;
static bool           scr_on     = true;
static PmuCallback    _long_cb   = nullptr;

static volatile bool  _irq_fired = false;
static void IRAM_ATTR axp_isr()  { _irq_fired = true; }

void hal_pmu_init() {
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    pmu_ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (!pmu_ok) {
        Serial.println("[HAL/PMU] AXP2101 non detecte");
        return;
    }

    // XPowersLib reconfigure Wire → restaurer pour le touch CST9220
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    delay(10);

    // Diagnostic rails
    Serial.printf("[HAL/PMU] ALDO1 %s %dmV  ALDO3 %s %dmV\n",
        pmu.isEnableALDO1() ? "ON" : "OFF", pmu.getALDO1Voltage(),
        pmu.isEnableALDO3() ? "ON" : "OFF", pmu.getALDO3Voltage());

    // S'assurer que ALDO1 (AMOLED 2.8V) et ALDO3 (touch/logic 3.3V) sont actifs
    if (!pmu.isEnableALDO1()) { pmu.setALDO1Voltage(2800); pmu.enableALDO1(); }
    if (!pmu.isEnableALDO3()) { pmu.setALDO3Voltage(3300); pmu.enableALDO3(); }

    // Bouton power : court + long uniquement
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
    pmu.clearIrqStatus();

    pinMode(AXP_INT, INPUT_PULLUP);
    attachInterrupt(AXP_INT, axp_isr, FALLING);

    Serial.println("[HAL/PMU] AXP2101 OK");
}

void hal_pmu_tick() {
    if (!pmu_ok || !_irq_fired) return;
    _irq_fired = false;
    pmu.getIrqStatus();

    if (pmu.isPekeyShortPressIrq()) {
        if (scr_on) hal_pmu_screen_off();
        else        hal_pmu_screen_on();
    }
    if (pmu.isPekeyLongPressIrq()) {
        if (_long_cb) _long_cb();
        else          hal_pmu_enter_sleep();
    }
    pmu.clearIrqStatus();
}

int hal_pmu_battery_pct() {
    if (!pmu_ok) return -1;
    return (int)pmu.getBatteryPercent();
}

void hal_pmu_screen_off() {
    if (!scr_on) return;
    scr_on = false;
    if (gfx) { gfx->setBrightness(0); gfx->displayOff(); }
    Serial.println("[HAL/PMU] Ecran OFF");
}

void hal_pmu_screen_on() {
    if (scr_on) return;
    scr_on = true;
    if (gfx) {
        gfx->displayOn();
        gfx->setBrightness(200);
        // Restaurer MADCTL CO5300 après réveil : rotation=2 → 0xC0
        if (gfx_bus) gfx_bus->writeC8D8(0x36, 0xC0);
    }
    Serial.println("[HAL/PMU] Ecran ON");
}

void hal_pmu_enter_sleep() {
    Serial.println("[HAL/PMU] Veille (light sleep)...");
    hal_pmu_screen_off();
    // Reconfigurer IRQ pour réveil sur n'importe quelle pression bouton
    if (pmu_ok) {
        pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ);
        pmu.clearIrqStatus();
    }
    esp_sleep_enable_ext0_wakeup((gpio_num_t)AXP_INT, 0);
    esp_light_sleep_start();
    // ← Réveil ici
    Serial.println("[HAL/PMU] Reveil");
    if (pmu_ok) {
        pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
        pmu.clearIrqStatus();
    }
    _irq_fired = false;
    hal_pmu_screen_on();
}

void hal_pmu_shutdown() {
    Serial.println("[HAL/PMU] Arret complet...");
    hal_pmu_screen_off();
    delay(200);
    if (pmu_ok) pmu.shutdown();
    esp_deep_sleep_start();  // fallback si PMU indisponible
}

void hal_pmu_set_long_press_cb(PmuCallback cb) { _long_cb = cb; }
