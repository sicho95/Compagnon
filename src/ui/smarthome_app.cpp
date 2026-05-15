#include "smarthome_app.h"
#include <stdio.h>

// ── Device IDs par défaut (override dans config.h) ────────────────────────────
#ifndef TUYA_SENSOR_ID
#define TUYA_SENSOR_ID "YOUR_TUYA_SENSOR_DEVICE_ID"
#endif
#ifndef TUYA_LIGHT_ID
#define TUYA_LIGHT_ID  "YOUR_TUYA_LIGHT_DEVICE_ID"
#endif
#ifndef TUYA_PLUG_ID
#define TUYA_PLUG_ID   "YOUR_TUYA_PLUG_DEVICE_ID"
#endif

// ── Palette AMOLED ────────────────────────────────────────────────────────────
#define COL_BG_SENSOR   0x1a3a4a
#define COL_BG_LIGHT    0x2a2a1a
#define COL_BG_PLUG     0x1a2a1a
#define COL_ACCENT      0x4fc3f7
#define COL_WARN        0xffcc44
#define COL_OK          0x66ee88
#define COL_MUTED       0x888888

// ─── Pack/unpack user_data (ptr + index dans 32 bits) ────────────────────────
// On stocke le pointeur directement + l'index via un petit tableau de contexte.
// Solution simple et portable sur ESP32 (ptr 32 bits).
struct _EvCtx { SmartHomeApp* app; int idx; };
static _EvCtx _ctxPool[SMARTHOME_MAX_DEVICES * 3]; // toggle + bright + ct par device
static int    _ctxCount = 0;

static _EvCtx* _allocCtx(SmartHomeApp* app, int idx) {
    if (_ctxCount >= (int)(sizeof(_ctxPool)/sizeof(_ctxPool[0]))) _ctxCount = 0;
    _ctxPool[_ctxCount] = {app, idx};
    return &_ctxPool[_ctxCount++];
}

// ─── Constructeur ─────────────────────────────────────────────────────────────
SmartHomeApp::SmartHomeApp(TuyaAPI* tuya, const DeviceEntry* devices, int count)
    : _tuya(tuya), _devices(devices),
      _count(count < SMARTHOME_MAX_DEVICES ? count : SMARTHOME_MAX_DEVICES),
      _root(nullptr), _status_label(nullptr)
{
    memset(_sw,       0, sizeof(_sw));
    memset(_bright_sl,0, sizeof(_bright_sl));
    memset(_ct_sl,    0, sizeof(_ct_sl));
    memset(_temp_lbl, 0, sizeof(_temp_lbl));
    memset(_hum_lbl,  0, sizeof(_hum_lbl));
}

// ─── create ───────────────────────────────────────────────────────────────────
void SmartHomeApp::create(lv_obj_t* parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(_root, 10, 0);
    lv_obj_set_style_gap(_root, 8, 0);
    lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    // Titre
    lv_obj_t* title = lv_label_create(_root);
    lv_label_set_text(title, LV_SYMBOL_HOME " Maison");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);

    // Cartes par device
    for (int i = 0; i < _count; i++) {
        switch (_devices[i].type) {
            case DEV_SENSOR: _buildSensorCard(_root, i); break;
            case DEV_LIGHT:  _buildLightCard(_root, i);  break;
            case DEV_PLUG:   _buildPlugCard(_root, i);   break;
        }
    }

    // Barre de status
    _status_label = lv_label_create(_root);
    lv_label_set_text(_status_label, "");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_10, 0);

    update();
}

void SmartHomeApp::destroy() {
    if (_root) { lv_obj_del(_root); _root = nullptr; }
}

// ─── _buildSensorCard ─────────────────────────────────────────────────────────
void SmartHomeApp::_buildSensorCard(lv_obj_t* parent, int i) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_BG_SENSOR), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = lv_label_create(card);
    lv_label_set_text(name, _devices[i].name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(COL_MUTED), 0);

    _temp_lbl[i] = lv_label_create(card);
    lv_label_set_text(_temp_lbl[i], LV_SYMBOL_CHARGE " --°C");
    lv_obj_set_style_text_color(_temp_lbl[i], lv_color_hex(COL_WARN), 0);

    _hum_lbl[i] = lv_label_create(card);
    lv_label_set_text(_hum_lbl[i], "Hum: --%");
    lv_obj_set_style_text_color(_hum_lbl[i], lv_color_hex(COL_ACCENT), 0);
}

