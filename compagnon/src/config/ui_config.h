#pragma once
#include "pin_config.h"  // source unique : LCD_WIDTH, LCD_HEIGHT, LCD_CORNER_RADIUS

// ─── Constantes de mise en page partagées ─────────────────────────────────────────────
// Toutes les apps et le launcher doivent utiliser ces constantes pour que
// le contenu soit toujours positionné SOUS la status bar.
//
// NE PAS redéfinir SCREEN_W / SCREEN_H ici — ils dérivent de LCD_WIDTH/HEIGHT.
// Pour changer la résolution, modifier UNIQUEMENT pin_config.h.

#define SCREEN_W  LCD_WIDTH
#define SCREEN_H  LCD_HEIGHT

#define STATUS_BAR_H  36   // hauteur status bar (doit == STATUS_H dans status_bar.cpp)

// Origine Y et hauteur disponible pour les apps (sous la status bar)
#define APP_Y  STATUS_BAR_H
#define APP_H  (SCREEN_H - STATUS_BAR_H)
