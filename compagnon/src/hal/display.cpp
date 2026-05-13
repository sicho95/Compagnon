#include "display.h"
#include "../config/pin_config.h"

Arduino_DataBus *gfx_bus = nullptr;
Arduino_CO5300  *gfx     = nullptr;

static lv_display_t *s_disp = nullptr;

#define BUF_LINES 40

static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

static void swap16_buf(uint16_t *buf, uint32_t n) {
  for (uint32_t i = 0; i < n; i++)
    buf[i] = (buf[i] >> 8) | (buf[i] << 8);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  swap16_buf((uint16_t *)px_map, w * h);
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  if (!a) return;
  if (a->x1 % 2)    a->x1--;
  if (a->y1 % 2)    a->y1--;
  if (!(a->x2 % 2)) a->x2++;
  if (!(a->y2 % 2)) a->y2++;
}

static uint32_t tick_cb() { return (uint32_t)millis(); }

// ─── Corner overlay : 4 carrés noirs dans lv_layer_sys() qui masquent les coins ───
// lv_screen_active() ne clipe pas ses enfants via clip_corner en LVGL 9.x.
// La technique overlay place 4 objets opaques noirs dans les 4 coins, par-dessus
// tout le contenu, pour simuler des coins arrondis sans modifier le rendu LVGL.
static void _create_corner_overlays() {
  const lv_coord_t R = LCD_CORNER_RADIUS;
  const lv_coord_t W = LCD_WIDTH;
  const lv_coord_t H = LCD_HEIGHT;

  // On utilise lv_layer_sys() (couche système, au-dessus de tout) pour que
  // les coins soient toujours visibles même sur les menus et popups.
  lv_obj_t *layer = lv_layer_sys();

  // Les 4 coins : chaque objet est un carré RxR avec un arc noir qui "recouvre"
  // le coin de l'écran. Astuce : on utilise un lv_obj avec bg noir et un
  // radius sur le coin opposé au coin de l'écran = cercle qui découpe proprement.
  //
  // Structure de chaque overlay :
  //   - taille : R x R px
  //   - bg : noir opaque
  //   - radius : R (arrondi sur le coin intérieur = crée la découpe visuelle)
  //   - no border, no shadow

  struct { lv_coord_t x; lv_coord_t y; } corners[4] = {
    { 0,     0     },  // haut-gauche
    { W - R, 0     },  // haut-droit
    { 0,     H - R },  // bas-gauche
    { W - R, H - R },  // bas-droit
  };

  for (int i = 0; i < 4; i++) {
    lv_obj_t *c = lv_obj_create(layer);
    lv_obj_set_size(c, R, R);
    lv_obj_set_pos(c, corners[i].x, corners[i].y);
    lv_obj_set_style_bg_color(c,     lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c,       LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_set_style_radius(c,       R,                LV_PART_MAIN);
    lv_obj_set_style_clip_corner(c,  true,             LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0,                LV_PART_MAIN);
    lv_obj_set_style_pad_all(c,      0,                LV_PART_MAIN);
    lv_obj_set_style_shadow_width(c, 0,                LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
  }
}

void hal_display_init() {
  Serial.println("[HAL/DISP] Init...");

  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, LOW);  delay(20);
  digitalWrite(LCD_RESET, HIGH); delay(120);

  gfx_bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK,
    LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
  );

  gfx = new Arduino_CO5300(gfx_bus, LCD_RESET, 0,
                            LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);

  if (!gfx->begin()) {
    Serial.println("[HAL/DISP] ECHEC begin() - BLOQUE");
    while (1) delay(500);
  }

  gfx->displayOn();
  gfx->setBrightness(200);
  gfx->fillScreen(0x0000);  // fond noir hardware dès l'init
  Serial.println("[HAL/DISP] GFX OK");

  lv_init();
  lv_tick_set_cb(tick_cb);

  size_t bytes = LCD_WIDTH * BUF_LINES * sizeof(lv_color_t);
  buf1 = (lv_color_t *)ps_malloc(bytes);
  buf2 = (lv_color_t *)ps_malloc(bytes);
  if (!buf1) { buf1 = (lv_color_t *)malloc(bytes); buf2 = nullptr; }

  s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_buffers(s_disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_disp, flush_cb);
  lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_270);

  // ── Écran racine : fond noir pur ─────────────────────────────────────────
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr,     lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr,       LV_OPA_COVER,     LV_PART_MAIN);
  lv_obj_set_style_pad_all(scr,      0,                LV_PART_MAIN);
  lv_obj_set_style_border_width(scr, 0,                LV_PART_MAIN);
  // Note : radius + clip_corner sur lv_screen_active() n'a aucun effet en
  // LVGL 9.x. Les coins arrondis sont gérés par _create_corner_overlays().

  // ── Coin-overlays : masquage LVGL des 4 coins (LCD_CORNER_RADIUS = LCD_CORNER_RADIUS px) ──
  _create_corner_overlays();

  Serial.printf("[HAL/DISP] LVGL OK - %dx%d corner_radius=%dpx (overlay)\n",
                LCD_WIDTH, LCD_HEIGHT, LCD_CORNER_RADIUS);
}

lv_display_t *hal_display_get() { return s_disp; }
