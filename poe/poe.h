#ifndef _POE_H_
#define _POE_H_

#include <stdint.h>
#include <stdbool.h>

#define POE_NONE	0x1000
#define RTL8238C	0x8239
#define HS104		0x0104

#ifndef MACHINE_POE_CHIP
#define MACHINE_POE_CHIP POE_NONE
#endif

#if (MACHINE_POE_CHIP == RTL8238C)
#include "rtl8238c.h"
#elif (MACHINE_POE_CHIP == HS104)
#include "hs104.h"
#elif (MACHINE_POE_CHIP != POE_NONE)
#error "Unsupported MACHINE_POE_CHIP"
#endif

/* PoE is present when the machine selects a controller. NO_POE=1 builds a PoE
 * board without it: the driver and every chip-agnostic consumer drop out, and
 * the `poe` command answers that the machine has no PoE. */
#if (MACHINE_POE_CHIP != POE_NONE) && !defined(NO_POE)
#define POE_PRESENT
#endif

/* Board wiring of the PSE, defined by the PCB source. */
struct poe_cfg {
	uint8_t i2c_bus;		// I2C bus, I2CBUS() encoded (SCL/SDA pin)
	uint8_t i2c_dev_addr_base;	// 7-bit address of the first controller
	uint8_t led_addr;		// 7-bit address of a PoE LED latch, 0 = none
	uint8_t bt_ports;		// ports 1..n may run BT/HiPoE, the rest AT (supply budget)
};
extern __code const struct poe_cfg poe_cfg;

void poe_init(void) __banked;

#ifdef POE_PRESENT

/* Register access to a PSE on poe_cfg.i2c_bus: dev is the 7-bit address, a
 * read fetches len (1-4) consecutive registers into poe_i2c_buf. Both return
 * false on a NAK or when the controller stays busy. */
extern __xdata uint8_t poe_i2c_buf[4];
bool poe_i2c_read(uint8_t dev, uint8_t reg, uint8_t len) __banked;
bool poe_i2c_write(uint8_t dev, uint8_t reg, uint8_t val) __banked;

/* Bring PoE up: the driver does whatever its chip needs (e.g. upload firmware
 * and enable ports). Run at boot and on `poe load`; afterwards the controller
 * powers detected PDs. */
void poe_bringup(void) __banked;

/* Enable/disable PoE on a single port (`poe port <n> <on|off>`). port_num is
 * 0-based; port_on: 1 = enable, 0 = disable. */
void poe_port_set(uint8_t port_num, uint8_t port_on) __banked;

/* PoE type of a port, chip-agnostic */
#define POE_TYPE_AF	0	// 802.3af, 15.4 W
#define POE_TYPE_AT	1	// 802.3at, 30 W
#define POE_TYPE_BT	2	// 802.3bt, 60 W
#define POE_TYPE_HIPOE	3	// vendor high power, 90 W

/* Set the PoE type of a port (0-based). Returns 0 on success, non-zero when
 * the port, the type or the supply budget (poe_cfg.bt_ports) does not allow it. */
uint8_t poe_port_type_set(uint8_t port_num, uint8_t type) __banked;

/* Power-cycle a port once (0-based). */
void poe_port_restart(uint8_t port_num) __banked;

/* Called once a second from the main loop: LEDs and other housekeeping. */
void poe_poll(void) __banked;

/* Enable/disable ALL ports at once (`poe global <on|off>` + the web UI's "All
 * PoE"). on: 1 = enable all, 0 = disable all. */
void poe_global_set(uint8_t on) __banked;

/*
 * Normalized per-port status. The driver decodes its (chip-specific) hardware
 * into these chip-agnostic fields, so the consumers - /poe.json and the web UI -
 * present PoE status with no knowledge of the controller at all. Set
 * poe_port_num (0-based), call poe_get_port(), read back:
 */
extern __xdata uint8_t poe_port_num;	// poe_get_port() input: port 0-based
extern __xdata uint8_t  poe_st_ok;	// 1 = the controller answered; else the rest is unknown
extern __xdata uint8_t  poe_st_admin;	// 1 = administratively enabled
extern __xdata uint8_t  poe_st_on;	// 1 = actually delivering power to a PD
extern __xdata uint8_t  poe_st_class;	// PD class 0..8 (0xff = none / unknown)
extern __xdata uint8_t  poe_st_volt;	// port voltage, volts
extern __xdata uint16_t poe_st_mv;	// port voltage, mV
extern __xdata uint16_t poe_st_ma;	// port current, mA
extern __xdata uint8_t  poe_st_type;	// POE_TYPE_*, 0xff = unknown
void poe_get_port(void) __banked;

#endif /* POE_PRESENT */

#endif
