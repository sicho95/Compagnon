#pragma once
#include <stdbool.h>

// Mémoriser la position GPS reçue via BLE (degrés décimaux, float32)
void  gps_update(float lat, float lon);
float gps_get_lat();
float gps_get_lon();
bool  gps_has_fix();
