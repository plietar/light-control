#pragma once
#include <stdint.h>

static inline uint16_t read_16le(const uint8_t *data) {
  return data[1] << 8 | data[0];
}

static inline uint16_t read_16be(const uint8_t *data) {
  return data[0] << 8 | data[1];
}
