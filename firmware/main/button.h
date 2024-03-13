// https://github.com/craftmetrics/esp32-button
#pragma once

#include "driver/gpio.h"
#include <esp_event.h>

#ifndef CONFIG_ESP32_BUTTON_TASK_STACK_SIZE
#define CONFIG_ESP32_BUTTON_TASK_STACK_SIZE 3072
#endif

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);

enum {
  BUTTON_DOWN,
  BUTTON_UP,
};

void button_init(unsigned long long pin_select);
void pulled_button_init(unsigned long long pin_select,
                        gpio_pull_mode_t pull_mode);
