#include "sd_mgr.h"
#include "../config/pin_config.h"
#include <SD.h>
#include <SPI.h>
#include <freertos/semphr.h>

// ─── Pins SD (Waveshare ESP32-S3-AMOLED-2.16") ───────────────────────────────
// Centralisées ici — ne plus les définir dans les apps individuelles
#ifndef SD_CS
  #define SD_CS   48
#endif
#ifndef SD_MOSI
  #define SD_MOSI 47
#endif
#ifndef SD_SCK
  #define SD_SCK  40
#endif
#ifndef SD_MISO
  #define SD_MISO 41
#endif

static SPIClass        _sd_spi(HSPI);
static bool            _mounted  = false;
static SemaphoreHandle_t _mutex  = nullptr;

// ─── Guard RAII pour le mutex ─────────────────────────────────────────────────
struct SdLock {
    bool ok;
    SdLock()  { ok = (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE); }
    ~SdLock() { if (ok && _mutex) xSemaphoreGive(_mutex); }
};

// ─── Créer les répertoires parents récursivement ──────────────────────────────
static void mkdirs(const char* path) {
    char tmp[128];
    strlcpy(tmp, path, sizeof(tmp));
    // Trouver le dernier '/' pour ne créer que les parents
    char* last = strrchr(tmp, '/');
    if (!last || last == tmp) return;
    *last = '\0';
    // Créer chaque niveau
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!SD.exists(tmp)) SD.mkdir(tmp);
            *p = '/';
        }
    }
    if (!SD.exists(tmp)) SD.mkdir(tmp);
}

// ─── API publique ─────────────────────────────────────────────────────────────
void sd_mgr_init() {
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    _sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS, _sd_spi)) {
        Serial.println("[SD_MGR] SD non trouvée — vérifier pins");
        _mounted = false;
        return;
    }
    _mounted = true;
    uint64_t sz = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD_MGR] SD montée — %llu MB\n", sz);

    // Créer l'arborescence de base
    const char* dirs[] = {
        "/compagnon/meteo",
        "/compagnon/bourse",
        "/compagnon/nestor",
        "/compagnon/musique",
        "/compagnon/radars",
        nullptr
    };
    for (int i = 0; dirs[i]; i++) {
        if (!SD.exists(dirs[i])) SD.mkdir(dirs[i]);
    }
}

bool sd_mgr_available() { return _mounted; }

void sd_mgr_eject() {
    SdLock lk;
    SD.end();
    _mounted = false;
    Serial.println("[SD_MGR] SD démontée");
}

bool sd_write_json(const char* path, const char* json) {
    if (!_mounted) return false;
    SdLock lk;
    if (!lk.ok) return false;
    mkdirs(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) { Serial.printf("[SD_MGR] Écriture échouée: %s\n", path); return false; }
    size_t written = f.print(json);
    f.close();
    return written > 0;
}

String sd_read_json(const char* path) {
    if (!_mounted) return "";
    SdLock lk;
    if (!lk.ok) return "";
    if (!SD.exists(path)) return "";
    File f = SD.open(path, FILE_READ);
    if (!f) return "";
    String s = f.readString();
    f.close();
    return s;
}

bool sd_write_raw(const char* path, const uint8_t* buf, size_t len) {
    if (!_mounted || !buf) return false;
    SdLock lk;
    if (!lk.ok) return false;
    mkdirs(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t w = f.write(buf, len);
    f.close();
    return w == len;
}

size_t sd_read_raw(const char* path, uint8_t* buf, size_t max_len) {
    if (!_mounted || !buf) return 0;
    SdLock lk;
    if (!lk.ok) return 0;
    if (!SD.exists(path)) return 0;
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    size_t r = f.read(buf, max_len);
    f.close();
    return r;
}

bool sd_exists(const char* path) {
    if (!_mounted) return false;
    SdLock lk;
    return lk.ok && SD.exists(path);
}

bool sd_mkdir(const char* path) {
    if (!_mounted) return false;
    SdLock lk;
    if (!lk.ok) return false;
    mkdirs(path);
    return SD.mkdir(path);
}

bool sd_remove(const char* path) {
    if (!_mounted) return false;
    SdLock lk;
    return lk.ok && SD.remove(path);
}

int sd_list_dir(const char* dir_path, char* paths,
                int max_count, int max_path_len,
                const char* ext_filter) {
    if (!_mounted || !paths) return 0;
    SdLock lk;
    if (!lk.ok) return 0;
    File dir = SD.open(dir_path);
    if (!dir || !dir.isDirectory()) return 0;
    int count = 0;
    while (count < max_count) {
        File f = dir.openNextFile();
        if (!f) break;
        if (f.isDirectory()) { f.close(); continue; }
        const char* name = f.name();
        if (ext_filter) {
            int nlen = strlen(name);
            int elen = strlen(ext_filter);
            if (nlen < elen || strcasecmp(name + nlen - elen, ext_filter) != 0) {
                f.close(); continue;
            }
        }
        char* dest = paths + count * max_path_len;
        snprintf(dest, max_path_len, "%s/%s", dir_path, name);
        count++;
        f.close();
    }
    dir.close();
    return count;
}

File sd_open(const char* path, const char* mode) {
    if (!_mounted) return File();
    // Pas de mutex ici : le caller gère le cycle de vie du File
    // (usage streaming — ne pas mixer avec d'autres ops SD simultanées)
    return SD.open(path, mode);
}
