/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */


#define DWC2_COMMON_DEBUG   2

#include "dwc2_common.h"

//--------------------------------------------------------------------
//
//--------------------------------------------------------------------
static void reset_core(dwc2_regs_t* dwc2) {
  // The software must check that bit 31 in this register is set to 1 (AHB Master is Idle) before starting any operation
  while (!(dwc2->grstctl & GRSTCTL_AHBIDL)) {
  }

  const uint32_t gsnpsid = dwc2->gsnpsid; // preload gsnpsid which is not readable while resetting
  dwc2->grstctl |= GRSTCTL_CSRST; // reset core

  if ((gsnpsid & DWC2_CORE_REV_MASK) < (DWC2_CORE_REV_4_20a & DWC2_CORE_REV_MASK)) {
    // prior v4.20a: CSRST is self-clearing, and the core clears this bit after all the necessary logic is reset in
    // the core, which can take several clocks, depending on the current state of the core. Once this bit has been
    // cleared, the software must wait at least 3 PHY clocks before accessing the PHY domain (synchronization delay).
    while (dwc2->grstctl & GRSTCTL_CSRST) {}
  } else {
    // From v4.20a: CSRST bit is write only. The application must clear this bit after checking the bit 29 of this
    // register i.e Core Soft Reset Done CSRT_DONE (w1c)
    while (!(dwc2->grstctl & GRSTCTL_CSRST_DONE)) {}
    dwc2->grstctl = (dwc2->grstctl & ~GRSTCTL_CSRST) | GRSTCTL_CSRST_DONE;
  }

  while (!(dwc2->grstctl & GRSTCTL_AHBIDL)) {} // wait for AHB master IDLE
}

// Dedicated FS PHY is internal with a clock 48Mhz.
static void phy_fs_init(dwc2_regs_t* dwc2) {

  uint32_t gusbcfg = dwc2->gusbcfg;

  // Select FS PHY
  gusbcfg |= GUSBCFG_PHYSEL;
  dwc2->gusbcfg = gusbcfg;

  // MCU specific PHY init before reset

  // Reset core after selecting PHY
  reset_core(dwc2);

  // USB turnaround time is critical for certification where long cables and 5-Hubs are used.
  // So if you need the AHB to run at less than 30 MHz, and if USB turnaround time is not critical,
  // these bits can be programmed to a larger value. Default is 5
  gusbcfg &= ~GUSBCFG_TRDT_Msk;
  gusbcfg |= 5u << GUSBCFG_TRDT_Pos;
  dwc2->gusbcfg = gusbcfg;

  // MCU specific PHY update post reset
}

/* dwc2 has 2 highspeed PHYs options
 * - UTMI+ is internal highspeed PHY, can be clocked at 30 Mhz (8-bit) or 60 Mhz (16-bit).
 * - ULPI is external highspeed PHY, clocked at 60Mhz with 8-bit interface.
 *
 * In addition, UTMI+/ULPI can be shared to run at fullspeed mode with 48Mhz
 */
static void phy_hs_init(dwc2_regs_t* dwc2) {
  uint32_t gusbcfg = dwc2->gusbcfg;
  const dwc2_ghwcfg2_t ghwcfg2 = {.value = dwc2->ghwcfg2};
  const dwc2_ghwcfg4_t ghwcfg4 = {.value = dwc2->ghwcfg4};

  const uint8_t phy_width = ghwcfg4.phy_data_width ? 16 : 8;

  // De-select FS PHY
  gusbcfg &= ~GUSBCFG_PHYSEL;

  if (ghwcfg2.hs_phy_type == GHWCFG2_HSPHY_ULPI) {

    // Select ULPI PHY (external)
    gusbcfg |= GUSBCFG_ULPI_UTMI_SEL;

    // ULPI is always 8-bit interface
    gusbcfg &= ~GUSBCFG_PHYIF16;

    // ULPI select single data rate
    gusbcfg &= ~GUSBCFG_DDRSEL;

    // default internal VBUS Indicator and Drive
    gusbcfg &= ~(GUSBCFG_ULPIEVBUSD | GUSBCFG_ULPIEVBUSI);

    // Disable FS/LS ULPI
    gusbcfg &= ~(GUSBCFG_ULPIFSLS | GUSBCFG_ULPICSM);
  } else {

    // Select UTMI+ PHY (internal)
    gusbcfg &= ~GUSBCFG_ULPI_UTMI_SEL;

    // Set 16-bit interface if supported
    if (phy_width == 16) {
      gusbcfg |= GUSBCFG_PHYIF16;
    } else {
      gusbcfg &= ~GUSBCFG_PHYIF16;
    }
  }

  // Apply config
  dwc2->gusbcfg = gusbcfg;

  // mcu specific phy init

  // Reset core after selecting PHY
  reset_core(dwc2);

  // Set turn-around, must after core reset otherwise it will be clear
  // - 9 if using 8-bit PHY interface
  // - 5 if using 16-bit PHY interface
  gusbcfg &= ~GUSBCFG_TRDT_Msk;
  gusbcfg |= (phy_width == 16 ? 5u : 9u) << GUSBCFG_TRDT_Pos;
  dwc2->gusbcfg = gusbcfg;

  // MCU specific PHY update post reset
}

