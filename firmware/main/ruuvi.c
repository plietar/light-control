#include "ruuvi.h"
#include "ble.h"
#include "byteorder.h"
#include "ruuvi_names.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <cJSON.h>
#include <string.h>

#define TAG "ruuvi"

#define MACSTR_SIZE (2 * 6 + 5 + 1)
#define MACSTR_UPPER "%02X:%02X:%02X:%02X:%02X:%02X"
static inline void mac2str(char *out, const uint8_t *in) {
  snprintf(out, MACSTR_SIZE, MACSTR_UPPER, MAC2STR(in));
}

bool ruuvi_decode_frame(struct ruuvi_frame *frame, const uint8_t *data,
                        size_t length) {
  if (length != 24 || data[0] != RUUVI_DATA_FORMAT_RAWV2) {
    return false;
  }

  frame->temperature = read_16be(data + 1);
  frame->humidity = read_16be(data + 3);
  frame->pressure = read_16be(data + 5);
  frame->acceleration_x = read_16be(data + 7);
  frame->acceleration_y = read_16be(data + 9);
  frame->acceleration_z = read_16be(data + 11);
  uint16_t power_info = read_16be(data + 13);
  frame->battery_voltage = power_info >> 5;
  frame->tx_power = (power_info & 0x1f);
  frame->movement_counter = data[15];
  frame->sequence_number = read_16be(data + 16);
  memcpy(frame->mac, data + 18, 6);

  return true;
}

static void publish_ruuvi_frame(esp_mqtt_client_handle_t mqtt_client,
                                const struct ruuvi_frame *frame) {
  char mac[MACSTR_SIZE];
  mac2str(mac, frame->mac);

  const char *name = ruuvi_find_name(frame->mac);

  cJSON *root = cJSON_CreateObject();
  if (name != NULL) {
    cJSON_AddStringToObject(root, "name", name);
  }
  cJSON_AddNumberToObject(root, "temperature", frame->temperature * 0.005);
  cJSON_AddNumberToObject(root, "humidity", frame->humidity * 0.0025);
  cJSON_AddNumberToObject(root, "pressure", frame->pressure + 50000);
  cJSON_AddNumberToObject(root, "sequence_number", frame->sequence_number);
  cJSON_AddNumberToObject(root, "acceleration_x", frame->acceleration_x);
  cJSON_AddNumberToObject(root, "acceleration_y", frame->acceleration_y);
  cJSON_AddNumberToObject(root, "acceleration_z", frame->acceleration_z);
  cJSON_AddNumberToObject(root, "battery_voltage",
                          frame->battery_voltage / 1000.);
  cJSON_AddStringToObject(root, "mac", mac);

  char *topic;
  asprintf(&topic, "%s/%s", CONFIG_RUUVI_MQTT_TOPIC_PREFIX, mac);
  char *payload = cJSON_PrintUnformatted(root);

  esp_mqtt_client_enqueue(mqtt_client, topic, payload, 0,
                          /* QOS */ 2, /* retain */ 0, true);

  free(topic);
  free(payload);
  cJSON_Delete(root);
}

static void on_manufacturer_data(esp_mqtt_client_handle_t client,
                                 const uint8_t *payload, size_t length) {
  struct ruuvi_frame frame;
  if (ruuvi_decode_frame(&frame, payload + 2, length - 2)) {
    ESP_LOGI(TAG, "Ruuvi Tag: " MACSTR_UPPER " (%s)", MAC2STR(frame.mac),
             ruuvi_find_name(frame.mac) ?: "unknown");
    // metric_ruuvi_packet_decoded_count += 1;
    publish_ruuvi_frame(client, &frame);
  } else {
    // metric_ruuvi_packet_malformed_count += 1;
    ESP_LOGI(TAG, "bad ruuvi frame");
    ESP_LOG_BUFFER_HEX(TAG, payload, length);
  }
}

static void ruuvi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
  esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)arg;
  if (event_base == BLE_EVENT &&
      event_id == BLE_EVENT_ADVERTISMENT_MANUFACTURER_DATA) {
    ble_event_advertisment_manufacturer_data *event = event_data;
    if (event->manufacturer_id == RUUVI_MANIFACTURER_ID) {
      on_manufacturer_data(client, event->payload, event->length);
    }
  }
}

void ruuvi_init(esp_mqtt_client_handle_t client) {
  ESP_ERROR_CHECK(esp_event_handler_register(BLE_EVENT, ESP_EVENT_ANY_ID,
                                             ruuvi_event_handler, client));
}
