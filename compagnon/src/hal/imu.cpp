#include "imu.h"
#include "../config/pin_config.h"
#include <Wire.h>
#include <SensorLib.h>
#include <SensorQMI8658.hpp>

static SensorQMI8658   imu;
static bool            imu_ok    = false;
static ScreenOrientation cur_or  = ORIENT_PORTRAIT;
static bool            changed   = false;

void hal_imu_init() {
    imu_ok = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (!imu_ok)
        imu_ok = imu.begin(Wire, QMI8658_H_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    if (imu_ok) {
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                SensorQMI8658::ACC_ODR_62_5Hz,
                                SensorQMI8658::LPF_MODE_2);
        imu.enableAccelerometer();
        imu.disableGyroscope();
        Serial.println("[HAL/IMU] QMI8658 OK");
    } else {
        Serial.println("[HAL/IMU] NON DETECTE");
    }
}

void hal_imu_tick() {
    static uint32_t last = 0;
    if (!imu_ok || millis() - last < 500) return;
    last = millis();
    if (!imu.getDataReady()) return;
    float ax, ay, az;
    if (!imu.getAccelerometer(ax, ay, az)) return;
    ScreenOrientation n = cur_or;
    const float T = 0.5f;
    if      (ay >  T) n = ORIENT_PORTRAIT;
    else if (ay < -T) n = ORIENT_PORTRAIT_INV;
    else if (ax >  T) n = ORIENT_LANDSCAPE_R;
    else if (ax < -T) n = ORIENT_LANDSCAPE_L;
    if (n != cur_or) { cur_or = n; changed = true; }
}

ScreenOrientation hal_imu_orientation() { return cur_or; }
bool hal_imu_changed() { bool c = changed; changed = false; return c; }
