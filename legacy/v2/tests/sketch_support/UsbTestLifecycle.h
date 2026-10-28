#pragma once

#include <Arduino.h>

// One hardware test per module: wait until pytest has connected (and uploaded
// every peer) before enabling USB or printing one-shot test output. Q is a
// repeatable readiness query; G releases setup without printing an ACK that
// could make expect() consume the enumeration output.
static void waitForUsbTestStart(const char *identity)
{
  for (;;)
  {
    if (Serial.available() > 0)
    {
      const char command = Serial.read();
      if (command == 'Q')
      {
        Serial.printf("TEST_IDLE %s\n", identity);
      }
      else if (command == 'G')
      {
        return;
      }
      else if (command == 0x1f)
      {
        // USB has not started; cleanup can acknowledge an aborted setup too.
        Serial.println("TEST_STOPPED");
      }
    }
    delay(1);
  }
}

// A control byte avoids collisions with existing sketch commands (including
// printable keyboard input). Check before any wait-for-device or application
// work, and stay quiet after shutdown until the next module uploads firmware.
static inline bool usbTestControlPending()
{
  return Serial.available() > 0 &&
         (Serial.peek() == 0x1c || Serial.peek() == 0x1f);
}

template <typename Stop>
static bool handleUsbTestStop(Stop stop, const char *peerIdentity = nullptr)
{
  static bool stopped = false;
  if (peerIdentity && Serial.available() > 0 && Serial.peek() == 0x1c)
  {
    Serial.read();
    // Answer from loop(), after setup has finished configuring the USB device.
    // Unlike device.ready(), this does not require the host to have started.
    Serial.printf("TEST_PEER_BOOTED %s\n", peerIdentity);
    return true;
  }
  if (Serial.available() > 0 && Serial.peek() == 0x1f)
  {
    Serial.read();
    if (!stopped)
    {
      Serial.printf("TEST_STOPPING\n");
      stop();
      stopped = true;
    }
    Serial.println("TEST_STOPPED");
  }
  if (stopped)
  {
    delay(1);
  }
  return stopped;
}
