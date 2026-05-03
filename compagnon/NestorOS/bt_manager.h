#pragma once
void bt_manager_init();       // Démarre Bluetooth Serial
void bt_manager_tick();       // Loop BT — traite les messages entrants
bool bt_is_active();          // true si BT démarré
void bt_manager_set_visible(bool visible); // Rendre visible/invisible aux téléphones
