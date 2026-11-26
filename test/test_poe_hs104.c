/*
 * Host test for poe/hs104.c: the driver runs unmodified against fake register
 * files for the HS104 (0x0d) and the PoE LED latch (0x4d) behind
 * poe_i2c_read()/poe_i2c_write().
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rtl837x_common.h"
#include "poe.h"

#define PSE	0x0d
#define LED	0x4d

const struct poe_cfg poe_cfg = { .i2c_bus = 0, .i2c_dev_addr_base = PSE, .led_addr = LED, .bt_ports = 1 };

uint8_t poe_port_num, poe_st_ok, poe_st_admin, poe_st_on, poe_st_class, poe_st_volt, poe_st_type;
uint16_t poe_st_ma, poe_st_mv;
uint8_t poe_i2c_buf[4];

static int verbose;
static char out[1024];
static void out_add(const char *s)
{
	strncat(out, s, sizeof(out) - strlen(out) - 1);
	if (verbose)
		fputs(s, stdout);
}
void print_string(const char *s) { out_add(s); }
void print_byte(uint8_t b) { char t[4]; snprintf(t, sizeof(t), "%02x", b); out_add(t); }
void print_short(uint16_t v) { char t[6]; snprintf(t, sizeof(t), "%04x", v); out_add(t); }
void itoa(uint8_t v) { char t[4]; snprintf(t, sizeof(t), "%u", v); out_add(t); }
void write_char(char c) { char t[2] = { c, 0 }; out_add(t); }

/* Fake chips */
static uint8_t regs[0x30], led[0x10];
static bool nak;		// every transfer fails
static bool nak_writes;		// only writes fail
static bool burst_fails;	// reads of more than one byte fail
static bool nak_pse;		// only the PSE is silent, the LED latch answers
static bool nak_pse_writes;	// only PSE writes fail
static uint8_t last_dev;
static int writes, led_writes, reads;
static uint8_t last_wreg, last_wval;
static uint8_t wlog[16][2];	// PSE writes in order: reg, value

bool poe_i2c_read(uint8_t dev, uint8_t reg, uint8_t len)
{
	uint8_t *r = dev == LED ? led : regs;

	last_dev = dev;
	reads++;
	if ((nak_pse && dev == PSE) || nak || !len || len > 4 || (dev != PSE && dev != LED) || (burst_fails && len > 1))
		return false;
	for (uint8_t i = 0; i < len; i++)
		poe_i2c_buf[i] = r[reg + i];
	return true;
}

bool poe_i2c_write(uint8_t dev, uint8_t reg, uint8_t val)
{
	last_dev = dev;
	if (nak || nak_writes || (dev == PSE && (nak_pse || nak_pse_writes)))
		return false;
	if (dev == LED) {
		led_writes++;
		led[reg] = val;
		return true;
	}
	if (writes < 16) {
		wlog[writes][0] = reg;
		wlog[writes][1] = val;
	}
	writes++;
	last_wreg = reg;
	last_wval = val;
	if (reg == HS104_REG_PW_EN)	// the chip keeps the fixed 0x4 nibble
		regs[reg] = 0x40 | (val & 0x0f);
	else if (reg != HS104_REG_RESTART)
		regs[reg] = val;
	return true;
}

static void chip_reset(void)
{
	memset(regs, 0, sizeof(regs));
	memset(led, 0, sizeof(led));
	memset(wlog, 0, sizeof(wlog));
	regs[HS104_REG_DEVID] = HS104_DEVICE_ID;
	regs[HS104_REG_PW_EN] = 0x40;
	regs[HS104_REG_PW_STATUS] = 0x40;
	regs[HS104_REG_PROTOCOL] = 0xaa;	// power-up default: AT everywhere
	nak = nak_writes = burst_fails = nak_pse = nak_pse_writes = false;
	out[0] = 0;
	writes = led_writes = 0;
	last_wreg = last_wval = 0;
}

