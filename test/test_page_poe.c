/*
 * Host test for httpd/page_poe.c: /poe.json built from a fake driver.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rtl837x_common.h"
#include "page_impl.h"
#include "poe.h"

uint8_t outbuf[TCP_OUTBUF_SIZE];
uint16_t slen;

const struct poe_cfg poe_cfg = { .i2c_dev_addr_base = 0x0d, .bt_ports = 1 };

uint8_t poe_port_num, poe_st_ok, poe_st_admin, poe_st_on, poe_st_class, poe_st_volt, poe_st_type;
uint16_t poe_st_ma, poe_st_mv;

static bool silent;


uint16_t strtox(uint8_t *dst, const char *s)
{
	uint16_t n = strlen(s);

	memcpy(dst, s, n);
	return n;
}

void poe_get_port(void)
{
	poe_st_ok = !silent && poe_port_num < 4;
	poe_st_admin = poe_st_ok && poe_port_num != 2;
	poe_st_on = poe_st_ok && poe_port_num == 0;
	poe_st_class = poe_st_on ? 4 : 0xff;
	poe_st_mv = poe_st_ok ? 52190 : 0;
	poe_st_ma = poe_st_on ? 300 : 0;
	poe_st_type = poe_st_ok ? (poe_port_num == 0 ? POE_TYPE_BT : POE_TYPE_AT) : 0xff;
}

static const char *body(void)
{
	outbuf[slen] = 0;
	const char *p = strstr((const char *)outbuf, "\r\n\r\n");
	return p ? p + 4 : (const char *)outbuf;
}

static int fails, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
	printf("FAIL %s:%d: %s\n  body: %s\n", __FILE__, __LINE__, #cond, body()); } } while (0)

static void test_ports_listed(void)
{
	silent = false;
	send_poe();
	CHECK(strstr((char *)outbuf, "application/json") != NULL);
	CHECK(strcmp(body(), "{\"chip\":\"HS104\",\"bt_ports\":1,\"ports\":["
		"{\"port\":1,\"admin\":1,\"on\":1,\"type\":2,\"class\":4,\"mv\":52190,\"ma\":300},"
		"{\"port\":2,\"admin\":1,\"on\":0,\"type\":1,\"class\":255,\"mv\":52190,\"ma\":0},"
		"{\"port\":3,\"admin\":0,\"on\":0,\"type\":1,\"class\":255,\"mv\":52190,\"ma\":0},"
		"{\"port\":4,\"admin\":1,\"on\":0,\"type\":1,\"class\":255,\"mv\":52190,\"ma\":0}]}") == 0);
}

static void test_silent_controller_reported(void)
{
	silent = true;
	send_poe();
	CHECK(strcmp(body(), "{\"chip\":\"HS104\",\"bt_ports\":1,\"error\":\"no answer\",\"ports\":[]}") == 0);
}

int main(void)
{
	test_ports_listed();
	test_silent_controller_reported();
	if (fails) {
		printf("test_page_poe: %d of %d checks FAILED\n", fails, checks);
		return 1;
	}
	printf("test_page_poe: ALL PASSED (%d checks)\n", checks);
	return 0;
}
