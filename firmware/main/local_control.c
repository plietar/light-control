#include "local_control.h"
#include "light.h"
#include "config.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <nvs_flash.h>

#define TAG "local_control"

// TODO: check for self-messages
// TODO: check if setting peers multiple times removes the old ones

static nvs_handle_t my_handle;
static char *peers_topic;
static char *peers_set_topic;

enum {
  LOCAL_CONTROL_LIGHT_STATE = 0,
};

static void recv_callback(const esp_now_recv_info_t *info, const uint8_t *data,
                          int data_len) {
  ESP_LOGI(TAG, "got packet from " MACSTR ", %d bytes", MAC2STR(info->src_addr),
           data_len);
  if (data_len == 0) {
    ESP_LOGW(TAG, "empty esp-now packet");
    return;
  }

  switch (data[0]) {
  case LOCAL_CONTROL_LIGHT_STATE:
    if (data_len == 3) {
      uint8_t group = data[1];
      uint8_t value = data[2];
      ESP_LOGI(TAG, "set-state %d", value);
      if (group == config_get_i32_or("group", 0)) {
        light_set_state(value, /* fade */ false);
      }
    }
    break;
  }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == LIGHT_EVENT && event_id == LIGHT_EVENT_INPUT_CHANGED) {
    uint8_t group = config_get_i32_or("group", 0);
    if (group > 0) {
      uint8_t packet[] = {LOCAL_CONTROL_LIGHT_STATE, group, *(const int *)event_data};
      ESP_LOGI(TAG, "sending %d", *(const int *)event_data);
      esp_now_send(NULL, packet, sizeof(packet));
    }
  }
}

static void set_peers(uint8_t *data, size_t size) {
  ESP_LOGI(TAG, "configuring %d peers", size / ESP_NOW_ETH_ALEN);
  for (size_t i = 0; i < size; i += ESP_NOW_ETH_ALEN) {
    ESP_LOGI(TAG, "Configuring peer " MACSTR, MAC2STR(data + i));
    struct esp_now_peer_info peer = {
        .channel = 0,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(&peer.peer_addr, data + i, ESP_NOW_ETH_ALEN);
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Cannot configure peer " MACSTR ": %s", MAC2STR(data + i),
               esp_err_to_name(err));
    }
  }
}

static void save_peers(uint8_t *data, size_t size) {
  if (nvs_set_blob(my_handle, "peers", data, size) != ESP_OK) {
    ESP_LOGE(TAG, "cannot save peers");
  }
  if (nvs_commit(my_handle) != ESP_OK) {
    ESP_LOGE(TAG, "cannot commit nvs");
  }
}

static void load_peers() {
  size_t size;
  if (nvs_get_blob(my_handle, "peers", NULL, &size) != ESP_OK) {
    ESP_LOGW(TAG, "No peers configured!");
    return;
  }

  uint8_t *data = malloc(size);
  if (nvs_get_blob(my_handle, "peers", data, &size) != ESP_OK) {
    ESP_LOGE(TAG, "bad bad bad");
    return;
  }

  set_peers(data, size);
}

static void configure_peers(const char *payload, size_t payload_len) {
  cJSON *root = cJSON_ParseWithLength(payload, payload_len);
  if (!cJSON_IsArray(root)) {
    ESP_LOGE(TAG, "peers list is not array");
    cJSON_Delete(root);
    return;
  }

  size_t n = cJSON_GetArraySize(root);
  uint8_t *data = malloc(n * ESP_NOW_ETH_ALEN);
  uint8_t *p = data;
  cJSON *elem;
  cJSON_ArrayForEach(elem, root) {
    if (cJSON_IsString(elem)) {
      unsigned int mac[ESP_NOW_ETH_ALEN];
      int x = sscanf(cJSON_GetStringValue(elem), MACSTR, &mac[0], &mac[1],
                     &mac[2], &mac[3], &mac[4], &mac[5]);
      if (x == ESP_NOW_ETH_ALEN) {
        for (size_t i = 0; i < ESP_NOW_ETH_ALEN; i++, p++) {
          *p = mac[i];
        }
      }
    }
  }

  save_peers(data, p - data);
  set_peers(data, p - data);

  cJSON_Delete(root);
}

static void publish_peers(esp_mqtt_client_handle_t client) {
  size_t size;
  if (nvs_get_blob(my_handle, "peers", NULL, &size) != ESP_OK) {
    // TODO: publish null
    ESP_LOGW(TAG, "No peers configured!");
    return;
  }

  uint8_t *data = malloc(size);
  if (nvs_get_blob(my_handle, "peers", data, &size) != ESP_OK) {
    ESP_LOGE(TAG, "bad bad bad");
    return;
  }

  cJSON *root = cJSON_CreateArray();
  for (size_t i = 0; i < size; i += ESP_NOW_ETH_ALEN) {
    char *s;
    asprintf(&s, MACSTR, MAC2STR(data + i));
    cJSON_AddItemToArray(root, cJSON_CreateString(s));
    free(s);
  }

  char *payload = cJSON_PrintUnformatted(root);
  esp_mqtt_client_enqueue(client, peers_topic, payload, 0,
                          /* QOS */ 2, /* retain */ 1, true);

  cJSON_Delete(root);
  free(payload);
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  if (event_id == MQTT_EVENT_CONNECTED) {
    esp_mqtt_client_subscribe(event->client, peers_set_topic, 2);
    publish_peers(event->client);
  } else if (event_id == MQTT_EVENT_DATA) {
    if (event->topic_len == strlen(peers_set_topic) &&
        strncmp(event->topic, peers_set_topic, event->topic_len) == 0) {
      configure_peers(event->data, event->data_len);
      publish_peers(event->client);
    }
  }
}

void local_control_init(esp_mqtt_client_handle_t client, const char *prefix) {
  asprintf(&peers_topic, "%s/peers", prefix);
  asprintf(&peers_set_topic, "%s/peers/set", prefix);

  ESP_ERROR_CHECK(nvs_open("local_control", NVS_READWRITE, &my_handle));

  esp_now_init();
  esp_now_register_recv_cb(recv_callback);

  load_peers();

  ESP_ERROR_CHECK(esp_event_handler_register(
      LIGHT_EVENT, LIGHT_EVENT_INPUT_CHANGED, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL));
}