static bool check_dwc2(dwc2_regs_t* dwc2) {

  // For some reason: GD32VF103 gsnpsid and all hwcfg register are always zero (skip it)
  (void)dwc2;
  enum { GSNPSID_ID_MASK = ESPUSB_MASK(31, 16) };
  const uint32_t gsnpsid = dwc2->gsnpsid & GSNPSID_ID_MASK;
  ESPUSB_REQUIRE(gsnpsid == DWC2_OTG_ID || gsnpsid == DWC2_FS_IOT_ID || gsnpsid == DWC2_HS_IOT_ID);

  return true;
}

//--------------------------------------------------------------------
//
//--------------------------------------------------------------------
bool dwc2_core_is_highspeed_phy(dwc2_regs_t* dwc2, bool prefer_hs_phy) {
  const dwc2_ghwcfg2_t ghwcfg2    = {.value = dwc2->ghwcfg2};
  const bool           has_hs_phy = (ghwcfg2.hs_phy_type != GHWCFG2_HSPHY_NOT_SUPPORTED);

  if (prefer_hs_phy) {
    return has_hs_phy;
  } else {
    const bool has_fs_phy = (ghwcfg2.fs_phy_type != GHWCFG2_FSPHY_NOT_SUPPORTED);
    // false if has fs phy, otherwise true since hs phy is the only available phy
    return !has_fs_phy && has_hs_phy;
  }
}

bool dwc2_core_init(uint8_t rhport, bool is_hs_phy) {
  dwc2_regs_t* dwc2 = DWC2_REG(rhport);

  // Check Synopsys ID register, failed if controller clock/power is not enabled
  ESPUSB_REQUIRE(check_dwc2(dwc2));

  // disable global interrupt
  dwc2->gahbcfg &= ~GAHBCFG_GINT;

  if (is_hs_phy) {
    phy_hs_init(dwc2);
  } else {
    phy_fs_init(dwc2);
  }

  /* Set HS/FS Timeout Calibration to 7 (max available value).
   * The number of PHY clocks that the application programs in
   * this field is added to the high/full speed interpacket timeout
   * duration in the core to account for any additional delays
   * introduced by the PHY. This can be required, because the delay
   * introduced by the PHY in generating the linestate condition
   * can vary from one PHY to another. */
  dwc2->gusbcfg |= (7ul << GUSBCFG_TOCAL_Pos);

  // Enable PHY clock TODO stop/gate clock when suspended mode
  dwc2->pcgcctl &= ~(PCGCCTL_STOPPCLK | PCGCCTL_GATEHCLK | PCGCCTL_PWRCLMP | PCGCCTL_RSTPDWNMODULE);

  dfifo_flush_tx(dwc2, 0x10); // all tx fifo
  dfifo_flush_rx(dwc2);

  // Clear pending and disable all interrupts
  dwc2->gintsts = 0xFFFFFFFFU;
  dwc2->gotgint = 0xFFFFFFFFU;
  dwc2->gintmsk = 0;


  // The ESP device engine supports internal buffer DMA only.
  dwc2->gahbcfg |= GAHBCFG_DMAEN | GAHBCFG_HBSTLEN_2;

  return true;
}

void dwc2_core_deinit(uint8_t rhport) {
  dwc2_regs_t* dwc2 = DWC2_REG(rhport);

  // Disable global interrupt
  dwc2->gahbcfg &= ~GAHBCFG_GINT;

  // Reset core: this also flushes FIFOs and clears all interrupt registers
  reset_core(dwc2);

  // Stop PHY clock and gate HCLK for power saving (per databook chapter 14)
  dwc2->pcgcctl |= PCGCCTL_STOPPCLK | PCGCCTL_GATEHCLK;

  // PHY allocation/power belongs to the native ESP runtime.

}
