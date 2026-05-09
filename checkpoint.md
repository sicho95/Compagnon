# Checkpoint — Fixes ESP32 + Complétion PWA

## Scope
1. **ESP32 hardware** : rotation 90°CCW (rotation=3→2), touch CST9220 fix, fond noir pur AMOLED, bordure 5px
2. **Boutons** : swap prev/next, appui long prev = retour launcher depuis une app
3. **Fluidity** : delay(1), fetches HTTP en tâche FreeRTOS (non-bloquant)
4. **Radar** : plus de fallback Paris (seulement si GPS dispo)
5. **Meteo** : NVS last-known position, fallback Paris uniquement en dernier recours
6. **PWA** : vue Météo (api.meteo-concept.com) + route dashboard + drawer

## Fichiers ESP32
- [ ] `compagnon.ino` → delay(1)
- [ ] `hal/display.cpp` → rotation=2, fillScreen 0x0000
- [ ] `hal/pmu.cpp` → MADCTL 0xC0
- [ ] `hal/touch.cpp` → Wire.end()+begin(), 800ms wait, rotation=2 mapping
- [ ] `ui/launcher.cpp` → btn swap, long=back, bg 0x000000, overlay 5px border
- [ ] `apps/meteo/meteo_app.cpp` → async FreeRTOS, NVS last-pos, no Paris fallback if BLE
- [ ] `apps/bourse/bourse_app.cpp` → async FreeRTOS
- [ ] `apps/radars/radar_app.cpp` → async FreeRTOS, no Paris fallback

## Fichiers PWA
- [ ] `src/ui/meteo-view.js` → nouvelle vue météo
- [ ] `src/ui/dashboard.js` → route meteo
- [ ] `src/app.js` → safeViews + drawer

## Notes techniques
- Rotation=2 (180°CW) = 90°CCW depuis rotation=3
- Touch mapping rotation=2 : swapXY=false, mirrorXY(true, true)
- MADCTL CO5300 rotation=2 : 0xC0
- Bordure : 4 rectangles noirs sur lv_layer_sys() (5px chaque bord)
- Async HTTP : xTaskCreatePinnedToCore + lv_async_call pour mise à jour UI
- BTN_LEFT=next, BTN_RIGHT=prev (swappés), BTN_RIGHT long=open/back
