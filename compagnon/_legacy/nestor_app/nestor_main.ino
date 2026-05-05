/**
 * Nestor — Mode Compagnon
 * Point d'entrée de l'application Nestor sur ESP32-S3
 *
 * Logique métier partagée avec la PWA :
 *   compagnon/shared/brain/       <-> src/brain/
 *   compagnon/shared/orchestrator/ <-> src/orchestrator/
 */
#include <lvgl.h>
#include "../shared/brain/brain.h"
#include "../shared/orchestrator/orchestrator.h"

void nestor_app_init() {
  brain_init();
  orchestrator_init();
  // TODO : construire l'UI LVGL Nestor
}

void nestor_app_loop() {
  orchestrator_tick();
  lv_timer_handler();
  delay(5);
}
