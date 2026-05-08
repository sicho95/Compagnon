// ─── App Musique — AirPlay sender + SD card + Spotify stub ────────────────
// Bibliothèques requises :
//   pschatzmann/arduino-audio-tools  (GitHub → Arduino Library Manager)
//   Activer dans arduino-audio-tools : AirplayClient, CodecMP3Helix

#include "musique_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../net/ble_mgr.h"
#include "../../config/pin_config.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUDP.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_random.h>

// ─── SD card SPI (Waveshare ESP32-S3-AMOLED-2.16") ──────────────────────
// Vérifier le schéma de la carte avant de flasher
#define SD_CS    48
#define SD_MOSI  47
#define SD_SCK   40
#define SD_MISO  41

// ─── Couleurs thème Musique (AMOLED noir + violet) ────────────────────────
#define C_BG      0x050010
#define C_CARD    0x1b0030
#define C_TXT     0xCC99FF
#define C_MUTED   0x664488
#define C_ACCENT  0x9933FF
#define C_DISABLED 0x333333

// ─── Constantes AirPlay ───────────────────────────────────────────────────
#define AIRPLAY_FRAMES_PER_PKT 352   // frames stéréo par paquet RTP
#define AIRPLAY_SAMPLE_RATE    44100
#define AIRPLAY_CHANNELS       2
#define AIRPLAY_BITS           16

// ─── Enceintes AirPlay découvertes ───────────────────────────────────────
struct AirplaySpeaker {
    char name[64];
    char ip[20];
    int  port;
};
static AirplaySpeaker _speakers[8];
static int            _nb_speakers = 0;

// ─── État global ──────────────────────────────────────────────────────────
static bool     _app_active    = false;
static bool     _sd_ok         = false;
static bool     _airplay_conn  = false;
static bool     _playing       = false;
static uint8_t  _volume        = 70;   // 0-100
static char     _current_track[128] = {};
static char     _current_artist[64] = {};
static char     _airplay_name[64]   = {};
static uint32_t _tick_audio         = 0;

// ─── Écrans LVGL ─────────────────────────────────────────────────────────
static lv_obj_t *_scr_main     = nullptr;
static lv_obj_t *_scr_sources  = nullptr;
static lv_obj_t *_lbl_track    = nullptr;
static lv_obj_t *_lbl_artist   = nullptr;
static lv_obj_t *_lbl_airplay  = nullptr;
static lv_obj_t *_lbl_status   = nullptr;
static lv_obj_t *_btn_play     = nullptr;
static lv_obj_t *_slider_vol   = nullptr;
static lv_obj_t *_dd_speakers  = nullptr;
static lv_obj_t *_list_files   = nullptr;
static lv_obj_t *_lbl_spotify  = nullptr;

// ─── BitWriter (flux de bits big-endian pour ALAC) ───────────────────────
struct BitWriter {
    uint8_t *buf;
    int      byte_pos;
    uint8_t  cur;
    int      bits_left;  // bits libres dans cur

    void init(uint8_t *b) {
        buf = b; byte_pos = 0; cur = 0; bits_left = 8;
    }
    void write(uint32_t val, int nbits) {
        while (nbits > 0) {
            int take = (nbits < bits_left) ? nbits : bits_left;
            int shift = nbits - take;
            cur |= ((val >> shift) & ((1u << take) - 1)) << (bits_left - take);
            bits_left -= take;
            nbits     -= take;
            if (bits_left == 0) { buf[byte_pos++] = cur; cur = 0; bits_left = 8; }
        }
    }
    void flush() {
        if (bits_left < 8) buf[byte_pos++] = cur;
    }
    int size() { return byte_pos; }
};

// ─── Encoder un frame PCM en ALAC verbatim (mode non compressé) ──────────
// Retourne le nombre d'octets écrits dans out
static int alac_encode_verbatim(const int16_t *pcm, int nframes, uint8_t *out) {
    BitWriter bw;
    bw.init(out);
    // En-tête CPE (channel pair element) — mode non compressé
    bw.write(1, 3);   // element type = CPE (1)
    bw.write(0, 4);   // instance = 0
    bw.write(0, 1);   // unused
    bw.write(0, 2);   // unused
    bw.write(1, 1);   // isNotCompressed = 1
    // Échantillons PCM 16 bits, entrelacés L/R, big-endian
    for (int i = 0; i < nframes * AIRPLAY_CHANNELS; i++) {
        bw.write((uint16_t)pcm[i], AIRPLAY_BITS);
    }
    bw.flush();
    return bw.size();
}

// ─── Construire un paquet RTP ────────────────────────────────────────────
static int build_rtp_packet(uint8_t *out, uint16_t seq, uint32_t ts,
                             uint32_t ssrc, const uint8_t *payload, int plen,
                             bool marker) {
    out[0] = 0x80;
    out[1] = (marker ? 0x80 : 0x00) | 96;
    out[2] = (seq >> 8) & 0xFF;
    out[3] = seq & 0xFF;
    out[4] = (ts >> 24) & 0xFF; out[5] = (ts >> 16) & 0xFF;
    out[6] = (ts >> 8) & 0xFF;  out[7] = ts & 0xFF;
    out[8] = (ssrc >> 24) & 0xFF; out[9] = (ssrc >> 16) & 0xFF;
    out[10] = (ssrc >> 8) & 0xFF; out[11] = ssrc & 0xFF;
    memcpy(out + 12, payload, plen);
    return 12 + plen;
}

