#pragma once
#include <Arduino.h>
#include <FS.h>

// ─── Gestionnaire SD centralisé ──────────────────────────────────────────────
//
// Séparation Flash / SD :
//   Flash 16MB  → OS, apps compilées, assets LVGL, NVS config
//   SD Card     → caches apps (JSON), historiques, MP3, images
//
// Règle : toute donnée > 1KB persistante → SD via sd_mgr
//         config légère (clés, flags) → NVS (nvs_config.h)
//
// Chemins conventions :
//   /compagnon/meteo/cache.json
//   /compagnon/bourse/history.json
//   /compagnon/nestor/avatars/
//   /compagnon/musique/tracks/
//
// Thread-safety : toutes les fonctions sont appelables depuis Core 1 (LVGL)
//                 et Core 0 (tasks réseau). Un mutex interne protège SD.

// ── Init / état ──────────────────────────────────────────────────────────────
void sd_mgr_init();        // à appeler une fois dans setup(), après SPI init
bool sd_mgr_available();   // SD présente et montée ?
void sd_mgr_eject();       // démonter proprement (avant retrait physique)

// ── JSON (caches apps) ───────────────────────────────────────────────────────
// Écrire un JSON string dans un fichier (crée le répertoire si besoin)
bool   sd_write_json(const char* path, const char* json);
// Lire un fichier JSON → String vide si absent ou erreur
String sd_read_json(const char* path);

// ── Binaire brut (MP3, images) ───────────────────────────────────────────────
bool   sd_write_raw(const char* path, const uint8_t* buf, size_t len);
size_t sd_read_raw(const char* path, uint8_t* buf, size_t max_len);

// ── Utilitaires ──────────────────────────────────────────────────────────────
bool   sd_exists(const char* path);
bool   sd_mkdir(const char* path);    // crée récursivement
bool   sd_remove(const char* path);

// Lister les fichiers d'un répertoire
// Remplit paths[] (tableaux de max_count * max_path_len chars)
int    sd_list_dir(const char* dir_path,
                   char* paths, int max_count, int max_path_len,
                   const char* ext_filter = nullptr);  // ex: ".mp3"

// Accès FS direct (pour lecture streaming MP3)
File   sd_open(const char* path, const char* mode = "r");
