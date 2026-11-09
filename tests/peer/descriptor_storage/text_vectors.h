#pragma once
#include <stdint.h>
#include <string.h>
namespace text_probe {
constexpr uint16_t manufacturer[] = {0x65e5, 0x672c, 0x8a9e, 0x20, 0xd83e, 0xddea};
constexpr uint16_t name[] = {0x8a08, 0x6e2c};
inline void makeUrl(char *url) {
  memcpy(url, "https://example.test/", 21);
  for (unsigned i = 13; i < 243; ++i) url[8 + i] = 'a' + i % 26;
  memcpy(url + 8 + 243, "日本語", 9);
  url[8 + 252] = 0;
}
}