// ─── Client AirPlay 1 minimal (RTSP/TCP + RTP/UDP) ───────────────────────
class AirplayClient {
    WiFiClient _tcp;
    WiFiUDP    _udp;
    int        _rtp_remote_port = 6000;
    uint16_t   _seq   = 0;
    uint32_t   _ts    = 0;
    uint32_t   _ssrc  = 0;
    int        _cseq  = 0;
    char       _session_path[80] = {};
    bool       _ok = false;

    // Envoyer une requête RTSP et retourner le code de statut
    int _rtsp_req(const char *method, const char *hdrs, const char *body) {
        int blen = body ? strlen(body) : 0;
        String req = String(method) + " " + _session_path + " RTSP/1.0\r\n";
        req += "CSeq: " + String(_cseq++) + "\r\n";
        req += "User-Agent: Nestor/1.0\r\n";
        req += "Client-Instance: NESTORCOMPAGNON01\r\n";
        if (hdrs) req += hdrs;
        if (blen > 0) {
            req += "Content-Type: application/sdp\r\n";
            req += "Content-Length: " + String(blen) + "\r\n";
        }
        req += "\r\n";
        if (body) req += body;
        _tcp.print(req);

        String first = _tcp.readStringUntil('\n');
        int code = 0;
        if (first.startsWith("RTSP/1.0 ")) code = first.substring(9, 12).toInt();

        // Lire et analyser les en-têtes de réponse
        while (_tcp.connected()) {
            String line = _tcp.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break;
            // Extraire server_port depuis Transport: ...;server_port=NNNN
            int sp = line.indexOf("server_port=");
            if (sp >= 0) _rtp_remote_port = line.substring(sp + 12).toInt();
        }
        return code;
    }

public:
    bool connect(const char *ip, int port) {
        _ssrc = esp_random();
        _seq  = esp_random() & 0xFFFF;
        _ts   = esp_random();
        _cseq = 0;
        _ok   = false;

        if (!WiFi.isConnected()) return false;
        if (!_tcp.connect(ip, port)) return false;
        _tcp.setTimeout(4000);

        char local_ip[20];
        IPAddress loc = WiFi.localIP();
        snprintf(local_ip, sizeof(local_ip), "%d.%d.%d.%d", loc[0], loc[1], loc[2], loc[3]);
        uint32_t sid = esp_random();
        snprintf(_session_path, sizeof(_session_path), "rtsp://%s/%u", ip, sid);

        // ANNOUNCE — envoyer le descripteur SDP
        char sdp[512];
        snprintf(sdp, sizeof(sdp),
            "v=0\r\n"
            "o=iTunes %u O IN IP4 %s\r\n"
            "s=iTunes\r\n"
            "c=IN IP4 %s\r\n"
            "t=0 0\r\n"
            "m=audio 0 RTP/AVP 96\r\n"
            "a=rtpmap:96 AppleLossless/44100/2\r\n"
            "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n",
            sid, local_ip, ip);

        if (_rtsp_req("ANNOUNCE", nullptr, sdp) != 200) {
            _tcp.stop(); return false;
        }

        // SETUP — demander le port RTP du serveur
        int local_rtp = 5100 + (esp_random() % 500);
        char setup_hdr[128];
        snprintf(setup_hdr, sizeof(setup_hdr),
                 "Transport: RTP/AVP/UDP;unicast;mode=record;client_port=%d-%d\r\n",
                 local_rtp, local_rtp + 1);
        // Envoyer SETUP manuellement pour capturer server_port
        String req = "SETUP " + String(_session_path) + "/streamid=0 RTSP/1.0\r\n";
        req += "CSeq: " + String(_cseq++) + "\r\n";
        req += "User-Agent: Nestor/1.0\r\n";
        req += String(setup_hdr) + "\r\n";
        _tcp.print(req);
        // Lire réponse SETUP
        String first = _tcp.readStringUntil('\n');
        int code = first.startsWith("RTSP/1.0 ") ? first.substring(9, 12).toInt() : 0;
        while (_tcp.connected()) {
            String line = _tcp.readStringUntil('\n'); line.trim();
            if (line.length() == 0) break;
            int sp = line.indexOf("server_port=");
            if (sp >= 0) _rtp_remote_port = line.substring(sp + 12).toInt();
        }
        if (code != 200) { _tcp.stop(); return false; }

        // Démarrer UDP RTP local
        _udp.begin(local_rtp);

        // RECORD — démarrer la session
        char rec_hdr[80];
        snprintf(rec_hdr, sizeof(rec_hdr),
                 "Range: npt=0-\r\nRTP-Info: seq=%u;rtptime=%u\r\n", _seq, _ts);
        if (_rtsp_req("RECORD", rec_hdr, nullptr) != 200) {
            _tcp.stop(); _udp.stop(); return false;
        }
        _ok = true;
        Serial.printf("[AIRPLAY] Connecte %s:%d → rtp_port=%d\n", ip, port, _rtp_remote_port);
        return true;
    }

