#pragma once
#include <Arduino.h>
void brain_init();
void brain_tick();
const char* brain_process(const char* input);
