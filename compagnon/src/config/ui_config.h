#pragma once

// ─── Constantes de mise en page partagées ─────────────────────────────────────
// Toutes les apps et le launcher doivent utiliser ces constantes pour que
// le contenu soit toujours positionné SOUS la status bar.

#define STATUS_BAR_H  36    // hauteur de la status bar en px (doit == STATUS_H dans status_bar.cpp)
#define SCREEN_W      480   // largeur écran AMOLED 2.16"
#define SCREEN_H      480   // hauteur écran AMOLED 2.16"

// Origine Y et hauteur disponible pour les apps (sous la status bar)
#define APP_Y   STATUS_BAR_H
#define APP_H   (SCREEN_H - STATUS_BAR_H)
