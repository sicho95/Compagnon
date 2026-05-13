#pragma once
#include "pin_config.h"

// ─── Constantes de mise en page partagées ─────────────────────────────────────────────
// Toutes les apps et le launcher doivent utiliser ces constantes pour que
// le contenu soit toujours positionné SOUS la status bar ET dans la safe area.
//
// NE PAS redéfinir SCREEN_W / SCREEN_H ici — ils dérivent de LCD_WIDTH/HEIGHT.
// Pour changer la résolution, modifier UNIQUEMENT pin_config.h.
// Pour ajuster les marges du boîtier arrondi, modifier BORDER_H / BORDER_V.

// ─── Taille physique de l'écran ───────────────────────────────────────────────────────
#define SCREEN_W  LCD_WIDTH    // 480 px
#define SCREEN_H  LCD_HEIGHT   // 480 px

// ─── Bordures safe area (boîtier arrondi) ────────────────────────────
// BORDER_H : marges gauche ET droite (chacune)
// BORDER_V : marges haut ET bas (chacune, en plus de la status bar côté haut)
#define BORDER_H  20   // px — marge horizontale gauche/droite
#define BORDER_V  10   // px — marge verticale haut/bas

// ─── Zone d'affichage logique (centrée dans l'écran physique) ────────────────────────
// Origine absolue (coordonnées LVGL depuis le coin haut-gauche de l'écran physique)
#define UI_X   BORDER_H                          //  20 px depuis la gauche
#define UI_Y   BORDER_V                          //  10 px depuis le haut
// Dimensions utiles
#define UI_W   (SCREEN_W - 2 * BORDER_H)        // 480 - 40 = 440 px
#define UI_H   (SCREEN_H - 2 * BORDER_V)        // 480 - 20 = 460 px

// ─── Status bar ──────────────────────────────────────────────────────────────────────
#define STATUS_BAR_H  36   // hauteur status bar (doit == STATUS_H dans status_bar.cpp)

// ─── Zone des apps (sous la status bar, dans la safe area) ───────────────────────────
// APP_Y : coordonnée Y absolue où commence la zone app
// APP_H : hauteur disponible pour le contenu app
#define APP_Y   (UI_Y + STATUS_BAR_H)            //  10 + 36 = 46 px
#define APP_H   (UI_H - STATUS_BAR_H)            // 460 - 36 = 424 px