    // Envoyer un frame audio PCM (nframes frames stéréo 16-bit)
    bool send_pcm(const int16_t *pcm, int nframes, const char *server_ip) {
        if (!_ok || !_tcp.connected()) return false;
        static uint8_t alac_buf[4 + AIRPLAY_FRAMES_PER_PKT * AIRPLAY_CHANNELS * 2];
        static uint8_t rtp_buf[12 + sizeof(alac_buf)];
        int alen = alac_encode_verbatim(pcm, nframes, alac_buf);
        int plen = build_rtp_packet(rtp_buf, _seq, _ts, _ssrc, alac_buf, alen, _seq == 0);
        _udp.beginPacket(server_ip, _rtp_remote_port);
        _udp.write(rtp_buf, plen);
        _udp.endPacket();
        _seq++;
        _ts += nframes;
        return true;
    }

    // Envoyer SET_PARAMETER pour le volume (0.0 à 1.0 → -30 dB à 0 dB)
    void set_volume(uint8_t vol_pct) {
        if (!_ok) return;
        float db = -30.0f + (vol_pct / 100.0f) * 30.0f;
        char body[64], hdr[64];
        snprintf(body, sizeof(body), "volume: %.2f\r\n", db);
        snprintf(hdr, sizeof(hdr), "Content-Type: text/parameters\r\n"
                                   "Content-Length: %d\r\n", (int)strlen(body));
        String req = "SET_PARAMETER " + String(_session_path) + " RTSP/1.0\r\n";
        req += "CSeq: " + String(_cseq++) + "\r\n";
        req += hdr + String("\r\n") + body;
        _tcp.print(req);
        // Vider la réponse
        while (_tcp.available()) _tcp.read();
    }

    void disconnect() { _tcp.stop(); _udp.stop(); _ok = false; }
    bool connected()  { return _ok && _tcp.connected(); }
};

static AirplayClient _airplay_client;
static char          _connected_ip[20] = {};

// ─── Spotify stub ────────────────────────────────────────────────────────
// TODO: intégrer SpotifyArduino (knochenhans/spotify-api-arduino) quand
//       la librairie est disponible et que le support OAuth PKCE ESP32 est stable.
static void spotify_connect_init() {
    Serial.println("[MUSIQUE] Spotify stub — non implémenté");
}

// ─── Découverte mDNS AirPlay ──────────────────────────────────────────────
// Retourne le nombre d'enceintes trouvées et remplit _speakers[]
static int scan_airplay_speakers() {
    _nb_speakers = 0;
    if (!WiFi.isConnected()) return 0;

    // _raop._tcp = protocole AirPlay 1 (RAOP)
    int n = MDNS.queryService("raop", "tcp");
    for (int i = 0; i < n && _nb_speakers < 8; i++) {
        strlcpy(_speakers[_nb_speakers].ip,
                MDNS.IP(i).toString().c_str(), sizeof(_speakers[0].ip));
        _speakers[_nb_speakers].port = MDNS.port(i);
        // Le nom RAOP est "MACADDR@Nom appareil" → extraire la partie après @
        String sname = MDNS.hostname(i);
        int at = sname.indexOf('@');
        if (at >= 0) sname = sname.substring(at + 1);
        strlcpy(_speakers[_nb_speakers].name, sname.c_str(), sizeof(_speakers[0].name));
        _nb_speakers++;
    }

    // Fallback : scanner aussi _airplay._tcp (AirPlay 1 annoncé différemment)
    if (_nb_speakers == 0) {
        n = MDNS.queryService("airplay", "tcp");
        for (int i = 0; i < n && _nb_speakers < 8; i++) {
            strlcpy(_speakers[_nb_speakers].ip,
                    MDNS.IP(i).toString().c_str(), sizeof(_speakers[0].ip));
            _speakers[_nb_speakers].port = MDNS.port(i);
            strlcpy(_speakers[_nb_speakers].name,
                    MDNS.hostname(i).c_str(), sizeof(_speakers[0].name));
            _nb_speakers++;
        }
    }
    Serial.printf("[MUSIQUE] %d enceinte(s) AirPlay trouvee(s)\n", _nb_speakers);
    return _nb_speakers;
}

// ─── SD card — initialiser et lister les fichiers audio ──────────────────
static SPIClass _sd_spi(HSPI);
static bool     _sd_files_loaded = false;

static bool sd_init() {
    _sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS, _sd_spi)) {
        Serial.println("[MUSIQUE] SD non trouvee — verifier pins");
        return false;
    }
    Serial.println("[MUSIQUE] SD OK");
    return true;
}

