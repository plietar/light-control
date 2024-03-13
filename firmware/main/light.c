#include "light.h"
#include "button.h"
#include "config.h"
#include <driver/gpio.h>
#include <led_indicator.h>
#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "light"

// From the LEDC documentation:
// "On ESP32-C3, when channel's binded timer selects its maximum duty
// resolution, the duty cycle value cannot be set to (2 ** duty_resolution).
// Otherwise, the internal duty counter in the hardware will overflow and be
// messed up."
// The max resolution is 14, so we pick one less than this to allow maximum
// brightness.
#define DUTY_RESOLUTION (13)
#define DUTY_MAX_BRIGHTNESS (1 << DUTY_RESOLUTION)

ESP_EVENT_DEFINE_BASE(LIGHT_EVENT);

static nvs_handle_t handle;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == BUTTON_EVENT) {
    int value = gpio_get_level(CONFIG_HW_GPIO_STATE_NUM);
    esp_event_post(LIGHT_EVENT, LIGHT_EVENT_INPUT_CHANGED, &value,
                   sizeof(value), 0);
    esp_event_post(LIGHT_EVENT, LIGHT_EVENT_STATE_CHANGED, &value,
                   sizeof(value), 0);
  }
}

void light_init() {
  ledc_timer_config_t timer_config = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = DUTY_RESOLUTION,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = 400,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer_config);

  ledc_channel_config_t control_config = {
      .gpio_num = CONFIG_HW_GPIO_CONTROL_NUM,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_2,
      .timer_sel = LEDC_TIMER_1,
      .intr_type = LEDC_INTR_DISABLE,
      .duty = 0,
      .hpoint = 0,
      .flags.output_invert = 0,
  };
  ledc_channel_config(&control_config);
  ledc_fade_func_install(0);

  gpio_config_t gpio_state_cfg = {
      .pin_bit_mask = (1 << CONFIG_HW_GPIO_STATE_NUM),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&gpio_state_cfg);

  button_init(1 << CONFIG_HW_GPIO_INPUT_NUM);
  ESP_ERROR_CHECK(esp_event_handler_register(BUTTON_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));

  ESP_ERROR_CHECK(nvs_open("light", NVS_READWRITE, &handle));
  uint8_t state;
  esp_err_t err = nvs_get_u8(handle, "state", &state);
  if (err == ESP_OK) {
    light_set_state(state, false);
  }
}

bool light_get_state() { return gpio_get_level(CONFIG_HW_GPIO_STATE_NUM); }

void light_set_state(bool level, bool fade) {
  uint32_t duty = level ^ gpio_get_level(CONFIG_HW_GPIO_INPUT_NUM)
                      ? DUTY_MAX_BRIGHTNESS
                      : 0;

  if (fade && config_get_bool_or("fade", true)) {
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, duty,
                            config_get_i32_or("fade_time", 200));
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, LEDC_FADE_NO_WAIT);
  } else {
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, duty, 0);
  }

  int value = level;
  esp_event_post(LIGHT_EVENT, LIGHT_EVENT_STATE_CHANGED, &value, sizeof(value),
                 0);

  nvs_set_u8(handle, "state", level ? 1 : 0);
  if (nvs_commit(handle) != ESP_OK) {
    ESP_LOGE(TAG, "cannot commit nvs");
  }
}
