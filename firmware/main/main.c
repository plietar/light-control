#include "ble.h"
#include "light.h"
#include "local_control.h"
#include "config.h"
#include "indicator.h"
#include "ruuvi.h"
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <mqtt_ota.h>
#include <nvs_flash.h>
#include <stdio.h>

#define TAG "main"

struct mqtt_topics {
  char *base;
  char *status;
  char *state;
  char *command;
  char *metrics;
  char *ota;
};

static struct mqtt_topics topics;
esp_mqtt_client_handle_t mqtt_handle;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    esp_mqtt_client_reconnect(mqtt_handle);
  } else if (event_base == LIGHT_EVENT &&
             event_id == LIGHT_EVENT_STATE_CHANGED) {
    int value = *(const int *)event_data;
    ESP_LOGI(TAG, "got notification %d", value);
    esp_mqtt_client_publish(mqtt_handle, topics.state, value ? "ON" : "OFF", 0,
                            2, 1);
  } else if (event_base == MQTT_OTA_EVENT &&
             event_id == MQTT_OTA_EVENT_STARTED) {
    ESP_LOGI(TAG, "OTA started...");
  } else if (event_base == MQTT_OTA_EVENT &&
             event_id == MQTT_OTA_EVENT_FINISHED) {
    ESP_LOGI(TAG, "OTA done. Restarting now");
    esp_restart();
  }
}

static void on_mqtt_message(const char *topic, size_t topic_len,
                            const char *data, size_t data_len) {
  if (topic_len == strlen(topics.command) && strncmp(topic, topics.command, topic_len) == 0) {
    if (data_len == 2 && strncmp(data, "ON", data_len) == 0) {
      light_set_state(true, true);
    } else if (data_len == 3 && strncmp(data, "OFF", data_len) == 0) {
      light_set_state(false, true);
    } else if (data_len == 7 && strncmp(data, "restart", data_len) == 0) {
      esp_restart();
    }
  }
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;

  if (event_id == MQTT_EVENT_CONNECTED) {
    esp_mqtt_client_subscribe(mqtt_handle, topics.command, 2);
    esp_mqtt_client_enqueue(mqtt_handle, topics.status, "Online", 0, 2, 1,
                            true);

    int state = light_get_state();
    esp_mqtt_client_enqueue(mqtt_handle, topics.state,
                            state ? "ON" : "OFF", 0, 2, 1, true);
  } else if (event_id == MQTT_EVENT_DATA) {
    on_mqtt_message(event->topic, event->topic_len, event->data,
                    event->data_len);
  }
}

static void wifi_init() {
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = CONFIG_WIFI_SSID,
              .password = CONFIG_WIFI_PASSWORD,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");
}

extern const uint8_t isrgrootx1_pem_start[] asm("_binary_isrgrootx1_pem_start");
void mqtt_init() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  asprintf(&topics.base, "%s/" MACSTR, CONFIG_MQTT_TOPIC_PREFIX, MAC2STR(mac));
  asprintf(&topics.status, "%s/status", topics.base);
  asprintf(&topics.state, "%s/state", topics.base);
  asprintf(&topics.command, "%s/command", topics.base);
  asprintf(&topics.metrics, "%s/metrics", topics.base);
  asprintf(&topics.ota, "%s/ota", topics.base);

  const esp_mqtt_client_config_t mqtt_cfg = {
      .broker =
          {
              .address =
                  {
                      .hostname = CONFIG_MQTT_BROKER,
                      .port = 8883,
                      .transport = MQTT_TRANSPORT_OVER_SSL,
                  },
              .verification.certificate = (const char *)isrgrootx1_pem_start,
          },

      .credentials =
          {
              .username = CONFIG_MQTT_USERNAME,
              .authentication.password = CONFIG_MQTT_PASSWORD,
          },

      .session.last_will =
          {
              .topic = topics.status,
              .msg = "Offline",
              .qos = 2,
              .retain = 1,
          },
  };

  mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, NULL);
  esp_mqtt_client_start(mqtt_handle);
}

static void publish_metrics(void *arg) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "millis", esp_timer_get_time() / 1000);
  cJSON_AddNumberToObject(root, "current_free_bytes",
                          heap_caps_get_free_size(MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(root, "minimum_free_bytes",
                          heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));

  int rssi;
  if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK) {
    cJSON_AddNumberToObject(root, "wifi_rssi", rssi);
  }

  extern const char project_build_date[];

  const esp_app_desc_t* app = esp_app_get_description();
  cJSON* firmware = cJSON_AddObjectToObject(root, "firmware");
  cJSON_AddStringToObject(firmware, "name", app->project_name);
  cJSON_AddStringToObject(firmware, "version", app->version);
  cJSON_AddStringToObject(firmware, "date", project_build_date);

  char *payload = cJSON_PrintUnformatted(root);
  esp_mqtt_client_enqueue(mqtt_handle, topics.metrics, payload, 0,
                          /* QOS */ 0, /* retain */ 0, true);

  free(payload);
  cJSON_Delete(root);
}

void metrics_init() {
  esp_timer_create_args_t args = {
      .callback = publish_metrics,
      .dispatch_method = ESP_TIMER_TASK,
      .skip_unhandled_events = true,
  };
  esp_timer_handle_t timer;
  ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
  esp_timer_start_periodic(timer, 60 * 1000 * 1000);
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  mqtt_init();
  indicator_init(mqtt_handle);
  config_init(mqtt_handle, topics.base);
  metrics_init();
  light_init();
  wifi_init();
  mqtt_ota_init(mqtt_handle, topics.ota);
  local_control_init(mqtt_handle, topics.base);

  ESP_ERROR_CHECK(esp_event_handler_register(MQTT_OTA_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(LIGHT_EVENT, LIGHT_EVENT_STATE_CHANGED,
                                             &event_handler, NULL));

#if RUUVI_ENABLE
  ble_init();
  ruuvi_init(mqtt_handle);
  ble_filter_set(RUUVI_MANIFACTURER_ID);
  ble_scan_start();
#endif
}
