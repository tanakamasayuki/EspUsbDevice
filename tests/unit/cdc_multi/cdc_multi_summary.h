#pragma once

#include <stdint.h>

// Kept out of the .ino because the Arduino preprocessor inserts the generated
// function prototypes above any type declared in the sketch itself, so a
// helper returning this struct would not compile there.
struct ConfigSummary
{
  uint8_t interfaces = 0;
  uint8_t associations = 0;
  uint8_t inEndpoints = 0;
  uint8_t outEndpoints = 0;
  uint8_t firstAssociationString = 0;
  uint8_t secondAssociationString = 0;
};
