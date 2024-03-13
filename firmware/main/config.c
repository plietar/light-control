#include "light.h"
#include "local_control.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <nvs_flash.h>

#define TAG "config"

static char *config_topic;
static char *config_set_topic;

static nvs_handle_t handle;

static void save_config(const char *payload, size_t payload_len) {
  cJSON *root = cJSON_ParseWithLength(payload, payload_len);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return;
  }

  nvs_erase_all(handle);

  cJSON *element;
  cJSON_ArrayForEach(element, root) {
    switch (element->type) {
    case cJSON_True:
      nvs_set_u8(handle, element->string, 1);
      break;
    case cJSON_False:
      nvs_set_u8(handle, element->string, 0);
      break;
    case cJSON_Number:
      nvs_set_i32(handle, element->string, element->valuedouble);
      break;
    default:
      ESP_LOGW(TAG, "unknown config entry type: %d", element->type);
    }
  }

  if (nvs_commit(handle) != ESP_OK) {
    ESP_LOGE(TAG, "cannot commit nvs");
  }
  cJSON_Delete(root);
}

static void publish_config(esp_mqtt_client_handle_t client) {
  cJSON *root = cJSON_CreateObject();

  nvs_iterator_t it = NULL;
  // TODO: use nvs_entry_find_in_handle in next IDF version
  for (esp_err_t res = nvs_entry_find("nvs", "config", NVS_TYPE_ANY, &it);
       res == ESP_OK; res = nvs_entry_next(&it)) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    switch (info.type) {
    case NVS_TYPE_U8: {
      uint8_t value;
      esp_err_t err = nvs_get_u8(handle, info.key, &value);
      if (err == ESP_OK) {
        cJSON_AddBoolToObject(root, info.key, value);
      }
      break;
    }
    case NVS_TYPE_I32: {
      int32_t value;
      esp_err_t err = nvs_get_i32(handle, info.key, &value);
      if (err == ESP_OK) {
        cJSON_AddNumberToObject(root, info.key, value);
      }
      break;
    }
    default:
      break;
    }
  }
  nvs_release_iterator(it);

  char *payload = cJSON_PrintUnformatted(root);
  esp_mqtt_client_enqueue(client, config_topic, payload, 0,
                          /* QOS */ 2, /* retain */ 1, true);

  cJSON_Delete(root);
  free(payload);
}

esp_err_t config_get_bool(const char *key, bool *out) {
  uint8_t v;
  esp_err_t err = nvs_get_u8(handle, key, &v);
  if (err == ESP_OK) {
    *out = v;
  }
  return err;
}

bool config_get_bool_or(const char *key, bool default_value) {
  bool value;
  esp_err_t err = config_get_bool(key, &value);
  if (err == ESP_OK) {
    return value;
  } else {
    return default_value;
  }
}

esp_err_t config_get_i32(const char *key, int32_t *out) {
  return nvs_get_i32(handle, key, out);
}

int32_t config_get_i32_or(const char *key, int32_t default_value) {
  int32_t value;
  esp_err_t err = config_get_i32(key, &value);
  if (err == ESP_OK) {
    return value;
  } else {
    return default_value;
  }
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  if (event_id == MQTT_EVENT_CONNECTED) {
    esp_mqtt_client_subscribe(event->client, config_set_topic, 2);
    publish_config(event->client);
  } else if (event_id == MQTT_EVENT_DATA) {
    if (event->topic_len == strlen(config_set_topic) &&
        strncmp(event->topic, config_set_topic, event->topic_len) == 0) {
      save_config(event->data, event->data_len);
      publish_config(event->client);
    }
  }
}

void config_init(esp_mqtt_client_handle_t client, const char *prefix) {
  asprintf(&config_topic, "%s/config", prefix);
  asprintf(&config_set_topic, "%s/config/set", prefix);

  ESP_ERROR_CHECK(nvs_open("config", NVS_READWRITE, &handle));

  ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL));
}
