#pragma once
#include <mqtt_client.h>

void config_init(esp_mqtt_client_handle_t client, const char *prefix);
esp_err_t config_get_bool(const char *key, bool *out);
bool config_get_bool_or(const char *key, bool default_value);
esp_err_t config_get_i32(const char *key, int32_t *out);
int32_t config_get_i32_or(const char *key, int32_t default_value);
