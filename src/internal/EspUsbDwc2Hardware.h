/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * Native ESP Device register access and reset/FIFO primitives.
 * Derived origin and MIT license: third_party/tinyusb/.
 */

#pragma once
#include "EspUsbDwc2Support.h"
#include "EspUsbDwc2Platform.h"
#include "EspUsbController.h"
#include "EspUsbDwc2Registers.h"

static inline EspUsbDwc2Registers *espusb_dwc2_registers(uint8_t port) {
  const EspUsbDwc2Port *info = espusb_dwc2_port(port);
  return info ? (EspUsbDwc2Registers *)info->reg_base : NULL;
}
bool espusb_dwc2_highspeed_phy(EspUsbDwc2Registers *, bool preferHighSpeed);
bool espusb_dwc2_phy_start(uint8_t port, bool highSpeedPhy);
void espusb_dwc2_phy_stop(uint8_t port);
static inline void espusb_dwc2_flush_tx(EspUsbDwc2Registers *regs, uint8_t fifo) {
  regs->grstctl = GRSTCTL_TXFFLSH | (fifo << GRSTCTL_TXFNUM_Pos);
  while (regs->grstctl & GRSTCTL_TXFFLSH_Msk) {}
}
static inline void espusb_dwc2_flush_rx(EspUsbDwc2Registers *regs) {
  regs->grstctl = GRSTCTL_RXFFLSH;
  while (regs->grstctl & GRSTCTL_RXFFLSH_Msk) {}
}
