/*
 * Hasivo HS104 PSE driver.
 *
 * The chip needs no firmware download: it is controlled through a small
 * register map over I2C (see hs104.h). Bring-up checks the device id, sets
 * each port's PoE type and switches all ports on, as the stock firmware does.
 * An optional LED latch (poe_cfg.led_addr) shows which ports deliver power.
 *
 * Selected by MACHINE_POE_CHIP == HS104 in the PCB source.
 */
#pragma codeseg BANK3
#pragma constseg BANK3

#include <stdint.h>
#include <stdbool.h>

#include "rtl837x_common.h"
#include "poe.h"

#define HS104_ADDR	(poe_cfg.i2c_dev_addr_base)
#define HS104_ALL	((1 << HS104_PORTS) - 1)
#define LED_REG		0x01

// POE_TYPE_* to the chip's 2-bit code: 0 BT, 1 HiPoE, 2 AT, 3 AF
static __code const uint8_t type_to_hs[4] = { 3, 2, 0, 1 };
static __code const uint8_t hs_to_type[4] = { POE_TYPE_BT, POE_TYPE_HIPOE, POE_TYPE_AT, POE_TYPE_AF };

static __xdata uint8_t hs_val;
static __xdata uint8_t hs_present;	// bring-up found the chip; poe_poll() relies on it
static __xdata uint8_t hs_skip;		// polls since the last probe of a missing chip

#define HS104_PROBE_POLLS	30	// look for a missing chip every 30 s


static bool hs104_read(uint8_t reg)
{
	if (!poe_i2c_read(HS104_ADDR, reg, 1))
		return false;
	hs_val = poe_i2c_buf[0];
	return true;
}


// Stock reads 16-bit values as two single-byte reads, high byte first
static __xdata uint16_t hs_val16;

static bool hs104_read16(uint8_t reg)
{
	if (!hs104_read(reg))
		return false;
	hs_val16 = (uint16_t)(hs_val & 0x3f) << 8;	// 14-bit values
	if (!hs104_read(reg + 1))
		return false;
	hs_val16 |= hs_val;
	return true;
}


static bool hs104_write(uint8_t reg)
{
	if (poe_i2c_write(HS104_ADDR, reg, hs_val))
		return true;
	print_string("PoE: HS104 write failed\n");
	return false;
}


static bool hs104_pw_en_write(uint8_t ports)
{
	hs_val = HS104_EXECUTE | ports;
	return hs104_write(HS104_REG_PW_EN);
}


// Ports below bt_ports may draw BT power, the rest are held to AT
static uint8_t hs104_default_types(void)
{
	static __xdata uint8_t ch, v;

	v = 0;
	for (ch = 0; ch < HS104_PORTS; ch++)
		v |= type_to_hs[ch < poe_cfg.bt_ports ? POE_TYPE_BT : POE_TYPE_AT] << (6 - 2 * ch);
	return v;
}


static void led_write(uint8_t v)
{
	if (poe_cfg.led_addr)
		poe_i2c_write(poe_cfg.led_addr, LED_REG, v);
}


void poe_bringup(void) __banked
{
	if (!hs104_read(HS104_REG_DEVID)) {
		print_string("PoE: no HS104 answering at 0x");
		print_byte(HS104_ADDR);
		write_char('\n');
		return;
	}
	if (hs_val != HS104_DEVICE_ID) {
		print_string("PoE: unexpected device id 0x");
		print_byte(hs_val);
		print_string(" at 0x");
		print_byte(HS104_ADDR);
		write_char('\n');
		return;
	}
	led_write(HS104_EXECUTE | HS104_ALL);	// lamp test until the first poll
	hs_val = hs104_default_types();
	if (!hs104_write(HS104_REG_PROTOCOL) || !hs104_pw_en_write(HS104_ALL)) {
		led_write(HS104_EXECUTE);
		return;
	}
	hs_present = 1;
	print_string("PoE: HS104 at 0x");
	print_byte(HS104_ADDR);
	print_string(", all ports enabled, types 0x");
	print_byte(hs104_default_types());
	write_char('\n');
}


void poe_port_set(uint8_t port_num, uint8_t port_on) __banked
{
	static __xdata uint8_t ports;

	if (port_num >= HS104_PORTS)
		return;
	if (!hs104_read(HS104_REG_PW_EN)) {
		print_string("PoE: HS104 read failed\n");
		return;
	}
	ports = hs_val & HS104_ALL;
	if (port_on)
		ports |= HS104_PORT_BIT(port_num);
	else
		ports &= ~HS104_PORT_BIT(port_num);
	hs104_pw_en_write(ports);
}


