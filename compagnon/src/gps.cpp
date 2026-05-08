#include "gps.h"

static float _lat = 0.0f;
static float _lon = 0.0f;
static bool  _fix = false;

void  gps_update(float lat, float lon) { _lat = lat; _lon = lon; _fix = true; }
float gps_get_lat()                    { return _lat; }
float gps_get_lon()                    { return _lon; }
bool  gps_has_fix()                    { return _fix; }
