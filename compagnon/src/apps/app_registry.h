#pragma once
#include "app_base.h"

// ─── Registre centralisé des apps ────────────────────────────────────────────
//
// Usage :
//   app_registry_init();           // à appeler une fois dans setup()
//   app_registry_stop_all();       // ferme toutes les apps ouvertes
//   const AppDescriptor* d = app_registry_get(APP_METEO);
//   if (d) d->start();

void                  app_registry_init();
const AppDescriptor*  app_registry_get(AppId id);
void                  app_registry_stop_all();

// Nombre d'apps enregistrées
int                   app_registry_count();

// Itération : app_registry_get_all()[i] pour i in 0..count-1
const AppDescriptor*  app_registry_get_all();