static int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_bringup_sets_types_then_enables_all_ports(void)
{
	chip_reset();
	poe_bringup();
	CHECK(writes == 2);
	CHECK(wlog[0][0] == HS104_REG_PROTOCOL && wlog[0][1] == 0x2a);	// BT AT AT AT
	CHECK(wlog[1][0] == HS104_REG_PW_EN && wlog[1][1] == 0x4f);
	CHECK(strstr(out, "all ports enabled") != NULL);
}

static void test_bringup_lamp_test(void)
{
	chip_reset();
	poe_bringup();
	CHECK(led[0x01] == 0x4f);
}

static void test_bringup_failed_write_not_reported_as_enabled(void)
{
	chip_reset();
	nak_writes = true;
	poe_bringup();
	CHECK(strstr(out, "all ports enabled") == NULL);
	CHECK(strstr(out, "failed") != NULL);
}

static void test_bringup_wrong_id_writes_nothing(void)
{
	chip_reset();
	regs[HS104_REG_DEVID] = 0x55;
	poe_bringup();
	CHECK(writes == 0);
	CHECK(strstr(out, "unexpected device id") != NULL);
}

static void test_bringup_nak_writes_nothing(void)
{
	chip_reset();
	nak = true;
	poe_bringup();
	CHECK(writes == 0);
	CHECK(strstr(out, "no HS104") != NULL);
}

static void test_port_off_clears_its_bit_only(void)
{
	chip_reset();
	regs[HS104_REG_PW_EN] = 0x4f;
	poe_port_set(0, 0);		// port 1 is bit 3
	CHECK(last_wreg == HS104_REG_PW_EN && last_wval == 0x47);
	poe_port_set(3, 0);		// port 4 is bit 0
	CHECK(last_wval == 0x46);
}

static void test_port_set_masks_upper_readback_bits(void)
{
	chip_reset();
	regs[HS104_REG_PW_EN] = 0xcf;		// unknown upper bits read back set
	poe_port_set(0, 0);
	CHECK(last_wval == 0x47);
}

static void test_port_on_sets_bit_with_execute(void)
{
	chip_reset();
	poe_port_set(3, 1);
	CHECK(last_wval == 0x41);
	poe_port_set(1, 1);
	CHECK(last_wval == 0x45);
}

static void test_port_out_of_range_ignored(void)
{
	chip_reset();
	poe_port_set(4, 1);
	poe_port_restart(4);
	CHECK(poe_port_type_set(4, POE_TYPE_AF) != 0);
	CHECK(writes == 0);
}

static void test_port_set_nak_writes_nothing(void)
{
	chip_reset();
	nak = true;
	poe_port_set(0, 1);
	CHECK(writes == 0);
}

static void test_global(void)
{
	chip_reset();
	poe_global_set(1);
	CHECK(last_wreg == HS104_REG_PW_EN && last_wval == 0x4f);
	poe_global_set(0);
	CHECK(last_wval == 0x40);
}

static void test_type_set_rewrites_only_its_field(void)
{
	chip_reset();
	regs[HS104_REG_PROTOCOL] = 0x2a;		// BT AT AT AT
	CHECK(poe_port_type_set(1, POE_TYPE_AF) == 0);	// port 2 is bits 5:4
	CHECK(last_wreg == HS104_REG_PROTOCOL && last_wval == 0x3a);
	CHECK(poe_port_type_set(3, POE_TYPE_AF) == 0);	// port 4 is bits 1:0
	CHECK(last_wval == 0x3b);
	CHECK(poe_port_type_set(0, POE_TYPE_HIPOE) == 0);
	CHECK(last_wval == 0x7b);
}

