#define MACHINE_CHIPSET RTL8372
#define MACHINE_POE_CHIP HS104
#include "S600W_4GT_2XGT_SE.c"

// HS104PTI on the HS-4Port-2021 V1.01 daughterboard
__code const struct poe_cfg poe_cfg = {
	.i2c_bus = I2CBUS(GPIO47_I2C_SDA0, GPIO46_I2C_SCL0),
	.i2c_dev_addr_base = 0x0d,
	.led_addr = 0x4d,	// PoE LED latch on the main board
	.bt_ports = 1,		// factory setting of the 65 W supply: BT on port 1 only
};
