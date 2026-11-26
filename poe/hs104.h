#ifndef _HS104_H_
#define _HS104_H_

/* Hasivo HS104PTI/HS104PBI: 4-channel PSE with an 8-bit register map, 16-bit
 * values big-endian. Bit-field registers have port n at bit (3 - n) and a
 * fixed 0x4 in the upper nibble. */
#define POE_CHIP_NAME		"HS104"
#define POE_MAX_PORTS		4
#define HS104_PORTS		4
#define HS104_PORT_BIT(n)	(0x08 >> (n))

#define HS104_REG_PW_STATUS	0x01	// bit per port: delivering power
#define HS104_REG_INPUT_V	0x02	// supply voltage, 10 mV
#define HS104_REG_PORT0_I	0x04	// port current, mA, port n at +2n
#define HS104_REG_DEVID		0x0c
#define HS104_DEVICE_ID		0x91
#define HS104_REG_PORT0_CLASS	0x0d	// PD class, port n at +n
#define HS104_REG_PW_EN		0x14	// bit per port: enabled
#define HS104_REG_PROTOCOL	0x19	// 2 bits per port: bt, Hi-PoE, at, af
#define HS104_REG_TOTAL_POWER	0x1d	// 10 mW
#define HS104_REG_RESTART	0x1f	// bit per port: power-cycle once
#define HS104_REG_PORT0_POWER	0x21	// 10 mW, port n at +2n

#define HS104_EXECUTE		0x40	// must be set in every PW_EN/RESTART write

#endif