static void test_type_bt_refused_beyond_bt_ports(void)
{
	chip_reset();
	regs[HS104_REG_PROTOCOL] = 0x2a;
	CHECK(poe_port_type_set(1, POE_TYPE_BT) != 0);
	CHECK(poe_port_type_set(2, POE_TYPE_HIPOE) != 0);
	CHECK(writes == 0);
	CHECK(poe_port_type_set(0, POE_TYPE_BT) == 0);
}

static void test_restart_pulses_its_bit(void)
{
	chip_reset();
	poe_port_restart(1);
	CHECK(last_wreg == HS104_REG_RESTART && last_wval == 0x44);
}

static void test_status_decode(void)
{
	chip_reset();
	regs[HS104_REG_PW_EN] = 0x4c;		// ports 1, 2 enabled
	regs[HS104_REG_PW_STATUS] = 0x48;	// port 1 delivering
	regs[HS104_REG_INPUT_V] = 0x14;		// 0x1463 = 5219 -> 52.19 V
	regs[HS104_REG_INPUT_V + 1] = 0x63;
	regs[HS104_REG_PORT0_I] = 0x05;		// 0x055a = 1370 mA
	regs[HS104_REG_PORT0_I + 1] = 0x5a;
	regs[HS104_REG_PORT0_CLASS] = 5;
	regs[HS104_REG_PORT0_CLASS + 1] = 4;	// stale class on a port without power
	regs[HS104_REG_PROTOCOL] = 0x2a;

	poe_port_num = 0;
	poe_get_port();
	CHECK(poe_st_ok == 1);
	CHECK(poe_st_admin == 1);
	CHECK(poe_st_on == 1);
	CHECK(poe_st_class == 5);
	CHECK(poe_st_volt == 52);
	CHECK(poe_st_mv == 52190);
	CHECK(poe_st_ma == 1370);
	CHECK(poe_st_type == POE_TYPE_BT);

	poe_port_num = 1;
	poe_get_port();
	CHECK(poe_st_admin == 1);
	CHECK(poe_st_on == 0);
	CHECK(poe_st_class == 0xff);
	CHECK(poe_st_ma == 0);
	CHECK(poe_st_type == POE_TYPE_AT);

	poe_port_num = 3;
	poe_get_port();
	CHECK(poe_st_admin == 0);
	CHECK(poe_st_on == 0);
}

static void test_status_uses_single_byte_reads(void)
{
	chip_reset();
	burst_fails = true;
	regs[HS104_REG_PW_EN] = 0x48;
	regs[HS104_REG_PW_STATUS] = 0x48;
	regs[HS104_REG_INPUT_V] = 0x14;
	regs[HS104_REG_INPUT_V + 1] = 0x63;
	regs[HS104_REG_PORT0_I] = 0x01;
	regs[HS104_REG_PORT0_I + 1] = 0x2c;
	poe_port_num = 0;
	poe_get_port();
	CHECK(poe_st_mv == 52190);
	CHECK(poe_st_ma == 300);
}

static void test_status_rejects_bad_status_marker(void)
{
	chip_reset();
	regs[HS104_REG_PW_EN] = 0x48;
	regs[HS104_REG_PW_STATUS] = 0x08;	// no fixed 0x4 nibble: not a valid read
	poe_port_num = 0;
	poe_get_port();
	CHECK(poe_st_ok == 0);
	CHECK(poe_st_on == 0);
}

static void test_status_nak_reports_unknown(void)
{
	chip_reset();
	nak = true;
	poe_port_num = 0;
	poe_st_admin = poe_st_on = 1;
	poe_get_port();
	CHECK(poe_st_ok == 0);
	CHECK(poe_st_admin == 0);
	CHECK(poe_st_on == 0);
	CHECK(poe_st_class == 0xff);
	CHECK(poe_st_volt == 0);
	CHECK(poe_st_mv == 0);
	CHECK(poe_st_ma == 0);
	CHECK(poe_st_type == 0xff);
}

