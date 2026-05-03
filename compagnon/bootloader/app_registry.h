/**
 * Registre des applications du bootloader
 */
#pragma once
#include <lvgl.h>

typedef void (*app_launcher_t)(void);

typedef struct {
  const char     *name;        // Nom affiché
  const char     *subtitle;    // Sous-titre
  const char     *icon;        // Symbole LVGL
  app_launcher_t  launch;      // Fonction de lancement
} AppEntry;

// Prototypes de lancement
void app_launch_nestor(void);
void app_launch_placeholder(void);
void bootloader_ui_create(const AppEntry *apps, uint8_t count);
