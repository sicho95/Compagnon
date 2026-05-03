/**
 * bootloader_ui.h
 * Menu graphique de sélection d'application ("launcher").
 * Affiché au démarrage. L'utilisateur tape une carte pour
 * charger l'app correspondante — sans reboot, instantané.
 */
#pragma once

/** Affiche le launcher LVGL. À appeler après display_init(). */
void bootloader_ui_show();

/** Appelée par chaque app quand l'utilisateur veut "revenir". */
void bootloader_ui_return();