struct AudioFile { char path[128]; };
static AudioFile _audio_files[64];
static int       _nb_files = 0;

// Lister les .mp3 et .flac à la racine et dans /Music/
static void list_audio_files() {
    _nb_files = 0;
    const char *dirs[] = { "/", "/Music" };
    for (const char *dir : dirs) {
        File d = SD.open(dir);
        if (!d) continue;
        while (_nb_files < 64) {
            File f = d.openNextFile();
            if (!f) break;
            if (f.isDirectory()) { f.close(); continue; }
            const char *n = f.name();
            int nlen = strlen(n);
            bool is_mp3  = nlen > 4 && strcasecmp(n + nlen - 4, ".mp3")  == 0;
            bool is_flac = nlen > 5 && strcasecmp(n + nlen - 5, ".flac") == 0;
            if (is_mp3 || is_flac) {
                snprintf(_audio_files[_nb_files].path,
                         sizeof(_audio_files[0].path), "%s/%s", dir, n);
                _nb_files++;
            }
            f.close();
        }
        d.close();
    }
    Serial.printf("[MUSIQUE] %d fichiers audio sur SD\n", _nb_files);
}

// ─── Notifications BLE vers la PWA ───────────────────────────────────────
static void notify_speakers() {
    DynamicJsonDocument doc(1024);
    JsonArray arr = doc.createNestedArray("speakers");
    for (int i = 0; i < _nb_speakers; i++) {
        JsonObject o = arr.createNestedObject();
        o["name"] = _speakers[i].name;
        o["ip"]   = _speakers[i].ip;
        o["port"] = _speakers[i].port;
    }
    char buf[1024];
    serializeJson(doc, buf, sizeof(buf));
    ble_mgr_music_notify(buf);
}

static void notify_files() {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("files");
    for (int i = 0; i < _nb_files; i++) {
        JsonObject o = arr.createNestedObject();
        // Extraire le nom depuis le chemin complet
        const char *p = strrchr(_audio_files[i].path, '/');
        o["name"] = p ? p + 1 : _audio_files[i].path;
        o["path"] = _audio_files[i].path;
    }
    char buf[2048];
    serializeJson(doc, buf, sizeof(buf));
    ble_mgr_music_notify(buf);
}

static void notify_status() {
    DynamicJsonDocument doc(256);
    doc["playing"] = _playing;
    doc["track"]   = _current_track;
    doc["artist"]  = _current_artist;
    doc["vol"]     = _volume;
    doc["airplay"] = _airplay_name;
    doc["ok"]      = _airplay_conn;
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    ble_mgr_music_notify(buf);
}

// ─── Mise à jour UI ───────────────────────────────────────────────────────
static void ui_update_track() {
    if (!_scr_main) return;
    if (_lbl_track)  lv_label_set_text(_lbl_track,  _current_track[0] ? _current_track : "---");
    if (_lbl_artist) lv_label_set_text(_lbl_artist, _current_artist[0] ? _current_artist : "");
    if (_lbl_airplay) {
        char buf[80];
        snprintf(buf, sizeof(buf), _airplay_conn ? "%s  \xE2\x97\x8F" : "%s",
                 _airplay_name[0] ? _airplay_name : "Non connecte");
        lv_label_set_text(_lbl_airplay, buf);
        lv_obj_set_style_text_color(_lbl_airplay,
            lv_color_hex(_airplay_conn ? C_ACCENT : C_MUTED), 0);
    }
    if (_btn_play) {
        lv_obj_t *lbl = lv_obj_get_child(_btn_play, 0);
        if (lbl) lv_label_set_text(lbl, _playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    if (_slider_vol) lv_slider_set_value(_slider_vol, _volume, LV_ANIM_OFF);
}

static void ui_update_dropdown() {
    if (!_dd_speakers) return;
    String opts = "";
    if (_nb_speakers == 0) {
        opts = "Aucune enceinte";
    } else {
        for (int i = 0; i < _nb_speakers; i++) {
            if (i > 0) opts += "\n";
            opts += _speakers[i].name;
        }
    }
    lv_dropdown_set_options(_dd_speakers, opts.c_str());
}

static void ui_update_files() {
    if (!_list_files) return;
    lv_obj_clean(_list_files);
    if (!_sd_ok) {
        lv_obj_t *lbl = lv_label_create(_list_files);
        lv_label_set_text(lbl, "SD non détectée");
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
        return;
    }
    for (int i = 0; i < _nb_files && i < 32; i++) {
        const char *p = strrchr(_audio_files[i].path, '/');
        const char *name = p ? p + 1 : _audio_files[i].path;
        // Stocker l'index comme userdata pour le callback
        lv_obj_t *btn_f = lv_list_add_btn(_list_files, LV_SYMBOL_AUDIO, name);
        lv_obj_set_style_text_color(btn_f, lv_color_hex(C_TXT), 0);
        lv_obj_set_style_bg_color(btn_f, lv_color_hex(C_BG), 0);
        lv_obj_set_user_data(btn_f, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn_f, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
            if (idx < 0 || idx >= _nb_files) return;
            strlcpy(_current_track,
                    strrchr(_audio_files[idx].path, '/') ? strrchr(_audio_files[idx].path, '/') + 1
                                                          : _audio_files[idx].path,
                    sizeof(_current_track));
            _current_artist[0] = '\0';
            _playing = true;
            ui_update_track();
            Serial.printf("[MUSIQUE] Lecture : %s\n", _audio_files[idx].path);
            // TODO: brancher AudioPlayer quand arduino-audio-tools est installé
            notify_status();
        }, LV_EVENT_CLICKED, NULL);
    }
    if (_nb_files == 0) {
        lv_obj_t *lbl = lv_label_create(_list_files);
        lv_label_set_text(lbl, "Aucun fichier .mp3/.flac");
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
    }
}

// ─── Callbacks boutons UI ─────────────────────────────────────────────────
static void cb_back(lv_event_t *) {
    musique_app_stop();
    ui_launcher_return();
}

static void cb_play(lv_event_t *) {
    _playing = !_playing;
    ui_update_track();
    notify_status();
}

static void cb_prev(lv_event_t *) {
    // Passer au fichier précédent
    notify_status();
}

static void cb_next(lv_event_t *) {
    // Passer au fichier suivant
    notify_status();
}

static void cb_volume(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    _volume = (uint8_t)lv_slider_get_value(slider);
    if (_airplay_conn) _airplay_client.set_volume(_volume);
    notify_status();
}

static void cb_scan_speakers(lv_event_t *) {
    if (_lbl_status) lv_label_set_text(_lbl_status, "Recherche...");
    lv_timer_handler();
    scan_airplay_speakers();
    ui_update_dropdown();
    if (_lbl_status) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%d enceinte(s) trouvee(s)", _nb_speakers);
        lv_label_set_text(_lbl_status, buf);
    }
    notify_speakers();
}

