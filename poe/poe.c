/*
 * Chip-agnostic PoE support: the no-PoE stub, the status variables behind
 * poe_get_port() and register access to the PSE over the switch's I2C master.
 * The controller itself is handled by the driver MACHINE_POE_CHIP selects.
 */
#pragma codeseg BANK3
#pragma constseg BANK3

#include <stdint.h>
#include <stdbool.h>

#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_common.h"
#include "boot.h"
#include "machine.h"

#ifndef POE_PRESENT
void poe_init(void) __banked
{
	print_string("\nNo PoE Support.\n");
}
#else

__xdata uint8_t poe_port_num;
__xdata uint8_t poe_st_ok;
__xdata uint8_t poe_st_admin;
__xdata uint8_t poe_st_on;
__xdata uint8_t poe_st_class;
__xdata uint8_t poe_st_volt;
__xdata uint16_t poe_st_mv;
__xdata uint16_t poe_st_ma;
__xdata uint8_t poe_st_type;

__xdata uint8_t poe_i2c_buf[4];
static __xdata bool poe_i2c_muxed;

/* A PSE that stops answering must not hang the caller. */
#define POE_I2C_POLL_LIMIT	0xffff

/* Start a transfer of len bytes at reg and wait for the controller. */
static bool poe_i2c_go(uint8_t dev, uint8_t len, uint8_t write)
{
	__xdata uint16_t polls = POE_I2C_POLL_LIMIT;

	// setup_i2c() only muxes the SFP buses
	if (!poe_i2c_muxed) {
		setup_i2c_bus(poe_cfg.i2c_bus);
		poe_i2c_muxed = true;
	}

	REG_WRITE(RTL837X_REG_I2C_CTRL, 0x00,
		  0x1 << (I2C_MEM_ADDR_WIDTH - 16) | (len - 1),
		  (dev >> 5) | poe_cfg.i2c_bus,
		  ((dev << 3) & 0xff) | write | I2C_CTRL_GO);
	do {
		reg_read(RTL837X_REG_I2C_CTRL);
		if (!--polls)
			return false;
	} while (SFR_DATA_0 & I2C_CTRL_GO);

	return !(SFR_DATA_0 & I2C_CTRL_FAIL);
}

bool poe_i2c_read(uint8_t dev, uint8_t reg, uint8_t len) __banked
{
	if (!len || len > 4)
		return false;
	REG_WRITE(RTL837X_REG_I2C_IN, 0, 0, 0, reg);
	if (!poe_i2c_go(dev, len, 0))
		return false;
	reg_read(RTL837X_REG_I2C_OUT);
	poe_i2c_buf[0] = SFR_DATA_0;
	poe_i2c_buf[1] = SFR_DATA_8;
	poe_i2c_buf[2] = SFR_DATA_16;
	poe_i2c_buf[3] = SFR_DATA_24;
	return true;
}

bool poe_i2c_write(uint8_t dev, uint8_t reg, uint8_t val) __banked
{
	REG_WRITE(RTL837X_REG_I2C_IN, 0, 0, 0, reg);
	REG_WRITE(RTL837X_REG_I2C_OUT, 0, 0, 0, val);
	return poe_i2c_go(dev, 1, I2C_CTRL_WRITE);
}

#endif
