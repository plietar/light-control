#include <stdbool.h>
#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(LIGHT_EVENT);

enum {
  LIGHT_EVENT_INPUT_CHANGED = 0,
  LIGHT_EVENT_STATE_CHANGED,
};

void light_init();
bool light_get_state();
void light_set_state(bool level, bool fade);
