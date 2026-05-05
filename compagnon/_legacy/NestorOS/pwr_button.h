#pragma once

void pwr_button_init();   // A appeler dans setup() apres Wire.begin()
void pwr_button_tick();   // A appeler dans loop() - gere les timings

// Exposee pour status_bar (niveau batterie via AXP2101)
int  pmu_get_battery_percent();
