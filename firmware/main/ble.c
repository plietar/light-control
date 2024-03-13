#include "ble.h"
#include "byteorder.h"
#include "esp_gap_ble_api.h"
#include "indicator.h"
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_log.h>
#include <string.h>

#define TAG "ble"

ESP_EVENT_DEFINE_BASE(BLE_EVENT);

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x50,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};

static uint32_t ble_manufacturer_id_filter = 0xffffffff;

static void on_scan_result(struct ble_scan_result_evt_param *result) {
  switch (result->search_evt) {
  case ESP_GAP_SEARCH_INQ_RES_EVT:
    for (size_t i = 0;
         i + 1 < result->adv_data_len && result->ble_adv[i] != 0;) {
      uint8_t length = result->ble_adv[i] - 1;
      uint8_t type = result->ble_adv[i + 1];
      uint8_t *payload = result->ble_adv + i + 2;

      switch (type) {
      case ESP_BLE_AD_TYPE_FLAG:
      case ESP_BLE_AD_TYPE_TX_PWR:
      case ESP_BLE_AD_TYPE_APPEARANCE:
      case ESP_BLE_AD_TYPE_16SRV_CMPL:
        break;

      case ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE:
        if (length > 2 && length < ESP_BLE_ADV_DATA_LEN_MAX) {
          uint16_t manufacturer_id = read_16le(payload);
          if (ble_manufacturer_id_filter == 0xffffffff ||
              ble_manufacturer_id_filter == manufacturer_id) {
            ESP_LOGI(TAG, "Manufacturer specific 0x%" PRIx16, manufacturer_id);

            struct ble_event_advertisment_manufacturer_data event;
            event.manufacturer_id = manufacturer_id;
            memcpy(event.payload, payload, length);
            event.length = length;
            esp_err_t err = esp_event_post(
                BLE_EVENT, BLE_EVENT_ADVERTISMENT_MANUFACTURER_DATA, &event,
                sizeof(event), 0);
            if (err != ESP_OK) {
              ESP_LOGE(TAG, "cannot queue BLE event, error = %s",
                       esp_err_to_name(err));
            }
          }
        }
        break;

      default:
        ESP_LOGI(TAG, "AD 0x%02x length=0x%02x", type, length);
        ESP_LOG_BUFFER_HEXDUMP(TAG, payload, length, ESP_LOG_INFO);
        break;
      }
      i += 2 + length;
    }
    break;
  default:
    break;
  }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
  // metrics_gap_event_handler(event, param);

  esp_err_t err;

  switch (event) {
  case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    ESP_LOGI(TAG, "Parameters set. Start scanning...");
    // The unit of the duration is second, 0 means scan permanently
    esp_ble_gap_start_scanning(0);
    break;

  case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
    // scan start complete event to indicate scan start successfully or failed
    if ((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
      ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
    } else {
      ESP_LOGI(TAG, "Scan started successfully");
    }
    break;

  case ESP_GAP_BLE_SCAN_RESULT_EVT:
    on_scan_result(&param->scan_rst);
    break;

  case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
      ESP_LOGE(TAG, "Scan stop failed: %s", esp_err_to_name(err));
    } else {
      ESP_LOGI(TAG, "Stop scan successfully");
    }
    break;

  default:
    break;
  }
}

void ble_init() {
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

  ESP_ERROR_CHECK(esp_bluedroid_init());
  ESP_ERROR_CHECK(esp_bluedroid_enable());

  ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
}

void ble_filter_set(uint16_t manufacturer_id) {
  ble_manufacturer_id_filter = manufacturer_id;
}

void ble_scan_start() {
  esp_ble_gap_set_scan_params(&ble_scan_params);
}