static void cb_connect_speaker(lv_event_t *) {
    if (!_dd_speakers) return;
    uint16_t sel = lv_dropdown_get_selected(_dd_speakers);
    if (sel >= _nb_speakers) return;
    if (_lbl_status) lv_label_set_text(_lbl_status, "Connexion AirPlay...");
    lv_timer_handler();

    _airplay_client.disconnect();
    strlcpy(_connected_ip, _speakers[sel].ip, sizeof(_connected_ip));
    strlcpy(_airplay_name, _speakers[sel].name, sizeof(_airplay_name));
    _airplay_conn = _airplay_client.connect(_speakers[sel].ip, _speakers[sel].port);

    if (_lbl_status) {
        lv_label_set_text(_lbl_status, _airplay_conn ? "" : "Echec AirPlay");
        lv_obj_set_style_text_color(_lbl_status,
            lv_color_hex(_airplay_conn ? C_ACCENT : 0xFF4444), 0);
    }
    _airplay_client.set_volume(_volume);
    ui_update_track();
    notify_status();
}

// Basculer vers l'écran des sources (SD / Spotify / Amazon)
static void cb_sources(lv_event_t *);

static void cb_back_sources(lv_event_t *) {
    if (_scr_main) lv_scr_load_anim(_scr_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

// ─── Construire l'écran principal ─────────────────────────────────────────
static void build_main_screen() {
    _scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr_main, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr_main, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr_main, LV_OBJ_FLAG_SCROLLABLE);

    // ── Bouton retour ───────────────────────────────────────────────────
    lv_obj_t *btn_back = lv_btn_create(_scr_main);
    lv_obj_set_size(btn_back, 52, 36);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(C_BG), 0);
    lv_obj_set_style_radius(btn_back, 10, 0);
    lv_obj_add_event_cb(btn_back, cb_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic_back = lv_label_create(btn_back);
    lv_label_set_text(ic_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ic_back, lv_color_hex(C_TXT), 0);
    lv_obj_center(ic_back);

    // ── Titre ────────────────────────────────────────────────────────────
    lv_obj_t *title = lv_label_create(_scr_main);
    lv_label_set_text(title, "Musique");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    // ── Bouton sources (☰) ───────────────────────────────────────────────
    lv_obj_t *btn_src = lv_btn_create(_scr_main);
    lv_obj_set_size(btn_src, 52, 36);
    lv_obj_align(btn_src, LV_ALIGN_TOP_RIGHT, -10, 46);
    lv_obj_set_style_bg_color(btn_src, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(btn_src, LV_OPA_50, 0);
    lv_obj_set_style_radius(btn_src, 10, 0);
    lv_obj_add_event_cb(btn_src, cb_sources, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic_src = lv_label_create(btn_src);
    lv_label_set_text(ic_src, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(ic_src, lv_color_hex(C_TXT), 0);
    lv_obj_center(ic_src);

    // ── Ligne enceinte + scan ────────────────────────────────────────────
    _dd_speakers = lv_dropdown_create(_scr_main);
    lv_obj_set_size(_dd_speakers, 280, 40);
    lv_obj_align(_dd_speakers, LV_ALIGN_TOP_LEFT, 10, 98);
    lv_obj_set_style_bg_color(_dd_speakers, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_text_color(_dd_speakers, lv_color_hex(C_TXT), 0);
    lv_obj_set_style_border_color(_dd_speakers, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_opa(_dd_speakers, LV_OPA_30, 0);
    lv_dropdown_set_options(_dd_speakers, "Aucune enceinte");

    lv_obj_t *btn_scan = lv_btn_create(_scr_main);
    lv_obj_set_size(btn_scan, 56, 40);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_LEFT, 298, 98);
    lv_obj_set_style_bg_color(btn_scan, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(btn_scan, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_scan, 10, 0);
    lv_obj_add_event_cb(btn_scan, cb_scan_speakers, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic_scan = lv_label_create(btn_scan);
    lv_label_set_text(ic_scan, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(ic_scan, lv_color_hex(C_TXT), 0);
    lv_obj_center(ic_scan);

    lv_obj_t *btn_conn = lv_btn_create(_scr_main);
    lv_obj_set_size(btn_conn, 104, 40);
    lv_obj_align(btn_conn, LV_ALIGN_TOP_LEFT, 362, 98);
    lv_obj_set_style_bg_color(btn_conn, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_opa(btn_conn, LV_OPA_70, 0);
    lv_obj_set_style_radius(btn_conn, 10, 0);
    lv_obj_add_event_cb(btn_conn, cb_connect_speaker, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic_conn = lv_label_create(btn_conn);
    lv_label_set_text(ic_conn, "Connecter");
    lv_obj_set_style_text_font(ic_conn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ic_conn, lv_color_white(), 0);
    lv_obj_center(ic_conn);

    // ── Statut AirPlay ───────────────────────────────────────────────────
    _lbl_airplay = lv_label_create(_scr_main);
    lv_label_set_text(_lbl_airplay, "Non connecte");
    lv_obj_set_style_text_font(_lbl_airplay, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_airplay, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_airplay, LV_ALIGN_TOP_MID, 0, 148);

    // ── Zone centrale : icône + titre + artiste ──────────────────────────
    lv_obj_t *card_center = lv_obj_create(_scr_main);
    lv_obj_set_size(card_center, 340, 150);
    lv_obj_align(card_center, LV_ALIGN_TOP_MID, 0, 174);
    lv_obj_set_style_bg_color(card_center, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(card_center, LV_OPA_50, 0);
    lv_obj_set_style_border_color(card_center, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(card_center, 1, 0);
    lv_obj_set_style_border_opa(card_center, LV_OPA_40, 0);
    lv_obj_set_style_radius(card_center, 18, 0);
    lv_obj_clear_flag(card_center, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ico_note = lv_label_create(card_center);
    lv_label_set_text(ico_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(ico_note, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico_note, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(ico_note, LV_ALIGN_TOP_MID, 0, 10);

    _lbl_track = lv_label_create(card_center);
    lv_label_set_text(_lbl_track, "---");
    lv_obj_set_style_text_font(_lbl_track, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_track, lv_color_hex(C_TXT), 0);
    lv_obj_set_style_text_align(_lbl_track, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_lbl_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(_lbl_track, 300);
    lv_obj_align(_lbl_track, LV_ALIGN_BOTTOM_MID, 0, -28);

    _lbl_artist = lv_label_create(card_center);
    lv_label_set_text(_lbl_artist, "");
    lv_obj_set_style_text_font(_lbl_artist, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_artist, lv_color_hex(C_MUTED), 0);
    lv_obj_set_style_text_align(_lbl_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(_lbl_artist, 300);
    lv_obj_align(_lbl_artist, LV_ALIGN_BOTTOM_MID, 0, -8);

    // ── Contrôles lecture ────────────────────────────────────────────────
    static const int BTN_Y = 340;
    static const int BTN_SZ = 56;

    auto make_ctrl_btn = [&](const char *icon, int x, lv_event_cb_t cb) {
        lv_obj_t *b = lv_btn_create(_scr_main);
        lv_obj_set_size(b, BTN_SZ, BTN_SZ);
        lv_obj_set_pos(b, x, BTN_Y);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, BTN_SZ / 2, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_ACCENT), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(b);
        lv_label_set_text(lbl, icon);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_TXT), 0);
        lv_obj_center(lbl);
        return b;
    };

    int cx = (480 - 3 * BTN_SZ - 2 * 24) / 2;
    make_ctrl_btn(LV_SYMBOL_PREV, cx,             cb_prev);
    _btn_play = make_ctrl_btn(LV_SYMBOL_PLAY, cx + BTN_SZ + 24, cb_play);
    make_ctrl_btn(LV_SYMBOL_NEXT, cx + 2 * (BTN_SZ + 24), cb_next);

    // ── Slider volume ────────────────────────────────────────────────────
    lv_obj_t *lbl_vol = lv_label_create(_scr_main);
    lv_label_set_text(lbl_vol, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(lbl_vol, lv_color_hex(C_MUTED), 0);
    lv_obj_align(lbl_vol, LV_ALIGN_TOP_LEFT, 20, 410);

    _slider_vol = lv_slider_create(_scr_main);
    lv_obj_set_size(_slider_vol, 400, 12);
    lv_obj_align(_slider_vol, LV_ALIGN_TOP_MID, 0, 415);
    lv_slider_set_range(_slider_vol, 0, 100);
    lv_slider_set_value(_slider_vol, _volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_slider_vol, lv_color_hex(C_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_slider_vol, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_slider_vol, lv_color_hex(C_TXT), LV_PART_KNOB);
    lv_obj_add_event_cb(_slider_vol, cb_volume, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Label statut ─────────────────────────────────────────────────────
    _lbl_status = lv_label_create(_scr_main);
    lv_label_set_text(_lbl_status, _sd_ok ? "" : "SD non détectée");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ─── Construire l'écran sources ──────────────────────────────────────────
static void build_sources_screen() {
    _scr_sources = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr_sources, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr_sources, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr_sources, LV_OBJ_FLAG_SCROLLABLE);

    // Bouton retour
    lv_obj_t *btn_b = lv_btn_create(_scr_sources);
    lv_obj_set_size(btn_b, 52, 36);
    lv_obj_align(btn_b, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn_b, lv_color_hex(C_BG), 0);
    lv_obj_set_style_radius(btn_b, 10, 0);
    lv_obj_add_event_cb(btn_b, cb_back_sources, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic_b = lv_label_create(btn_b);
    lv_label_set_text(ic_b, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ic_b, lv_color_hex(C_TXT), 0);
    lv_obj_center(ic_b);

    lv_obj_t *ttl = lv_label_create(_scr_sources);
    lv_label_set_text(ttl, "Sources");
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ttl, lv_color_hex(C_TXT), 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 48);

    // ── Section SD card ──────────────────────────────────────────────────
    lv_obj_t *lbl_sd = lv_label_create(_scr_sources);
    lv_label_set_text(lbl_sd, _sd_ok ? LV_SYMBOL_SD_CARD " SD card" : LV_SYMBOL_SD_CARD " SD (absent)");
    lv_obj_set_style_text_font(lbl_sd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_sd, lv_color_hex(_sd_ok ? C_TXT : C_MUTED), 0);
    lv_obj_align(lbl_sd, LV_ALIGN_TOP_LEFT, 16, 96);

    // Liste scrollable des fichiers
    _list_files = lv_list_create(_scr_sources);
    lv_obj_set_size(_list_files, 460, 170);
    lv_obj_align(_list_files, LV_ALIGN_TOP_MID, 0, 124);
    lv_obj_set_style_bg_color(_list_files, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(_list_files, LV_OPA_40, 0);
    lv_obj_set_style_radius(_list_files, 14, 0);
    lv_obj_set_style_border_color(_list_files, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_opa(_list_files, LV_OPA_20, 0);
    ui_update_files();

    // ── Section Spotify ──────────────────────────────────────────────────
    lv_obj_t *card_sp = lv_obj_create(_scr_sources);
    lv_obj_set_size(card_sp, 460, 76);
    lv_obj_align(card_sp, LV_ALIGN_TOP_MID, 0, 306);
    lv_obj_set_style_bg_color(card_sp, lv_color_hex(0x001A08), 0);
    lv_obj_set_style_bg_opa(card_sp, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_sp, lv_color_hex(0x1DB954), 0);
    lv_obj_set_style_border_width(card_sp, 1, 0);
    lv_obj_set_style_border_opa(card_sp, LV_OPA_40, 0);
    lv_obj_set_style_radius(card_sp, 14, 0);
    lv_obj_clear_flag(card_sp, LV_OBJ_FLAG_SCROLLABLE);

    _lbl_spotify = lv_label_create(card_sp);
    lv_label_set_text(_lbl_spotify, "Spotify Connect\n(stub — brancher SpotifyArduino)");
    lv_obj_set_style_text_font(_lbl_spotify, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_spotify, lv_color_hex(0x1DB954), 0);
    lv_obj_set_style_text_align(_lbl_spotify, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(_lbl_spotify);

    // ── Section Amazon Music (désactivée) ────────────────────────────────
    lv_obj_t *card_am = lv_obj_create(_scr_sources);
    lv_obj_set_size(card_am, 460, 60);
    lv_obj_align(card_am, LV_ALIGN_TOP_MID, 0, 394);
    lv_obj_set_style_bg_color(card_am, lv_color_hex(C_DISABLED), 0);
    lv_obj_set_style_bg_opa(card_am, LV_OPA_50, 0);
    lv_obj_set_style_radius(card_am, 14, 0);
    lv_obj_clear_flag(card_am, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl_am = lv_label_create(card_am);
    lv_label_set_text(lbl_am, "Amazon Music — Non disponible (DRM propriétaire)");
    lv_obj_set_style_text_font(lbl_am, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_am, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_align(lbl_am, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl_am);
}

// Callback "sources" défini après build_sources_screen pour éviter la dépendance
static void cb_sources(lv_event_t *) {
    if (!_scr_sources) build_sources_screen();
    lv_scr_load_anim(_scr_sources, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

// ─── API publique ─────────────────────────────────────────────────────────
void musique_app_start() {
    orchestrator_set_app(APP_MUSIQUE);
    _app_active  = false;  // marqué actif après init
    _playing     = false;
    _airplay_conn = false;
    _nb_speakers  = 0;
    _nb_files     = 0;
    _current_track[0] = '\0';
    _current_artist[0] = '\0';

    // Initialiser SD card
    _sd_ok = sd_init();
    if (_sd_ok) list_audio_files();

    // Initialiser mDNS si pas déjà fait
    if (WiFi.isConnected() && !MDNS.begin("nestor-compagnon")) {
        Serial.println("[MUSIQUE] MDNS init echoue — decouverte AirPlay desactivee");
    }

    spotify_connect_init();

    build_main_screen();
    _app_active = true;

    lv_scr_load_anim(_scr_main, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    Serial.println("[APP/MUSIQUE] Ouverte");
}

void musique_app_tick() {
    if (!_app_active || !_scr_main) return;

    // Rafraîchir le statut AirPlay si la connexion tombe
    uint32_t now = millis();
    if (_airplay_conn && !_airplay_client.connected()) {
        _airplay_conn = false;
        ui_update_track();
    }

    // TODO: quand arduino-audio-tools est installé, appeler player.copy() ici
    // _audio_player.copy();
    _tick_audio = now;
}

void musique_app_stop() {
    _app_active = false;
    _playing    = false;
    _airplay_client.disconnect();
    _airplay_conn = false;

    if (_scr_sources) { lv_obj_del(_scr_sources); _scr_sources = nullptr; }
    if (_scr_main)    { lv_obj_del(_scr_main);    _scr_main    = nullptr; }

    _lbl_track = nullptr; _lbl_artist   = nullptr;
    _lbl_airplay = nullptr; _lbl_status = nullptr;
    _btn_play   = nullptr; _slider_vol  = nullptr;
    _dd_speakers = nullptr; _list_files = nullptr;
    _lbl_spotify = nullptr;

    orchestrator_set_app(APP_LAUNCHER);
    Serial.println("[APP/MUSIQUE] Fermee");
}

// ─── Traitement des commandes BLE reçues de la PWA ───────────────────────
void musique_ble_cmd(const char *cmd) {
    if (!cmd) return;
    Serial.printf("[MUSIQUE/BLE] cmd: %s\n", cmd);

    if (strcmp(cmd, "music:scan") == 0) {
        scan_airplay_speakers();
        notify_speakers();
        if (_app_active) ui_update_dropdown();

    } else if (strncmp(cmd, "music:connect:", 14) == 0) {
        // Format: music:connect:{ip}:{port}
        char buf[80]; strlcpy(buf, cmd + 14, sizeof(buf));
        char *colon = strrchr(buf, ':');
        int port = colon ? atoi(colon + 1) : 5000;
        if (colon) *colon = '\0';
        strlcpy(_connected_ip, buf, sizeof(_connected_ip));
        strlcpy(_airplay_name, buf, sizeof(_airplay_name));  // IP comme nom provisoire
        _airplay_client.disconnect();
        _airplay_conn = _airplay_client.connect(buf, port);
        if (_airplay_conn) _airplay_client.set_volume(_volume);
        if (_app_active) ui_update_track();
        notify_status();

    } else if (strcmp(cmd, "music:list_files") == 0) {
        if (_sd_ok) list_audio_files();
        notify_files();

    } else if (strncmp(cmd, "music:play:", 11) == 0) {
        strlcpy(_current_track, cmd + 11, sizeof(_current_track));
        _current_artist[0] = '\0';
        _playing = true;
        if (_app_active) ui_update_track();
        notify_status();
        // TODO: _audio_player.setPath(_current_track); _audio_player.play();

    } else if (strcmp(cmd, "music:pause") == 0) {
        _playing = !_playing;
        if (_app_active) ui_update_track();
        notify_status();

    } else if (strcmp(cmd, "music:stop") == 0) {
        _playing = false;
        _current_track[0] = '\0';
        if (_app_active) ui_update_track();
        notify_status();

    } else if (strcmp(cmd, "music:next") == 0) {
        // TODO: player.next()
        notify_status();

    } else if (strcmp(cmd, "music:prev") == 0) {
        // TODO: player.previous()
        notify_status();

    } else if (strncmp(cmd, "music:vol:", 10) == 0) {
        int v = atoi(cmd + 10);
        _volume = (uint8_t)constrain(v, 0, 100);
        if (_airplay_conn) _airplay_client.set_volume(_volume);
        if (_app_active && _slider_vol)
            lv_slider_set_value(_slider_vol, _volume, LV_ANIM_OFF);
        notify_status();
    }
}
