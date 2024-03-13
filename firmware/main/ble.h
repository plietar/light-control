#pragma once
#include "sdkconfig.h"

#if CONFIG_RUUVI_ENABLE

#include <esp_gap_ble_api.h>
#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(BLE_EVENT);

typedef struct ble_event_advertisment_manufacturer_data {
  uint16_t manufacturer_id;
  uint8_t payload[ESP_BLE_ADV_DATA_LEN_MAX];
  size_t length;
} ble_event_advertisment_manufacturer_data;

enum {
  BLE_EVENT_ADVERTISMENT_MANUFACTURER_DATA = 0,
};

void ble_init();
void ble_filter_set(uint16_t manufacturer_id);
void ble_scan_start();

#endif // CONFIG_RUUVI_ENABLE