void poe_global_set(uint8_t on) __banked
{
	hs104_pw_en_write(on ? HS104_ALL : 0);
}


uint8_t poe_port_type_set(uint8_t port_num, uint8_t type) __banked
{
	static __xdata uint8_t shift, ch;

	if (port_num >= HS104_PORTS || type > POE_TYPE_HIPOE)
		return 1;
	if ((type == POE_TYPE_BT || type == POE_TYPE_HIPOE) && port_num >= poe_cfg.bt_ports)
		return 1;
	if (!hs104_read(HS104_REG_PROTOCOL))
		return 1;
	shift = 6 - 2 * port_num;
	hs_val = (hs_val & ~(3 << shift)) | (type_to_hs[type] << shift);
	// a bad read must not hand BT or HiPoE to a port beyond the budget
	for (ch = poe_cfg.bt_ports; ch < HS104_PORTS; ch++) {
		shift = 6 - 2 * ch;
		if (!((hs_val >> shift) & 2))	// codes 0 (BT) and 1 (HiPoE)
			hs_val = (hs_val & ~(3 << shift)) | (type_to_hs[POE_TYPE_AT] << shift);
	}
	return hs104_write(HS104_REG_PROTOCOL) ? 0 : 1;
}


void poe_port_restart(uint8_t port_num) __banked
{
	if (port_num >= HS104_PORTS)
		return;
	hs_val = HS104_EXECUTE | HS104_PORT_BIT(port_num);
	hs104_write(HS104_REG_RESTART);
}


void poe_get_port(void) __banked
{
	static __xdata uint8_t bit;

	bit = HS104_PORT_BIT(poe_port_num);
	poe_st_ok = 0;
	poe_st_admin = 0;
	poe_st_on = 0;
	poe_st_class = 0xff;
	poe_st_volt = 0;
	poe_st_mv = 0;
	poe_st_ma = 0;
	poe_st_type = 0xff;
	if (poe_port_num >= HS104_PORTS)
		return;

	if (!hs104_read(HS104_REG_PW_EN))
		return;
	poe_st_admin = (hs_val & bit) ? 1 : 0;
	// stock discards a status read without the fixed 0x4 nibble
	if (!hs104_read(HS104_REG_PW_STATUS) || (hs_val & 0xf0) != 0x40) {
		poe_st_admin = 0;
		return;
	}
	poe_st_ok = 1;
	poe_st_on = (hs_val & bit) ? 1 : 0;

	if (hs104_read16(HS104_REG_INPUT_V)) {
		poe_st_mv = hs_val16 > 6553 ? 0xffff : hs_val16 * 10;
		poe_st_volt = hs_val16 / 100;
	}
	if (hs104_read(HS104_REG_PROTOCOL))
		poe_st_type = hs_to_type[(hs_val >> (6 - 2 * poe_port_num)) & 3];

	if (!poe_st_on)
		return;
	if (hs104_read(HS104_REG_PORT0_CLASS + poe_port_num))
		poe_st_class = hs_val;
	if (hs104_read16(HS104_REG_PORT0_I + 2 * poe_port_num))
		poe_st_ma = hs_val16;
}


void poe_poll(void) __banked
{
	static __xdata uint8_t leds;

	if (!hs_present) {
		if (++hs_skip < HS104_PROBE_POLLS)
			return;
		hs_skip = 0;
		if (!hs104_read(HS104_REG_DEVID) || hs_val != HS104_DEVICE_ID)
			return;
		hs_present = 1;
	}
	leds = 0;
	if (hs104_read(HS104_REG_PW_STATUS) && (hs_val & 0xf0) == 0x40) {
		leds = hs_val & HS104_ALL;
	} else {
		hs_present = 0;		// silent: show nothing powered, probe now and then
		hs_skip = 0;
	}
	if (!poe_cfg.led_addr || !poe_i2c_read(poe_cfg.led_addr, LED_REG, 1))
		return;
	hs_val = (poe_i2c_buf[0] & ~HS104_ALL) | HS104_EXECUTE | leds;
	if (hs_val != poe_i2c_buf[0])
		led_write(hs_val);
}


void poe_init(void) __banked
{
	poe_bringup();
}
