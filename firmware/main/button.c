// https://github.com/craftmetrics/esp32-button

#include "button.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "BUTTON"

ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

typedef struct {
  uint8_t pin;
  bool inverted;
  uint16_t history;
  uint32_t down_time;
} debounce_t;

int pin_count = -1;
debounce_t *debounce;

static void update_button(debounce_t *d) {
  d->history = (d->history << 1) | gpio_get_level(d->pin);
}

#define MASK 0b1111000000111111
static bool button_rose(debounce_t *d) {
  if ((d->history & MASK) == 0b0000000000111111) {
    d->history = 0xffff;
    return 1;
  }
  return 0;
}
static bool button_fell(debounce_t *d) {
  if ((d->history & MASK) == 0b1111000000000000) {
    d->history = 0x0000;
    return 1;
  }
  return 0;
}
static bool button_down(debounce_t *d) {
  if (d->inverted)
    return button_fell(d);
  return button_rose(d);
}
static bool button_up(debounce_t *d) {
  if (d->inverted)
    return button_rose(d);
  return button_fell(d);
}

static uint32_t millis() { return esp_timer_get_time() / 1000; }

static void button_task(void *pvParameter) {
  for (;;) {
    for (int idx = 0; idx < pin_count; idx++) {
      update_button(&debounce[idx]);
      if (button_up(&debounce[idx])) {
        debounce[idx].down_time = 0;
        ESP_LOGI(TAG, "%d UP", debounce[idx].pin);
        esp_event_post(BUTTON_EVENT, BUTTON_UP, &debounce[idx].pin, 1,
                       portMAX_DELAY);
      } else if (button_down(&debounce[idx]) && debounce[idx].down_time == 0) {
        debounce[idx].down_time = millis();
        ESP_LOGI(TAG, "%d DOWN", debounce[idx].pin);
        esp_event_post(BUTTON_EVENT, BUTTON_DOWN, &debounce[idx].pin, 1,
                       portMAX_DELAY);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void button_init(unsigned long long pin_select) {
  return pulled_button_init(pin_select, GPIO_FLOATING);
}

void pulled_button_init(unsigned long long pin_select,
                        gpio_pull_mode_t pull_mode) {
  if (pin_count != -1) {
    ESP_LOGI(TAG, "Already initialized");
  }

  // Configure the pins
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_POSEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en =
      (pull_mode == GPIO_PULLUP_ONLY || pull_mode == GPIO_PULLUP_PULLDOWN);
  io_conf.pull_down_en =
      (pull_mode == GPIO_PULLDOWN_ONLY || pull_mode == GPIO_PULLUP_PULLDOWN);
  ;
  io_conf.pin_bit_mask = pin_select;
  gpio_config(&io_conf);

  // Scan the pin map to determine number of pins
  pin_count = 0;
  for (int pin = 0; pin <= 39; pin++) {
    if ((1ULL << pin) & pin_select) {
      pin_count++;
    }
  }

  // Initialize global state and queue
  debounce = calloc(pin_count, sizeof(debounce_t));

  // Scan the pin map to determine each pin number, populate the state
  uint32_t idx = 0;
  for (int pin = 0; pin <= 39; pin++) {
    if ((1ULL << pin) & pin_select) {
      ESP_LOGI(TAG, "Registering button input: %d", pin);
      debounce[idx].pin = pin;
      debounce[idx].down_time = 0;
      debounce[idx].inverted = true;
      if (debounce[idx].inverted)
        debounce[idx].history = 0xffff;
      idx++;
    }
  }

  // Spawn a task to monitor the pins
  xTaskCreate(&button_task, "button_task", CONFIG_ESP32_BUTTON_TASK_STACK_SIZE,
              NULL, 10, NULL);
}
