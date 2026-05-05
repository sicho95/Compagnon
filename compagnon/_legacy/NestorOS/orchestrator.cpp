#include "orchestrator.h"
#include "brain.h"

static ActiveApp _current_app = APP_LAUNCHER;

void orchestrator_init() {
  brain_init();
  _current_app = APP_LAUNCHER;
  Serial.println("[ORCH] Orchestrateur initialisé");
}

void orchestrator_tick() {
  brain_tick();
  // TODO : vérification batterie (XPowersLib), WiFi, notifications
}

void orchestrator_set_app(ActiveApp app) {
  _current_app = app;
  Serial.printf("[ORCH] App active : %d\n", app);
}

ActiveApp orchestrator_get_app() {
  return _current_app;
}
