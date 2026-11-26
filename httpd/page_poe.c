/*
 * /poe.json: {"chip","bt_ports","ports":[{"port","admin","on","type","class","mv","ma"}]}
 * with type POE_TYPE_* and class 255 = none; {"error":"no answer","ports":[]}
 * when the controller is silent. The poe command changes it, e.g. POST /cmd
 * "poe port 3 off".
 */
#pragma codeseg BANK4
#pragma constseg BANK4

#include <stdint.h>
#include "rtl837x_common.h"
#include "page_impl.h"
#include "poe.h"

extern __xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
extern __xdata uint16_t slen;

#ifdef POE_PRESENT
void send_poe(void) __banked
{
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	str_code_to_html("{" JSONQUOTE("chip") ":\"" POE_CHIP_NAME "\"," JSONQUOTE("bt_ports") ":");
	itoa_html(poe_cfg.bt_ports);
	poe_port_num = 0;
	poe_get_port();
	if (!poe_st_ok) {
		str_code_to_html("," JSONQUOTE("error") ":\"no answer\"," JSONQUOTE("ports") ":[]}");
		return;
	}
	str_code_to_html("," JSONQUOTE("ports") ":[");
	for (poe_port_num = 0; poe_port_num < POE_MAX_PORTS; poe_port_num++) {
		poe_get_port();
		if (!poe_st_ok)
			break;
		if (poe_port_num)
			char_to_html(',');
		str_code_to_html("{" JSONQUOTE("port") ":");
		itoa_html(poe_port_num + 1);
		str_code_to_html("," JSONQUOTE("admin") ":");
		itoa_html(poe_st_admin);
		str_code_to_html("," JSONQUOTE("on") ":");
		itoa_html(poe_st_on);
		str_code_to_html("," JSONQUOTE("type") ":");
		itoa_html(poe_st_type);
		str_code_to_html("," JSONQUOTE("class") ":");
		itoa_html(poe_st_class);
		str_code_to_html("," JSONQUOTE("mv") ":");
		itoa16_html(poe_st_mv);
		str_code_to_html("," JSONQUOTE("ma") ":");
		itoa16_html(poe_st_ma);
		char_to_html('}');
	}
	str_code_to_html("]}");
}
#endif
