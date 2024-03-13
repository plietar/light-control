#include "indicator.h"

#include "ble.h"
#include "light.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <led_indicator.h>
#include <mqtt_ota.h>

#define TAG "indicator"

led_indicator_handle_t primary_handle;
led_indicator_handle_t secondary_handle;

const blink_step_t disconnected_led_sequence[] = {
    {LED_BLINK_HOLD, LED_STATE_OFF, 0},
    {LED_BLINK_BREATHE, LED_STATE_ON, 500},
    {LED_BLINK_BREATHE, LED_STATE_OFF, 500},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t wifi_led_sequence[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 20},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1860},
    {LED_BLINK_LOOP, 0, 0},
};
const blink_step_t mqtt_led_sequence[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 20},
    {LED_BLINK_HOLD, LED_STATE_OFF, 200},
    {LED_BLINK_HOLD, LED_STATE_ON, 20},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1640},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t ota_led_sequence[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 20},
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t blink_led_sequence[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 20},
    {LED_BLINK_HOLD, LED_STATE_OFF, 0},
    {LED_BLINK_STOP, 0, 0},
};

enum {
  PRIMARY_INDICATOR_OTA,
  PRIMARY_INDICATOR_MQTT,
  PRIMARY_INDICATOR_WIFI,
  PRIMARY_INDICATOR_DISCONNECTED,
  PRIMARY_INDICATOR_MAX,
};

enum {
  SECONDARY_INDICATOR_BLINK,
  SECONDARY_INDICATOR_MAX,
};

blink_step_t const *primary_sequence_list[] = {
    [PRIMARY_INDICATOR_OTA] = ota_led_sequence,
    [PRIMARY_INDICATOR_MQTT] = mqtt_led_sequence,
    [PRIMARY_INDICATOR_WIFI] = wifi_led_sequence,
    [PRIMARY_INDICATOR_DISCONNECTED] = disconnected_led_sequence,
    [PRIMARY_INDICATOR_MAX] = NULL,
};

blink_step_t const *secondary_sequence_list[] = {
    [SECONDARY_INDICATOR_BLINK] = blink_led_sequence,
    [SECONDARY_INDICATOR_MAX] = NULL,
};

static void indicator_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    led_indicator_start(primary_handle, PRIMARY_INDICATOR_WIFI);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    led_indicator_stop(primary_handle, PRIMARY_INDICATOR_WIFI);
  } else if (event_base == MQTT_OTA_EVENT &&
             event_id == MQTT_OTA_EVENT_STARTED) {
    led_indicator_start(primary_handle, PRIMARY_INDICATOR_OTA);
  } else if (event_base == MQTT_OTA_EVENT &&
             (event_id == MQTT_OTA_EVENT_FAILED ||
              event_id == MQTT_OTA_EVENT_FINISHED)) {
    led_indicator_stop(primary_handle, PRIMARY_INDICATOR_OTA);
#if CONFIG_RUUVI_ENABLE
  } else if (event_base == BLE_EVENT &&
             event_id == BLE_EVENT_ADVERTISMENT_MANUFACTURER_DATA) {
    led_indicator_start(secondary_handle, SECONDARY_INDICATOR_BLINK);
#endif
  }
}

static void indicator_mqtt_event_handler(void *arg, esp_event_base_t event_base,
                                         int32_t event_id, void *event_data) {
  if (event_id == MQTT_EVENT_CONNECTED) {
    led_indicator_start(primary_handle, PRIMARY_INDICATOR_MQTT);
  } else if (event_id == MQTT_EVENT_DISCONNECTED) {
    led_indicator_stop(primary_handle, PRIMARY_INDICATOR_MQTT);
  }
}

void indicator_init(esp_mqtt_client_handle_t mqtt_handle) {
  led_indicator_ledc_config_t primary_ledc_config = {
      .is_active_level_high = true,
      .timer_inited = false,
      .timer_num = LEDC_TIMER_0,
      .gpio_num = CONFIG_HW_GPIO_PRIMARY_LED_NUM,
      .channel = LEDC_CHANNEL_0,
  };
  led_indicator_config_t primary_config = {
      .mode = LED_LEDC_MODE,
      .led_indicator_ledc_config = &primary_ledc_config,
      .blink_lists = primary_sequence_list,
      .blink_list_num = PRIMARY_INDICATOR_MAX,
  };
  led_indicator_ledc_config_t secondary_ledc_config = {
      .is_active_level_high = true,
      .timer_inited = true,
      .timer_num = LEDC_TIMER_0,
      .gpio_num = CONFIG_HW_GPIO_SECONDARY_LED_NUM,
      .channel = LEDC_CHANNEL_1,
  };
  led_indicator_config_t secondary_config = {
      .mode = LED_LEDC_MODE,
      .led_indicator_ledc_config = &secondary_ledc_config,
      .blink_lists = secondary_sequence_list,
      .blink_list_num = SECONDARY_INDICATOR_MAX,
  };

  primary_handle = led_indicator_create(&primary_config);
  secondary_handle = led_indicator_create(&secondary_config);

  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             indicator_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, indicator_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(MQTT_OTA_EVENT, ESP_EVENT_ANY_ID,
                                             indicator_event_handler, NULL));
#if CONFIG_RUUVI_ENABLE
  ESP_ERROR_CHECK(esp_event_handler_register(
      BLE_EVENT, BLE_EVENT_ADVERTISMENT_MANUFACTURER_DATA,
      indicator_event_handler, NULL));
#endif

  ESP_ERROR_CHECK(esp_mqtt_client_register_event(
      mqtt_handle, ESP_EVENT_ANY_ID, indicator_mqtt_event_handler, NULL));

  led_indicator_start(primary_handle, PRIMARY_INDICATOR_DISCONNECTED);
}
