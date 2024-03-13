#include <stddef.h>
#include <string.h>

struct ruuvi_name {
  uint8_t mac[6];
  const char *name;
};

#define MAC(a, b, c, d, e, f)                                                  \
  { 0x##a, 0x##b, 0x##c, 0x##d, 0x##e, 0x##f }
struct ruuvi_name RUUVI_NAMES[] = {
    {MAC(c2, 48, c3, 40, e6, d0), "Kitchen"},
    {MAC(ce, 58, 41, ee, ac, e8), "Living Room"},
    {MAC(d3, c1, 5f, 36, 49, a3), "Bedroom"},
    {MAC(df, a9, fb, bf, aa, 09), "Engine Bay"},
    {MAC(e6, c9, 36, f6, 12, 3f), "Bathroom"},
    {{0x0}, NULL},
};
#undef MAC

static inline const char *ruuvi_find_name(const uint8_t *mac) {
  for (const struct ruuvi_name *p = RUUVI_NAMES; p->name != NULL; p++) {
    if (memcmp(mac, &p->mac, 6) == 0) {
      return p->name;
    }
  }
  return NULL;
}
