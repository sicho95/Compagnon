#include "orchestrator.h"
#include "brain.h"

static ActiveApp _app = APP_LAUNCHER;

void orchestrator_init() { brain_init(); Serial.println("[ORCH] OK"); }
void orchestrator_tick() { brain_tick(); }
void orchestrator_set_app(ActiveApp a) { _app = a; Serial.printf("[ORCH] App: %d\n", a); }
ActiveApp orchestrator_get_app() { return _app; }
