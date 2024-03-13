#pragma once
#include <mqtt_client.h>

void local_control_init(esp_mqtt_client_handle_t client, const char *prefix);