static void test_poll_drives_leds_from_powered_ports(void)
{
	chip_reset();
	poe_bringup();
	led[0x01] = 0x80;			// an unrelated latch bit stays as it is
	regs[HS104_REG_PW_STATUS] = 0x4a;	// ports 1 and 3 delivering
	poe_poll();
	CHECK(led[0x01] == 0xca);
	led_writes = 0;
	poe_poll();				// nothing changed, nothing written
	CHECK(led_writes == 0);
	regs[HS104_REG_PW_STATUS] = 0x40;
	poe_poll();
	CHECK(led[0x01] == 0xc0);
}

static void test_poll_clears_leds_when_pse_goes_silent(void)
{
	chip_reset();
	poe_bringup();				// lamp test: all LEDs on
	nak_pse = true;
	poe_poll();
	nak_pse = false;
	CHECK(led[0x01] == 0x40);		// no port can be shown powered
}

static void test_failed_bringup_clears_lamp_test(void)
{
	chip_reset();
	nak_pse_writes = true;
	poe_bringup();
	nak_pse_writes = false;
	CHECK(led[0x01] == 0x40);
}

static void test_poll_backs_off_without_a_chip(void)
{
	int n = 0;

	chip_reset();
	regs[HS104_REG_DEVID] = 0x55;		// bring-up finds no HS104
	poe_bringup();
	reads = 0;
	for (int i = 0; i < 30; i++)
		poe_poll();
	n = reads;
	CHECK(n <= 2);				// at most a retry per 30 s
}

static void test_type_write_keeps_budget_on_other_ports(void)
{
	chip_reset();
	regs[HS104_REG_PROTOCOL] = 0x00;	// a bad read: BT everywhere
	CHECK(poe_port_type_set(0, POE_TYPE_AT) == 0);
	CHECK(last_wval == 0xaa);		// ports 2-4 forced back to AT
}

static void test_type_read_failure_writes_nothing(void)
{
	chip_reset();
	nak = true;
	CHECK(poe_port_type_set(0, POE_TYPE_AT) != 0);
	CHECK(writes == 0);
}

static void test_voltage_does_not_wrap(void)
{
	chip_reset();
	regs[HS104_REG_PW_EN] = 0x48;
	regs[HS104_REG_PW_STATUS] = 0x40;
	regs[HS104_REG_INPUT_V] = 0x3f;		// 0x3fff = 163.83 V, a bad read
	regs[HS104_REG_INPUT_V + 1] = 0xff;
	poe_port_num = 0;
	poe_get_port();
	CHECK(poe_st_mv == 0xffff);
}

int main(int argc, char **argv)
{
	verbose = argc > 1;
	test_bringup_sets_types_then_enables_all_ports();
	test_bringup_lamp_test();
	test_bringup_failed_write_not_reported_as_enabled();
	test_bringup_wrong_id_writes_nothing();
	test_bringup_nak_writes_nothing();
	test_port_off_clears_its_bit_only();
	test_port_set_masks_upper_readback_bits();
	test_port_on_sets_bit_with_execute();
	test_port_out_of_range_ignored();
	test_port_set_nak_writes_nothing();
	test_global();
	test_type_set_rewrites_only_its_field();
	test_type_bt_refused_beyond_bt_ports();
	test_restart_pulses_its_bit();
	test_status_decode();
	test_status_uses_single_byte_reads();
	test_status_rejects_bad_status_marker();
	test_status_nak_reports_unknown();
	test_poll_drives_leds_from_powered_ports();
	test_poll_clears_leds_when_pse_goes_silent();
	test_failed_bringup_clears_lamp_test();
	test_poll_backs_off_without_a_chip();
	test_type_write_keeps_budget_on_other_ports();
	test_type_read_failure_writes_nothing();
	test_voltage_does_not_wrap();
	if (fails) {
		printf("test_poe_hs104: %d of %d checks FAILED\n", fails, checks);
		return 1;
	}
	printf("test_poe_hs104: ALL PASSED (%d checks)\n", checks);
	return 0;
}
