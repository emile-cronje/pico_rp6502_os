/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usbh_core.h"
#include "hardware/structs/usb.h"
#include "usb/hid.h"
#include "usb/msc.h"
#include "usb/usb.h"
#include "usb/xin.h"

#if defined(DEBUG_RIA_USB) || defined(DEBUG_RIA_USB_USB)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

void usb_init(void)
{
    // Initialize CherryUSB host stack with bus 0 and USB hardware base address
    usbh_initialize(0, USBCTRL_REGS_BASE);
}

void usb_task(void)
{
    // CherryUSB handles tasks internally through interrupts
    // No explicit task call needed like in TinyUSB
}

void usb_print_status(void)
{
    int count_gamepad = hid_pad_count() + xin_pad_count();
    printf("USB : ");
    hid_print_status();
    printf(", %d gamepad%s", count_gamepad, count_gamepad == 1 ? "" : "s");
    msc_print_status();
}