// ─── _buildLightCard ──────────────────────────────────────────────────────────
void SmartHomeApp::_buildLightCard(lv_obj_t* parent, int i) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_gap(card, 6, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_BG_LIGHT), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Row: nom + switch
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, _devices[i].name);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    _sw[i] = lv_switch_create(row);
    lv_obj_add_event_cb(_sw[i], _onToggle, LV_EVENT_VALUE_CHANGED, _allocCtx(this, i));

    // Slider luminosité
    if (_devices[i].hasBrightness) {
        lv_obj_t* bl = lv_label_create(card);
        lv_label_set_text(bl, "Luminosité");
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(bl, lv_color_hex(COL_MUTED), 0);

        _bright_sl[i] = lv_slider_create(card);
        lv_obj_set_width(_bright_sl[i], lv_pct(90));
        lv_slider_set_range(_bright_sl[i], 10, 1000);
        lv_slider_set_value(_bright_sl[i], 500, LV_ANIM_OFF);
        lv_obj_add_event_cb(_bright_sl[i], _onBrightness, LV_EVENT_RELEASED, _allocCtx(this, i));
    }

    // Slider color temp
    if (_devices[i].hasColorTemp) {
        lv_obj_t* ctl = lv_label_create(card);
        lv_label_set_text(ctl, "Chaleur (K)");
        lv_obj_set_style_text_font(ctl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(ctl, lv_color_hex(COL_MUTED), 0);

        _ct_sl[i] = lv_slider_create(card);
        lv_obj_set_width(_ct_sl[i], lv_pct(90));
        lv_slider_set_range(_ct_sl[i], 2700, 6500);
        lv_slider_set_value(_ct_sl[i], 4000, LV_ANIM_OFF);
        lv_obj_add_event_cb(_ct_sl[i], _onColorTemp, LV_EVENT_RELEASED, _allocCtx(this, i));
    }
}

// ─── _buildPlugCard ───────────────────────────────────────────────────────────
void SmartHomeApp::_buildPlugCard(lv_obj_t* parent, int i) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_BG_PLUG), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, _devices[i].name);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    _sw[i] = lv_switch_create(card);
    lv_obj_add_event_cb(_sw[i], _onToggle, LV_EVENT_VALUE_CHANGED, _allocCtx(this, i));
}

// ─── update ───────────────────────────────────────────────────────────────────
void SmartHomeApp::update() {
    for (int i = 0; i < _count; i++) _refreshDevice(i);
}

void SmartHomeApp::_refreshDevice(int i) {
    if (!_tuya || !_tuya->isTokenValid()) return;
    const DeviceEntry& d = _devices[i];

    if (d.type == DEV_SENSOR) {
        float t, h;
        if (_tuya->getSensorData(d.id, t, h)) {
            _state[i].temp = t; _state[i].hum = h; _state[i].fetched = true;
            char buf[32];
            snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %.1f°C", t);
            if (_temp_lbl[i]) lv_label_set_text(_temp_lbl[i], buf);
            snprintf(buf, sizeof(buf), "Hum: %.0f%%", h);
            if (_hum_lbl[i]) lv_label_set_text(_hum_lbl[i], buf);
        }
    } else {
        TuyaDevice dev;
        if (_tuya->getDeviceStatus(d.id, dev)) {
            _state[i].on         = dev.is_on;
            _state[i].brightness = dev.brightness;
            _state[i].fetched    = true;
            if (_sw[i]) {
                if (dev.is_on) lv_obj_add_state(_sw[i],   LV_STATE_CHECKED);
                else           lv_obj_clear_state(_sw[i], LV_STATE_CHECKED);
            }
            if (_bright_sl[i]) lv_slider_set_value(_bright_sl[i], dev.brightness, LV_ANIM_ON);
        }
    }
}

void SmartHomeApp::_setStatus(const char* msg) {
    if (_status_label) lv_label_set_text(_status_label, msg);
}

// ─── Callbacks ────────────────────────────────────────────────────────────────
void SmartHomeApp::_onToggle(lv_event_t* e) {
    _EvCtx* ctx = (_EvCtx*)lv_event_get_user_data(e);
    SmartHomeApp* self = ctx->app;
    int i = ctx->idx;
    bool on = lv_obj_has_state(self->_sw[i], LV_STATE_CHECKED);
    self->_state[i].on = on;
    self->_setStatus(on ? "Activation..." : "Extinction...");
    if (self->_tuya && self->_tuya->isTokenValid()) {
        self->_tuya->switchDevice(self->_devices[i].id, on);
    }
    self->_setStatus("");
}

void SmartHomeApp::_onBrightness(lv_event_t* e) {
    _EvCtx* ctx = (_EvCtx*)lv_event_get_user_data(e);
    SmartHomeApp* self = ctx->app;
    int i = ctx->idx;
    int val = lv_slider_get_value(self->_bright_sl[i]);
    self->_state[i].brightness = val;
    if (self->_tuya && self->_tuya->isTokenValid()) {
        self->_tuya->setBrightness(self->_devices[i].id, val);
    }
}

void SmartHomeApp::_onColorTemp(lv_event_t* e) {
    _EvCtx* ctx = (_EvCtx*)lv_event_get_user_data(e);
    SmartHomeApp* self = ctx->app;
    int i = ctx->idx;
    int kelvin = lv_slider_get_value(self->_ct_sl[i]);
    self->_state[i].colorTemp = kelvin;
    if (self->_tuya && self->_tuya->isTokenValid()) {
        // Convertir Kelvin → 0-1000 (API Tuya)
        int tuyaVal = (int)((float)(kelvin - 2700) / (6500 - 2700) * 1000);
        self->_tuya->setColorTemp(self->_devices[i].id, tuyaVal);
    }
}
