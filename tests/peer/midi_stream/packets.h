#pragma once
#include <stdint.h>
inline void midiPattern(uint8_t *data, unsigned index, unsigned cables = 1) {
  data[0] = uint8_t(((index % cables) << 4) | 9);
  data[1] = uint8_t(0x90 | (index % 16)); data[2] = index % 128; data[3] = (index * 3 + 7) % 128;
}
static const uint8_t midiHelperExpected[][4] = {
  {9,0x92,60,100}, {8,0x83,61,0}, {11,0xb4,7,127}, {12,0xc5,10,0},
  {10,0xa6,62,80}, {13,0xd7,81,0}, {14,0xe8,0,64}, {9,0x9f,127,127},
  {4,0xf0,1,2}, {7,3,4,0xf7}, {15,0xf8,0,0}, {2,0xf1,5,0},
  {3,0xf2,1,2}, {5,0xf6,0,0}, {6,0xf0,0xf7,0}
};
constexpr unsigned midiHelperCount = sizeof(midiHelperExpected) / 4;
