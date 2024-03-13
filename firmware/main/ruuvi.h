#pragma once
#include "sdkconfig.h"

#if CONFIG_RUUVI_ENABLE

#include <mqtt_client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RUUVI_MANIFACTURER_ID 0x0499
#define RUUVI_DATA_FORMAT_RAWV2 0x05

struct ruuvi_acceleration {
  uint64_t x;
  uint64_t y;
  uint64_t z;
};

struct ruuvi_frame {
  int16_t temperature;
  uint16_t humidity;
  uint16_t pressure;
  int16_t acceleration_x;
  int16_t acceleration_y;
  int16_t acceleration_z;

  uint16_t battery_voltage;
  uint16_t tx_power;

  uint8_t movement_counter;
  uint16_t sequence_number;
  uint8_t mac[6];
};

bool ruuvi_decode_frame(struct ruuvi_frame *frame, const uint8_t *data,
                        size_t length);

void ruuvi_init(esp_mqtt_client_handle_t mqtt_handle);

#endif // CONFIG_RUUVI_ENABLE
