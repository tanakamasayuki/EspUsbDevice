/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#pragma once
#include "../../../internal/EspUsbDwc2Support.h"
#include "../../../internal/EspUsbDwc2Platform.h"
#include "../../../internal/EspUsbController.h"
#include "dwc2_type.h"

static inline dwc2_regs_t *DWC2_REG(uint8_t port) {
  const EspUsbDwc2Port *info = espusb_dwc2_port(port);
  return info ? (dwc2_regs_t *)info->reg_base : NULL;
}
bool dwc2_core_is_highspeed_phy(dwc2_regs_t *, bool preferHighSpeed);
bool dwc2_core_init(uint8_t port, bool highSpeedPhy);
void dwc2_core_deinit(uint8_t port);
static inline void dfifo_flush_tx(dwc2_regs_t *regs, uint8_t fifo) {
  regs->grstctl = GRSTCTL_TXFFLSH | (fifo << GRSTCTL_TXFNUM_Pos);
  while (regs->grstctl & GRSTCTL_TXFFLSH_Msk) {}
}
static inline void dfifo_flush_rx(dwc2_regs_t *regs) {
  regs->grstctl = GRSTCTL_RXFFLSH;
  while (regs->grstctl & GRSTCTL_RXFFLSH_Msk) {}
}
