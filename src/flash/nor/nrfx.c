/***************************************************************************
 *   Copyright (C) 2013 Synapse Product Development                        *
 *   Andrey Smirnov <andrew.smironv@gmail.com>                             *
 *   Angus Gratton <gus@projectgus.com>                                    *
 *   Erdem U. Altunyurt <spamjunkeater@gmail.com>                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <target/algorithm.h>
#include <target/armv7m.h>
#include <helper/types.h>
#include <helper/time_support.h>

enum {
	NRFX_FLASH_BASE = 0x00000000,
};

/* Factory Information Configuration Registers */
#define NRF5_FICR_BASE 0x10000000
#define NRF9_FICR_BASE 0x00ff0000

#define NRF5_FICR_REG(offset) (NRF5_FICR_BASE + offset)
#define NRF9_FICR_REG(offset) (NRF9_FICR_BASE + offset)

#define NRFX_UNIMPLEMENTED 0xffffffff

/*
 * The following nrfx_<unit>_registers enums contain "virtual"
 * registers definitions: they just link each register's name to an integer
 * index. The index is made of a 12bits progressive number ored with
 * another number which represents the hw unit the register itself belongs to.
 */
#define REG_INDEX_BITS 12

#define REG_INDEX_MASK ((1 << REG_INDEX_BITS) - 1)

#define FICR_ID (0 << REG_INDEX_BITS)
#define UICR_ID (1 << REG_INDEX_BITS)
#define NVMC_ID (2 << REG_INDEX_BITS)

#define FICR_BASE FICR_ID
#define UICR_BASE UICR_ID
#define NVMC_BASE NVMC_ID

#define reg_index(r) ((r) & REG_INDEX_MASK)

static inline int is_ficr(int n)
{
	return ((n & ~REG_INDEX_MASK) == FICR_ID);
}

static inline int is_uicr(int n)
{
	return ((n & ~REG_INDEX_MASK) == UICR_ID);
}

static inline int is_nvmc(int n)
{
	return ((n & ~REG_INDEX_MASK) == NVMC_ID);
}

enum nrfx_ficr_registers {
	NRFX_FICR_CODEPAGESIZE = FICR_BASE,
	NRFX_FICR_CODESIZE,
	NRFX_FICR_CLENR0,
	NRFX_FICR_PPFC,
	NRFX_FICR_NUMRAMBLOCK,
	NRFX_FICR_SIZERAMBLOCK0,
	NRFX_FICR_SIZERAMBLOCK1,
	NRFX_FICR_SIZERAMBLOCK2,
	NRFX_FICR_SIZERAMBLOCK3,
	NRFX_FICR_CONFIGID,
	NRFX_FICR_DEVICEID0,
	NRFX_FICR_DEVICEID1,
	NRFX_FICR_ER0,
	NRFX_FICR_ER1,
	NRFX_FICR_ER2,
	NRFX_FICR_ER3,
	NRFX_FICR_IR0,
	NRFX_FICR_IR1,
	NRFX_FICR_IR2,
	NRFX_FICR_IR3,
	NRFX_FICR_DEVICEADDRTYPE,
	NRFX_FICR_DEVICEADDR0,
	NRFX_FICR_DEVICEADDR1,
	NRFX_FICR_OVERRIDEN,
	NRFX_FICR_NRF_1MBIT0,
	NRFX_FICR_NRF_1MBIT1,
	NRFX_FICR_NRF_1MBIT2,
	NRFX_FICR_NRF_1MBIT3,
	NRFX_FICR_NRF_1MBIT4,
	NRFX_FICR_BLE_1MBIT0,
	NRFX_FICR_BLE_1MBIT1,
	NRFX_FICR_BLE_1MBIT2,
	NRFX_FICR_BLE_1MBIT3,
	NRFX_FICR_BLE_1MBIT4,
	NRFX_FICR_PART,
	NRFX_FICR_VARIANT,
	NRFX_FICR_PACKAGE,
	NRFX_FICR_RAM,
	NRFX_FICR_FLASH,
	NRFX_FICR_NREGS = ((NRFX_FICR_FLASH + 1) & REG_INDEX_MASK),
};

static const uint32_t nrf51_ficr_registers[] = {
	[reg_index(NRFX_FICR_CODEPAGESIZE)]	= NRF5_FICR_REG(0x010),
	[reg_index(NRFX_FICR_CODESIZE)]		= NRF5_FICR_REG(0x014),
	[reg_index(NRFX_FICR_CLENR0)]		= NRF5_FICR_REG(0x028),
	[reg_index(NRFX_FICR_PPFC)]		= NRF5_FICR_REG(0x02C),
	[reg_index(NRFX_FICR_NUMRAMBLOCK)]	= NRF5_FICR_REG(0x034),
	[reg_index(NRFX_FICR_SIZERAMBLOCK0)]	= NRF5_FICR_REG(0x038),
	[reg_index(NRFX_FICR_SIZERAMBLOCK1)]	= NRF5_FICR_REG(0x03C),
	[reg_index(NRFX_FICR_SIZERAMBLOCK2)]	= NRF5_FICR_REG(0x040),
	[reg_index(NRFX_FICR_SIZERAMBLOCK3)]	= NRF5_FICR_REG(0x044),
	[reg_index(NRFX_FICR_CONFIGID)]		= NRF5_FICR_REG(0x05C),
	[reg_index(NRFX_FICR_DEVICEID0)]	= NRF5_FICR_REG(0x060),
	[reg_index(NRFX_FICR_DEVICEID1)]	= NRF5_FICR_REG(0x064),
	[reg_index(NRFX_FICR_ER0)]		= NRF5_FICR_REG(0x080),
	[reg_index(NRFX_FICR_ER1)]		= NRF5_FICR_REG(0x084),
	[reg_index(NRFX_FICR_ER2)]		= NRF5_FICR_REG(0x088),
	[reg_index(NRFX_FICR_ER3)]		= NRF5_FICR_REG(0x08C),
	[reg_index(NRFX_FICR_IR0)]		= NRF5_FICR_REG(0x090),
	[reg_index(NRFX_FICR_IR1)]		= NRF5_FICR_REG(0x094),
	[reg_index(NRFX_FICR_IR2)]		= NRF5_FICR_REG(0x098),
	[reg_index(NRFX_FICR_IR3)]		= NRF5_FICR_REG(0x09C),
	[reg_index(NRFX_FICR_DEVICEADDRTYPE)]	= NRF5_FICR_REG(0x0A0),
	[reg_index(NRFX_FICR_DEVICEADDR0)]	= NRF5_FICR_REG(0x0A4),
	[reg_index(NRFX_FICR_DEVICEADDR1)]	= NRF5_FICR_REG(0x0A8),
	[reg_index(NRFX_FICR_OVERRIDEN)]	= NRF5_FICR_REG(0x0AC),
	[reg_index(NRFX_FICR_NRF_1MBIT0)]	= NRF5_FICR_REG(0x0B0),
	[reg_index(NRFX_FICR_NRF_1MBIT1)]	= NRF5_FICR_REG(0x0B4),
	[reg_index(NRFX_FICR_NRF_1MBIT2)]	= NRF5_FICR_REG(0x0B8),
	[reg_index(NRFX_FICR_NRF_1MBIT3)]	= NRF5_FICR_REG(0x0BC),
	[reg_index(NRFX_FICR_NRF_1MBIT4)]	= NRF5_FICR_REG(0x0C0),
	[reg_index(NRFX_FICR_BLE_1MBIT0)]	= NRF5_FICR_REG(0x0EC),
	[reg_index(NRFX_FICR_BLE_1MBIT1)]	= NRF5_FICR_REG(0x0F0),
	[reg_index(NRFX_FICR_BLE_1MBIT2)]	= NRF5_FICR_REG(0x0F4),
	[reg_index(NRFX_FICR_BLE_1MBIT3)]	= NRF5_FICR_REG(0x0F8),
	[reg_index(NRFX_FICR_BLE_1MBIT4)]	= NRF5_FICR_REG(0x0FC),
	[reg_index(NRFX_FICR_PART)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_VARIANT)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_PACKAGE)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_RAM)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_FLASH)]		= NRFX_UNIMPLEMENTED,
};

static const uint32_t nrf52_ficr_registers[] = {
	[reg_index(NRFX_FICR_CODEPAGESIZE)]	= NRF5_FICR_REG(0x010),
	[reg_index(NRFX_FICR_CODESIZE)]		= NRF5_FICR_REG(0x014),
	[reg_index(NRFX_FICR_CLENR0)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_PPFC)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NUMRAMBLOCK)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK2)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK3)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_CONFIGID)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_DEVICEID0)]	= NRF5_FICR_REG(0x060),
	[reg_index(NRFX_FICR_DEVICEID1)]	= NRF5_FICR_REG(0x064),
	[reg_index(NRFX_FICR_ER0)]		= NRF5_FICR_REG(0x080),
	[reg_index(NRFX_FICR_ER1)]		= NRF5_FICR_REG(0x084),
	[reg_index(NRFX_FICR_ER2)]		= NRF5_FICR_REG(0x088),
	[reg_index(NRFX_FICR_ER3)]		= NRF5_FICR_REG(0x08C),
	[reg_index(NRFX_FICR_IR0)]		= NRF5_FICR_REG(0x090),
	[reg_index(NRFX_FICR_IR1)]		= NRF5_FICR_REG(0x094),
	[reg_index(NRFX_FICR_IR2)]		= NRF5_FICR_REG(0x098),
	[reg_index(NRFX_FICR_IR3)]		= NRF5_FICR_REG(0x09C),
	[reg_index(NRFX_FICR_DEVICEADDRTYPE)]	= NRF5_FICR_REG(0x0A0),
	[reg_index(NRFX_FICR_DEVICEADDR0)]	= NRF5_FICR_REG(0x0A4),
	[reg_index(NRFX_FICR_DEVICEADDR1)]	= NRF5_FICR_REG(0x0A8),
	[reg_index(NRFX_FICR_OVERRIDEN)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT2)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT3)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT4)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT2)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT3)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT4)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_PART)]		= NRF5_FICR_REG(0x100),
	[reg_index(NRFX_FICR_VARIANT)]		= NRF5_FICR_REG(0x104),
	[reg_index(NRFX_FICR_PACKAGE)]		= NRF5_FICR_REG(0x108),
	[reg_index(NRFX_FICR_RAM)]		= NRF5_FICR_REG(0x10C),
	[reg_index(NRFX_FICR_FLASH)]		= NRF5_FICR_REG(0x110),
};

static const uint32_t nrf91_ficr_registers[] = {
	[reg_index(NRFX_FICR_CODEPAGESIZE)]	= NRF9_FICR_REG(0x220),
	[reg_index(NRFX_FICR_CODESIZE)]		= NRF9_FICR_REG(0x224),
	[reg_index(NRFX_FICR_CLENR0)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_PPFC)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NUMRAMBLOCK)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK2)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_SIZERAMBLOCK3)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_CONFIGID)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_DEVICEID0)]	= NRF9_FICR_REG(0x204),
	[reg_index(NRFX_FICR_DEVICEID1)]	= NRF9_FICR_REG(0x208),
	[reg_index(NRFX_FICR_ER0)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_ER1)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_ER2)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_ER3)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_IR0)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_IR1)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_IR2)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_IR3)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_DEVICEADDRTYPE)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_DEVICEADDR0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_DEVICEADDR1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_OVERRIDEN)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT2)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT3)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_NRF_1MBIT4)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT2)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT3)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_BLE_1MBIT4)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_FICR_PART)]		= NRF9_FICR_REG(0x20c),
	[reg_index(NRFX_FICR_VARIANT)]		= NRF9_FICR_REG(0x210),
	[reg_index(NRFX_FICR_PACKAGE)]		= NRF9_FICR_REG(0x214),
	[reg_index(NRFX_FICR_RAM)]		= NRF9_FICR_REG(0x218),
	[reg_index(NRFX_FICR_FLASH)]		= NRF9_FICR_REG(0x21c),
};

/* User Information Configuration Regsters */
#define NRF5_UICR_BASE 0x10001000
#define NRF9_UICR_BASE 0x00ff8000

#define NRF5_UICR_REG(offset) (NRF5_UICR_BASE + offset)
#define NRF9_UICR_REG(offset) (NRF9_UICR_BASE + offset)
#define NRFX_UICR_SIZE 0x1000

static inline uint32_t nrfx_uicr_base(int family)
{
	switch(family) {
	case 51:
	case 52:
		return NRF5_UICR_BASE;
	case 91:
		return NRF9_UICR_BASE;
	default:
		return NRFX_UNIMPLEMENTED;
	}
	return NRFX_UNIMPLEMENTED;
}

enum nrfx_uicr_registers {
	NRFX_UICR_CLENR0 = UICR_BASE,
	NRFX_UICR_RBPCONF,
	NRFX_UICR_XTALFREQ,
	NRFX_UICR_FWID,
	NRFX_UICR_PSELRESET0,
	NRFX_UICR_PSELRESET1,
	NRFX_UICR_APPROTECT,
	NRFX_UICR_NFCPINS,
	NRFX_UICR_SECUREAPPROTECT,
	NRFX_UICR_ERASEPROTECT,
	NRFX_UICR_NREGS = ((NRFX_UICR_ERASEPROTECT + 1) & REG_INDEX_MASK),
};

static const uint32_t nrf51_uicr_registers[] = {
	[reg_index(NRFX_UICR_CLENR0)]	= NRF5_UICR_REG(0x000),
	[reg_index(NRFX_UICR_RBPCONF)]	= NRF5_UICR_REG(0x004),
	[reg_index(NRFX_UICR_XTALFREQ)]	= NRF5_UICR_REG(0x008),
	[reg_index(NRFX_UICR_FWID)]	= NRF5_UICR_REG(0x010),
	[reg_index(NRFX_UICR_PSELRESET0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_PSELRESET1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_APPROTECT)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_NFCPINS)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_SECUREAPPROTECT)] = NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_ERASEPROTECT)] = NRFX_UNIMPLEMENTED,
};

static const uint32_t nrf52_uicr_registers[] = {
	[reg_index(NRFX_UICR_CLENR0)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_RBPCONF)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_XTALFREQ)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_FWID)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_PSELRESET0)]	= NRF5_UICR_REG(0x200),
	[reg_index(NRFX_UICR_PSELRESET1)]	= NRF5_UICR_REG(0x204),
	[reg_index(NRFX_UICR_APPROTECT)]	= NRF5_UICR_REG(0x208),
	[reg_index(NRFX_UICR_NFCPINS)]		= NRF5_UICR_REG(0x20C),
	[reg_index(NRFX_UICR_SECUREAPPROTECT)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_ERASEPROTECT)]	= NRFX_UNIMPLEMENTED,
};

static const uint32_t nrf91_uicr_registers[] = {
	[reg_index(NRFX_UICR_CLENR0)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_RBPCONF)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_XTALFREQ)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_FWID)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_PSELRESET0)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_PSELRESET1)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_APPROTECT)]	= NRF9_UICR_REG(0x000),
	[reg_index(NRFX_UICR_NFCPINS)]		= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_UICR_SECUREAPPROTECT)]	= NRF9_UICR_REG(0x02C),
	[reg_index(NRFX_UICR_ERASEPROTECT)]	= NRF9_UICR_REG(0x030),
};


/* Non-Volatile Memory Controller Registers */
#define NRF5_NVMC_BASE 0x4001E000
#define NRF9_NVMC_BASE 0x50039000

#define NRF5_NVMC_REG(offset) (NRF5_NVMC_BASE + offset)
#define NRF9_NVMC_REG(offset) (NRF9_NVMC_BASE + offset)

enum nrfx_nvmc_registers {
	NRFX_NVMC_READY = NVMC_BASE,
	NRFX_NVMC_CONFIG,
	NRFX_NVMC_ERASEPAGE,
	NRFX_NVMC_ERASEALL,
	NRFX_NVMC_ERASEUICR,
};

enum nrfx_nvmc_config_bits {
	NRFX_NVMC_CONFIG_REN = 0x00,
	NRFX_NVMC_CONFIG_WEN = 0x01,
	NRFX_NVMC_CONFIG_EEN = 0x02,
};

static const uint32_t nrf5_nvmc_registers[] = {
	[reg_index(NRFX_NVMC_READY)]		= NRF5_NVMC_REG(0x400),
	[reg_index(NRFX_NVMC_CONFIG)]		= NRF5_NVMC_REG(0x504),
	[reg_index(NRFX_NVMC_ERASEPAGE)]	= NRF5_NVMC_REG(0x508),
	[reg_index(NRFX_NVMC_ERASEALL)]		= NRF5_NVMC_REG(0x50C),
	[reg_index(NRFX_NVMC_ERASEUICR)]	= NRF5_NVMC_REG(0x514),
};

static const uint32_t nrf91_nvmc_registers[] = {
	[reg_index(NRFX_NVMC_READY)]		= NRF9_NVMC_REG(0x400),
	[reg_index(NRFX_NVMC_CONFIG)]		= NRF9_NVMC_REG(0x504),
	[reg_index(NRFX_NVMC_ERASEPAGE)]	= NRFX_UNIMPLEMENTED,
	[reg_index(NRFX_NVMC_ERASEALL)]		= NRF9_NVMC_REG(0x50C),
	[reg_index(NRFX_NVMC_ERASEUICR)]	= NRF9_NVMC_REG(0x514),
};

struct nrfx_info {
	uint32_t code_page_size;
	uint32_t refcount;

	struct {
		bool probed;
		int (*write) (struct flash_bank *bank,
			      struct nrfx_info *chip,
			      const uint8_t *buffer, uint32_t offset, uint32_t count);
	} bank[2];
	int family;
	const uint32_t *ficr_registers;
	const uint32_t *uicr_registers;
	const uint32_t *nvmc_registers;
	struct target *target;
};

static inline int reg_read(struct nrfx_info *chip, uint32_t addr, uint32_t *out)
{
	if (addr == NRFX_UNIMPLEMENTED)
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	return target_read_u32(chip->target, addr, out);
}

static inline int reg_write(struct nrfx_info *chip, uint32_t addr, uint32_t in)
{
	if (addr == NRFX_UNIMPLEMENTED)
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	return target_write_u32(chip->target, addr, in);
}

static inline int ficr_read(struct nrfx_info *chip, enum nrfx_ficr_registers r,
			    uint32_t *out)
{
	return reg_read(chip, chip->ficr_registers[r & REG_INDEX_MASK], out);
}

static inline int ficr_write(struct nrfx_info *chip, enum nrfx_ficr_registers r,
			     uint32_t in)
{
	return reg_write(chip, chip->ficr_registers[r & REG_INDEX_MASK], in);
}

static inline int uicr_read(struct nrfx_info *chip, enum nrfx_uicr_registers r,
			    uint32_t *out)
{
	return reg_read(chip, chip->uicr_registers[r & REG_INDEX_MASK], out);
}

static inline int uicr_write(struct nrfx_info *chip, enum nrfx_ficr_registers r,
			     uint32_t in)
{
	return reg_write(chip, chip->uicr_registers[r & REG_INDEX_MASK], in);
}

static inline int nvmc_read(struct nrfx_info *chip, enum nrfx_uicr_registers r,
			    uint32_t *out)
{
	return reg_read(chip, chip->nvmc_registers[r & REG_INDEX_MASK], out);
}

static inline int nvmc_write(struct nrfx_info *chip, enum nrfx_ficr_registers r,
			     uint32_t in)
{
	return reg_write(chip, chip->nvmc_registers[r & REG_INDEX_MASK], in);
}

static inline int ficr_is_implemented(struct nrfx_info *chip,
				      enum nrfx_ficr_registers r)
{
	return chip->ficr_registers[r & REG_INDEX_MASK] != NRFX_UNIMPLEMENTED;
}

static inline int uicr_is_implemented(struct nrfx_info *chip,
				      enum nrfx_uicr_registers r)
{
	return chip->uicr_registers[r & REG_INDEX_MASK] != NRFX_UNIMPLEMENTED;
}

static inline int nvmc_is_implemented(struct nrfx_info *chip,
				      enum nrfx_nvmc_registers r)
{
	return chip->nvmc_registers[r & REG_INDEX_MASK] != NRFX_UNIMPLEMENTED;
}

static inline int is_implemented(struct nrfx_info *chip, int r)
{
	if (is_ficr(r))
		return ficr_is_implemented(chip, r);
	if (is_uicr(r))
		return uicr_is_implemented(chip, r);
	if (is_nvmc(r))
		return nvmc_is_implemented(chip, r);
	return 0;
}

union nrfx_device_id {
	uint16_t hwid;
	uint32_t part;
};

struct nrfx_device_data {
	const char *part;
	const char *variant;
	const char *build_code;
	unsigned int flash_size_kb;
};

struct nrfx_device_spec {
	int have_hwid;
	union {
		uint16_t hwid;
		uint32_t part;
	} id;
	const char *part;
	const char *variant;
	const char *build_code;
	unsigned int flash_size_kb;
};

#define NRF51_DEVICE_DEF(_id, pt, var, bcode, fsize)	\
{							\
	.have_hwid = 1,					\
	.id = {						\
		.hwid = (_id),				\
	},						\
	.part		= pt,				\
	.variant	= var,				\
	.build_code	= bcode,			\
	.flash_size_kb	= (fsize),			\
}

#define NRF52_DEVICE_DEF(pt)				\
{							\
	.have_hwid = 0,					\
	.id = {						\
		.part = pt,				\
	},						\
}

/* The known devices table below is derived from the "nRF51 Series
 * Compatibility Matrix" document, which can be found by searching for
 * ATTN-51 on the Nordic Semi website:
 *
 * http://www.nordicsemi.com/eng/content/search?SearchText=ATTN-51
 *
 * Up to date with Matrix v2.0, plus some additional HWIDs.
 *
 * The additional HWIDs apply where the build code in the matrix is
 * shown as Gx0, Bx0, etc. In these cases the HWID in the matrix is
 * for x==0, x!=0 means different (unspecified) HWIDs.
 */
static const struct nrfx_device_spec nrfx_known_devices_table[] = {
	/* nRF51822 Devices (IC rev 1). */
	NRF51_DEVICE_DEF(0x001D, "51822", "QFAA", "CA/C0", 256),
	NRF51_DEVICE_DEF(0x0026, "51822", "QFAB", "AA",    128),
	NRF51_DEVICE_DEF(0x0027, "51822", "QFAB", "A0",    128),
	NRF51_DEVICE_DEF(0x0020, "51822", "CEAA", "BA",    256),
	NRF51_DEVICE_DEF(0x002F, "51822", "CEAA", "B0",    256),

	/* Some early nRF51-DK (PCA10028) & nRF51-Dongle (PCA10031) boards
	   with built-in jlink seem to use engineering samples not listed
	   in the nRF51 Series Compatibility Matrix V1.0. */
	NRF51_DEVICE_DEF(0x0071, "51822", "QFAC", "AB",    256),

	/* nRF51822 Devices (IC rev 2). */
	NRF51_DEVICE_DEF(0x002A, "51822", "QFAA", "FA0",   256),
	NRF51_DEVICE_DEF(0x0044, "51822", "QFAA", "GC0",   256),
	NRF51_DEVICE_DEF(0x003C, "51822", "QFAA", "G0",    256),
	NRF51_DEVICE_DEF(0x0057, "51822", "QFAA", "G2",    256),
	NRF51_DEVICE_DEF(0x0058, "51822", "QFAA", "G3",    256),
	NRF51_DEVICE_DEF(0x004C, "51822", "QFAB", "B0",    128),
	NRF51_DEVICE_DEF(0x0040, "51822", "CEAA", "CA0",   256),
	NRF51_DEVICE_DEF(0x0047, "51822", "CEAA", "DA0",   256),
	NRF51_DEVICE_DEF(0x004D, "51822", "CEAA", "D00",   256),

	/* nRF51822 Devices (IC rev 3). */
	NRF51_DEVICE_DEF(0x0072, "51822", "QFAA", "H0",    256),
	NRF51_DEVICE_DEF(0x00D1, "51822", "QFAA", "H2",    256),
	NRF51_DEVICE_DEF(0x007B, "51822", "QFAB", "C0",    128),
	NRF51_DEVICE_DEF(0x0083, "51822", "QFAC", "A0",    256),
	NRF51_DEVICE_DEF(0x0084, "51822", "QFAC", "A1",    256),
	NRF51_DEVICE_DEF(0x007D, "51822", "CDAB", "A0",    128),
	NRF51_DEVICE_DEF(0x0079, "51822", "CEAA", "E0",    256),
	NRF51_DEVICE_DEF(0x0087, "51822", "CFAC", "A0",    256),
	NRF51_DEVICE_DEF(0x008F, "51822", "QFAA", "H1",    256),
	/* nRF51422 Devices (IC rev 1). */
	NRF51_DEVICE_DEF(0x001E, "51422", "QFAA", "CA",    256),
	NRF51_DEVICE_DEF(0x0024, "51422", "QFAA", "C0",    256),
	NRF51_DEVICE_DEF(0x0031, "51422", "CEAA", "A0A",   256),

	/* nRF51422 Devices (IC rev 2). */
	NRF51_DEVICE_DEF(0x002D, "51422", "QFAA", "DAA",   256),
	NRF51_DEVICE_DEF(0x002E, "51422", "QFAA", "E0",    256),
	NRF51_DEVICE_DEF(0x0061, "51422", "QFAB", "A00",   128),
	NRF51_DEVICE_DEF(0x0050, "51422", "CEAA", "B0",    256),

	/* nRF51422 Devices (IC rev 3). */
	NRF51_DEVICE_DEF(0x0073, "51422", "QFAA", "F0",    256),
	NRF51_DEVICE_DEF(0x007C, "51422", "QFAB", "B0",    128),
	NRF51_DEVICE_DEF(0x0085, "51422", "QFAC", "A0",    256),
	NRF51_DEVICE_DEF(0x0086, "51422", "QFAC", "A1",    256),
	NRF51_DEVICE_DEF(0x007E, "51422", "CDAB", "A0",    128),
	NRF51_DEVICE_DEF(0x007A, "51422", "CEAA", "C0",    256),
	NRF51_DEVICE_DEF(0x0088, "51422", "CFAC", "A0",    256),

	/* nRF52810 Devices */
	NRF52_DEVICE_DEF(0x52810),

	/* nRF52832 Devices */
	NRF52_DEVICE_DEF(0x52832),

	/* nRF52840 Devices */
	NRF52_DEVICE_DEF(0x52840),
};

static int nrfx_bank_is_probed(struct flash_bank *bank)
{
	struct nrfx_info *chip = bank->driver_priv;

	assert(chip != NULL);

	return chip->bank[bank->bank_number].probed;
}
static int nrfx_probe(struct flash_bank *bank);

static int nrfx_get_probed_chip_if_halted(struct flash_bank *bank, struct nrfx_info **chip)
{
	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	*chip = bank->driver_priv;

	int probed = nrfx_bank_is_probed(bank);
	if (probed < 0)
		return probed;
	else if (!probed)
		return nrfx_probe(bank);
	else
		return ERROR_OK;
}

static int nrfx_wait_for_nvmc(struct nrfx_info *chip)
{
	uint32_t ready;
	int res;
	int timeout_ms = 340;
	int64_t ts_start = timeval_ms();

	do {
		res = nvmc_read(chip, NRFX_NVMC_READY, &ready);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read NVMC_READY register");
			return res;
		}

		if (ready == 0x00000001)
			return ERROR_OK;

		keep_alive();

	} while ((timeval_ms()-ts_start) < timeout_ms);

	LOG_DEBUG("Timed out waiting for NVMC_READY");
	return ERROR_FLASH_BUSY;
}

static int nrfx_nvmc_erase_enable(struct nrfx_info *chip)
{
	int res;
	res = nvmc_write(chip, NRFX_NVMC_CONFIG, NRFX_NVMC_CONFIG_EEN);

	if (res != ERROR_OK) {
		LOG_ERROR("Failed to enable erase operation");
		return res;
	}

	/*
	  According to NVMC examples in Nordic SDK busy status must be
	  checked after writing to NVMC_CONFIG
	 */
	res = nrfx_wait_for_nvmc(chip);
	if (res != ERROR_OK)
		LOG_ERROR("Erase enable did not complete");

	return res;
}

static int nrfx_nvmc_write_enable(struct nrfx_info *chip)
{
	int res;
	res = nvmc_write(chip, NRFX_NVMC_CONFIG, NRFX_NVMC_CONFIG_WEN);

	if (res != ERROR_OK) {
		LOG_ERROR("Failed to enable write operation");
		return res;
	}

	/*
	  According to NVMC examples in Nordic SDK busy status must be
	  checked after writing to NVMC_CONFIG
	 */
	res = nrfx_wait_for_nvmc(chip);
	if (res != ERROR_OK)
		LOG_ERROR("Write enable did not complete");

	return res;
}

static int nrfx_nvmc_read_only(struct nrfx_info *chip)
{
	int res;
	res = nvmc_write(chip, NRFX_NVMC_CONFIG, NRFX_NVMC_CONFIG_REN);

	if (res != ERROR_OK) {
		LOG_ERROR("Failed to enable read-only operation");
		return res;
	}
	/*
	  According to NVMC examples in Nordic SDK busy status must be
	  checked after writing to NVMC_CONFIG
	 */
	res = nrfx_wait_for_nvmc(chip);
	if (res != ERROR_OK)
		LOG_ERROR("Read only enable did not complete");

	return res;
}

static int nrfx_nvmc_generic_erase(struct nrfx_info *chip,
				   uint32_t erase_addr,
				   enum nrfx_nvmc_registers erase_register,
				   uint32_t erase_value)
{
	int res;

	res = nrfx_nvmc_erase_enable(chip);
	if (res != ERROR_OK)
		goto error;

	if (chip->family != 91) {
		res = nvmc_write(chip, erase_register, erase_value);
		if (res != ERROR_OK)
			goto set_read_only;
	} else {
		target_write_u32(chip->target, erase_addr, 0xffffffff);
		usleep(100000);
	}

	res = nrfx_wait_for_nvmc(chip);
	if (res != ERROR_OK)
		goto set_read_only;

	return nrfx_nvmc_read_only(chip);

set_read_only:
	nrfx_nvmc_read_only(chip);
error:
	LOG_ERROR("Failed to erase reg: 0x%08"PRIx32" val: 0x%08"PRIx32,
		  erase_register, erase_value);
	return ERROR_FAIL;
}

static int nrfx_protect_check(struct flash_bank *bank)
{
	int res;
	uint32_t clenr0;

	/* UICR cannot be write protected so just return early */
	if (bank->base == NRF5_UICR_BASE ||
	    bank->base == NRF9_UICR_BASE)
		return ERROR_OK;

	struct nrfx_info *chip = bank->driver_priv;

	assert(chip != NULL);

	res = ficr_read(chip, NRFX_FICR_CLENR0, &clenr0);

	if (res == ERROR_TARGET_RESOURCE_NOT_AVAILABLE) {
		/* CLENR0 not implemented */
		clenr0 = 0xFFFFFFFF;
		goto no_clenr0;
	}

	if (res != ERROR_OK) {
		LOG_ERROR("Couldn't read code region 0 size[FICR]");
		return res;
	}

	if (clenr0 == 0xFFFFFFFF) {
		res = uicr_read(chip, NRFX_UICR_CLENR0, &clenr0);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read code region 0 size[UICR]");
			return res;
		}
	}

no_clenr0:
	for (int i = 0; i < bank->num_sectors; i++)
		bank->sectors[i].is_protected =
			clenr0 != 0xFFFFFFFF && bank->sectors[i].offset < clenr0;

	return ERROR_OK;
}

static int nrfx_protect(struct flash_bank *bank, int set, int first, int last)
{
	int res;
	uint32_t clenr0, ppfc;
	struct nrfx_info *chip;

	/* UICR cannot be write protected so just bail out early */
	if (bank->base == NRF5_UICR_BASE)
		return ERROR_FAIL;

	res = nrfx_get_probed_chip_if_halted(bank, &chip);
	if (res != ERROR_OK)
		return res;

	if (first != 0) {
		LOG_ERROR("Code region 0 must start at the begining of the bank");
		return ERROR_FAIL;
	}

	res = ficr_read(chip, NRFX_FICR_PPFC, &ppfc);
	if (res != ERROR_OK) {
		LOG_ERROR("Couldn't read PPFC register");
		return res;
	}

	if ((ppfc & 0xFF) == 0x00) {
		LOG_ERROR("Code region 0 size was pre-programmed at the factory, can't change flash protection settings");
		return ERROR_FAIL;
	}

	res = uicr_read(chip, NRFX_UICR_CLENR0, &clenr0);
	if (res != ERROR_OK) {
		LOG_ERROR("Couldn't read code region 0 size[UICR]");
		return res;
	}

	if (clenr0 == 0xFFFFFFFF) {
		res = uicr_write(chip, NRFX_UICR_CLENR0, clenr0);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't write code region 0 size[UICR]");
			return res;
		}

	} else {
		LOG_ERROR("You need to perform chip erase before changing the protection settings");
	}

	nrfx_protect_check(bank);

	return ERROR_OK;
}

static void log_probed_chip(struct nrfx_info *chip,
			    const struct nrfx_device_spec *spec,
			    int have_hwid, uint32_t hwid, uint32_t part)
{
	if (!spec) {
		LOG_WARNING("Unknown device (%s 0x%08" PRIx32 ")",
			    have_hwid ? "HWID" : "PART NUMBER",
			    have_hwid ? hwid : part);
		return;
	}

	if (spec->have_hwid) {
		LOG_INFO("nRF%s-%s(build code: %s) %ukB Flash",
			 spec->part,
			 spec->variant,
			 spec->build_code,
			 spec->flash_size_kb);
		return;
	}
	/* No hwid, get data from ficr registers */
	LOG_INFO("device: nRF%" PRIx32, part);
}

static void check_probed_chip_size(struct nrfx_info *chip,
				   unsigned int bank_size,
				   const struct nrfx_device_spec *spec)
{
	uint32_t flash_size_kb = 0;

	if (spec && spec->flash_size_kb)
		flash_size_kb = spec->flash_size_kb;
	else {
		if (ficr_read(chip, NRFX_FICR_FLASH, &flash_size_kb) != ERROR_OK) {
			LOG_ERROR("Could not read chip's flash size");
			return;
		}
	}
	if ((bank_size >> 10) != flash_size_kb)
		LOG_WARNING("Chip's reported Flash capacity does not match expected one (%u != %u)", bank_size, flash_size_kb);
	return;
}

static int nrfx_probe(struct flash_bank *bank)
{
	uint32_t hwid = 0, part = 0;
	int res, have_hwid = 0, have_part = 0;
	struct nrfx_info *chip = bank->driver_priv;

	if (ficr_is_implemented(chip, NRFX_FICR_CONFIGID)) {
		res = ficr_read(chip, NRFX_FICR_CONFIGID, &hwid);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read CONFIGID register");
			return res;
		}
		have_hwid = 1;
		hwid &= 0xFFFF;	/* HWID is stored in the lower two
				 * bytes of the CONFIGID register */
	}
	if (ficr_is_implemented(chip, NRFX_FICR_PART)) {
		res = ficr_read(chip, NRFX_FICR_PART, &part);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read PART register");
			return res;
		}
		have_part = 1;
	}

	if (!have_hwid && !have_part) {
		LOG_ERROR("Neither CONFIGID nor PART implemented\n");
		return ERROR_FAIL;
	}

	const struct nrfx_device_spec *spec = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(nrfx_known_devices_table); i++) {
		spec = &nrfx_known_devices_table[i];

		if (spec->have_hwid && have_hwid && hwid ==
		    spec->id.hwid) {
			spec = &nrfx_known_devices_table[i];
			break;
		}
		if (!spec->have_hwid && have_part && part ==
		    spec->id.part) {
			spec = &nrfx_known_devices_table[i];
			break;
		}
	}

	if (!chip->bank[0].probed && !chip->bank[1].probed)
		log_probed_chip(chip, spec, have_hwid, hwid, part);

	if (bank->base == NRFX_FLASH_BASE) {
		/* The value stored in NRFX_FICR_CODEPAGESIZE is the number of bytes in one page of FLASH. */
		res = ficr_read(chip, NRFX_FICR_CODEPAGESIZE,
				&chip->code_page_size);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read code page size");
			return res;
		}

		/* Note the register name is misleading,
		 * NRF5_FICR_CODESIZE is the number of pages in flash memory, not the number of bytes! */
		uint32_t num_sectors;
		res = ficr_read(chip, NRFX_FICR_CODESIZE, &num_sectors);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read code memory size");
			return res;
		}

		bank->num_sectors = num_sectors;
		bank->size = num_sectors * chip->code_page_size;

		check_probed_chip_size(chip, bank->size, spec);

		bank->sectors = calloc(bank->num_sectors,
				       sizeof((bank->sectors)[0]));
		if (!bank->sectors)
			return ERROR_FLASH_BANK_NOT_PROBED;

		/* Fill out the sector information: all NRFX sectors are the same size and
		 * there is always a fixed number of them. */
		for (int i = 0; i < bank->num_sectors; i++) {
			bank->sectors[i].size = chip->code_page_size;
			bank->sectors[i].offset	= i * chip->code_page_size;

			/* mark as unknown */
			bank->sectors[i].is_erased = -1;
			bank->sectors[i].is_protected = -1;
		}

		nrfx_protect_check(bank);

		chip->bank[0].probed = true;
	} else {
		bank->size = NRFX_UICR_SIZE;
		bank->num_sectors = 1;
		bank->sectors = calloc(bank->num_sectors,
				       sizeof((bank->sectors)[0]));
		if (!bank->sectors)
			return ERROR_FLASH_BANK_NOT_PROBED;

		bank->sectors[0].size = bank->size;
		bank->sectors[0].offset	= 0;

		bank->sectors[0].is_erased = 0;
		bank->sectors[0].is_protected = 0;

		chip->bank[1].probed = true;
	}

	return ERROR_OK;
}

static int nrfx_auto_probe(struct flash_bank *bank)
{
	int probed = nrfx_bank_is_probed(bank);

	if (probed < 0)
		return probed;
	else if (probed)
		return ERROR_OK;
	else
		return nrfx_probe(bank);
}

static int nrfx_erase_all(struct nrfx_info *chip)
{
	LOG_DEBUG("Erasing all non-volatile memory");
	return nrfx_nvmc_generic_erase(chip,
				       0,
				       NRFX_NVMC_ERASEALL,
				       0x00000001);
}

static int nrfx_erase_page(struct flash_bank *bank,
			   struct nrfx_info *chip,
			   struct flash_sector *sector)
{
	int res;

	LOG_DEBUG("Erasing page at 0x%"PRIx32, sector->offset);
	if (sector->is_protected) {
		LOG_ERROR("Cannot erase protected sector at 0x%" PRIx32, sector->offset);
		return ERROR_FAIL;
	}

	if (bank->base == NRF5_UICR_BASE) {
		uint32_t ppfc;
		res = ficr_read(chip, NRFX_FICR_PPFC,  &ppfc);
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read PPFC register");
			return res;
		}

		if ((ppfc & 0xFF) == 0xFF) {
			/* We can't erase the UICR.  Double-check to
			   see if it's already erased before complaining. */
			default_flash_blank_check(bank);
			if (sector->is_erased == 1)
				return ERROR_OK;

			LOG_ERROR("The chip was not pre-programmed with SoftDevice stack and UICR cannot be erased separately. Please issue mass erase before trying to write to this region");
			return ERROR_FAIL;
		}

		res = nrfx_nvmc_generic_erase(chip,
					      0,
					      NRFX_NVMC_ERASEUICR,
					      0x00000001);


	} else {
		res = nrfx_nvmc_generic_erase(chip,
					      sector->offset,
					      NRFX_NVMC_ERASEPAGE,
					      sector->offset);
	}

	return res;
}

static const uint8_t nrfx_flash_write_code[] = {
	/* See contrib/loaders/flash/cortex-m0.S */
/* <wait_fifo>: */
	0x0d, 0x68,		/* ldr	r5,	[r1,	#0] */
	0x00, 0x2d,		/* cmp	r5,	#0 */
	0x0b, 0xd0,		/* beq.n	1e <exit> */
	0x4c, 0x68,		/* ldr	r4,	[r1,	#4] */
	0xac, 0x42,		/* cmp	r4,	r5 */
	0xf9, 0xd0,		/* beq.n	0 <wait_fifo> */
	0x20, 0xcc,		/* ldmia	r4!,	{r5} */
	0x20, 0xc3,		/* stmia	r3!,	{r5} */
	0x94, 0x42,		/* cmp	r4,	r2 */
	0x01, 0xd3,		/* bcc.n	18 <no_wrap> */
	0x0c, 0x46,		/* mov	r4,	r1 */
	0x08, 0x34,		/* adds	r4,	#8 */
/* <no_wrap>: */
	0x4c, 0x60,		/* str	r4, [r1,	#4] */
	0x04, 0x38,		/* subs	r0, #4 */
	0xf0, 0xd1,		/* bne.n	0 <wait_fifo> */
/* <exit>: */
	0x00, 0xbe		/* bkpt	0x0000 */
};


/* Start a low level flash write for the specified region */
static int nrfx_ll_flash_write(struct nrfx_info *chip, uint32_t offset, const uint8_t *buffer, uint32_t bytes)
{
	struct target *target = chip->target;
	uint32_t buffer_size = 8192;
	struct working_area *write_algorithm;
	struct working_area *source;
	uint32_t address = NRFX_FLASH_BASE + offset;
	struct reg_param reg_params[4];
	struct armv7m_algorithm armv7m_info;
	int retval = ERROR_OK;


	LOG_DEBUG("Writing buffer to flash offset=0x%"PRIx32" bytes=0x%"PRIx32, offset, bytes);
	assert(bytes % 4 == 0);

	/* allocate working area with flash programming code */
	if (target_alloc_working_area(target, sizeof(nrfx_flash_write_code),
			&write_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, falling back to slow memory writes");

		for (; bytes > 0; bytes -= 4) {
			retval = target_write_memory(chip->target, offset, 4, 1, buffer);
			if (retval != ERROR_OK)
				return retval;

			retval = nrfx_wait_for_nvmc(chip);
			if (retval != ERROR_OK)
				return retval;

			offset += 4;
			buffer += 4;
		}

		return ERROR_OK;
	}

	LOG_WARNING("using fast async flash loader. This is currently supported");
	LOG_WARNING("only with ST-Link and CMSIS-DAP. If you have issues, add");
	LOG_WARNING("\"set WORKAREASIZE 0\" before sourcing nrf51.cfg/nrf52.cfg to disable it");

	retval = target_write_buffer(target, write_algorithm->address,
				sizeof(nrfx_flash_write_code),
				nrfx_flash_write_code);
	if (retval != ERROR_OK)
		return retval;

	/* memory buffer */
	while (target_alloc_working_area(target, buffer_size, &source) != ERROR_OK) {
		buffer_size /= 2;
		buffer_size &= ~3UL; /* Make sure it's 4 byte aligned */
		if (buffer_size <= 256) {
			/* free working area, write algorithm already allocated */
			target_free_working_area(target, write_algorithm);

			LOG_WARNING("No large enough working area available, can't do block memory writes");
			return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
		}
	}

	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	init_reg_param(&reg_params[0], "r0", 32, PARAM_IN_OUT);	/* byte count */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT);	/* buffer start */
	init_reg_param(&reg_params[2], "r2", 32, PARAM_OUT);	/* buffer end */
	init_reg_param(&reg_params[3], "r3", 32, PARAM_IN_OUT);	/* target address */

	buf_set_u32(reg_params[0].value, 0, 32, bytes);
	buf_set_u32(reg_params[1].value, 0, 32, source->address);
	buf_set_u32(reg_params[2].value, 0, 32, source->address + source->size);
	buf_set_u32(reg_params[3].value, 0, 32, address);

	retval = target_run_flash_async_algorithm(target, buffer, bytes/4, 4,
			0, NULL,
			4, reg_params,
			source->address, source->size,
			write_algorithm->address, 0,
			&armv7m_info);

	target_free_working_area(target, source);
	target_free_working_area(target, write_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);

	return retval;
}

/* Check and erase flash sectors in specified range then start a low level page write.
   start/end must be sector aligned.
*/
static int nrfx_write_pages(struct flash_bank *bank, uint32_t start, uint32_t end, const uint8_t *buffer)
{
	int res = ERROR_FAIL;
	struct nrfx_info *chip = bank->driver_priv;

	assert(start % chip->code_page_size == 0);
	assert(end % chip->code_page_size == 0);

	res = nrfx_nvmc_write_enable(chip);
	if (res != ERROR_OK)
		goto error;

	res = nrfx_ll_flash_write(chip, start, buffer, (end - start));
	if (res != ERROR_OK)
		goto error;

	return nrfx_nvmc_read_only(chip);

error:
	nrfx_nvmc_read_only(chip);
	LOG_ERROR("Failed to write to nrf5 flash");
	return res;
}

static int nrfx_erase(struct flash_bank *bank, int first, int last)
{
	int res;
	struct nrfx_info *chip;

	res = nrfx_get_probed_chip_if_halted(bank, &chip);
	if (res != ERROR_OK)
		return res;

	/* For each sector to be erased */
	for (int s = first; s <= last && res == ERROR_OK; s++)
		res = nrfx_erase_page(bank, chip, &bank->sectors[s]);

	return res;
}

static int nrfx_code_flash_write(struct flash_bank *bank,
				 struct nrfx_info *chip,
				 const uint8_t *buffer, uint32_t offset, uint32_t count)
{

	int res;
	/* Need to perform reads to fill any gaps we need to preserve in the first page,
	   before the start of buffer, or in the last page, after the end of buffer */
	uint32_t first_page = offset/chip->code_page_size;
	uint32_t last_page = DIV_ROUND_UP(offset+count, chip->code_page_size);

	uint32_t first_page_offset = first_page * chip->code_page_size;
	uint32_t last_page_offset = last_page * chip->code_page_size;

	LOG_DEBUG("Padding write from 0x%08"PRIx32"-0x%08"PRIx32" as 0x%08"PRIx32"-0x%08"PRIx32,
		offset, offset+count, first_page_offset, last_page_offset);

	uint32_t page_cnt = last_page - first_page;
	uint8_t buffer_to_flash[page_cnt*chip->code_page_size];

	/* Fill in any space between start of first page and start of buffer */
	uint32_t pre = offset - first_page_offset;
	if (pre > 0) {
		res = target_read_memory(bank->target,
					first_page_offset,
					1,
					pre,
					buffer_to_flash);
		if (res != ERROR_OK)
			return res;
	}

	/* Fill in main contents of buffer */
	memcpy(buffer_to_flash+pre, buffer, count);

	/* Fill in any space between end of buffer and end of last page */
	uint32_t post = last_page_offset - (offset+count);
	if (post > 0) {
		/* Retrieve the full row contents from Flash */
		res = target_read_memory(bank->target,
					offset + count,
					1,
					post,
					buffer_to_flash+pre+count);
		if (res != ERROR_OK)
			return res;
	}

	return nrfx_write_pages(bank, first_page_offset, last_page_offset, buffer_to_flash);
}

static int nrfx_uicr_flash_write(struct flash_bank *bank,
				  struct nrfx_info *chip,
				  const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	int res;
	uint8_t uicr[NRFX_UICR_SIZE];
	struct flash_sector *sector = &bank->sectors[0];
	uint32_t base = nrfx_uicr_base(chip->family);

	if (base == NRFX_UNIMPLEMENTED) {
		LOG_ERROR("%s: unsupported family %d\n", __func__,
			  chip->family);
		return ERROR_FAIL;
	}
	if ((offset + count) > NRFX_UICR_SIZE)
		return ERROR_FAIL;

	res = target_read_memory(bank->target,
				 base,
				 1,
				 NRFX_UICR_SIZE,
				 uicr);

	if (res != ERROR_OK)
		return res;

	res = nrfx_erase_page(bank, chip, sector);
	if (res != ERROR_OK)
		return res;

	res = nrfx_nvmc_write_enable(chip);
	if (res != ERROR_OK)
		return res;

	memcpy(&uicr[offset], buffer, count);

	res = nrfx_ll_flash_write(chip, base, uicr, NRFX_UICR_SIZE);
	if (res != ERROR_OK) {
		nrfx_nvmc_read_only(chip);
		return res;
	}

	return nrfx_nvmc_read_only(chip);
}


static int nrfx_write(struct flash_bank *bank, const uint8_t *buffer,
		       uint32_t offset, uint32_t count)
{
	int res;
	struct nrfx_info *chip;

	res = nrfx_get_probed_chip_if_halted(bank, &chip);
	if (res != ERROR_OK)
		return res;

	return chip->bank[bank->bank_number].write(bank, chip, buffer, offset, count);
}

static void nrfx_free_driver_priv(struct flash_bank *bank)
{
	struct nrfx_info *chip = bank->driver_priv;
	if (chip == NULL)
		return;

	chip->refcount--;
	if (chip->refcount == 0) {
		free(chip);
		bank->driver_priv = NULL;
	}
}

static int nrfx_flash_bank_command(struct flash_bank *bank, int family)
{
	static struct nrfx_info *chip;

	switch (bank->base) {
	case NRFX_FLASH_BASE:
		bank->bank_number = 0;
		break;
	case NRF5_UICR_BASE:
	case NRF9_UICR_BASE:
		bank->bank_number = 1;
		break;
	default:
		LOG_ERROR("Invalid bank address 0x%08" PRIx32, bank->base);
		return ERROR_FAIL;
	}

	if (!chip) {
		/* Create a new chip */
		chip = calloc(1, sizeof(*chip));
		if (!chip)
			return ERROR_FAIL;

		chip->target = bank->target;
		chip->family = family;
		switch(family) {
		case 51:
			chip->ficr_registers = nrf51_ficr_registers;
			chip->uicr_registers = nrf51_uicr_registers;
			chip->nvmc_registers = nrf5_nvmc_registers;
			break;
		case 52:
			chip->ficr_registers = nrf52_ficr_registers;
			chip->uicr_registers = nrf52_uicr_registers;
			chip->nvmc_registers = nrf5_nvmc_registers;
			break;
		case 91:
			chip->ficr_registers = nrf91_ficr_registers;
			chip->uicr_registers = nrf91_uicr_registers;
			chip->nvmc_registers = nrf91_nvmc_registers;
			break;
		default:
			LOG_ERROR("Unsupported family %d\n", family);
		}
	}

	switch (bank->base) {
	case NRFX_FLASH_BASE:
		chip->bank[bank->bank_number].write = nrfx_code_flash_write;
		break;
	case NRF5_UICR_BASE:
	case NRF9_UICR_BASE:
		chip->bank[bank->bank_number].write = nrfx_uicr_flash_write;
		break;
	}

	chip->refcount++;
	chip->bank[bank->bank_number].probed = false;
	bank->driver_priv = chip;

	return ERROR_OK;
}

FLASH_BANK_COMMAND_HANDLER(nrf51_flash_bank_command)
{
	return nrfx_flash_bank_command(bank, 51);
}

FLASH_BANK_COMMAND_HANDLER(nrf52_flash_bank_command)
{
	return nrfx_flash_bank_command(bank, 52);
}

FLASH_BANK_COMMAND_HANDLER(nrf91_flash_bank_command)
{
	return nrfx_flash_bank_command(bank, 91);
}

static int nrfx_handle_mass_erase_command(struct command_invocation *cmd,
					  int family)
{
	int res;
	struct flash_bank *bank = NULL;
	struct target *target = get_current_target(CMD_CTX);

	res = get_flash_bank_by_addr(target, NRFX_FLASH_BASE, true, &bank);
	if (res != ERROR_OK)
		return res;

	assert(bank != NULL);

	struct nrfx_info *chip;

	res = nrfx_get_probed_chip_if_halted(bank, &chip);
	if (res != ERROR_OK)
		return res;

	uint32_t ppfc;

	res = ficr_read(chip, NRFX_FICR_PPFC, &ppfc);
	if (res != ERROR_OK) {
		LOG_ERROR("Couldn't read PPFC register");
		return res;
	}

	if ((ppfc & 0xFF) == 0x00) {
		LOG_ERROR("Code region 0 size was pre-programmed at the factory, "
			  "mass erase command won't work.");
		return ERROR_FAIL;
	}

	res = nrfx_erase_all(chip);
	if (res != ERROR_OK) {
		LOG_ERROR("Failed to erase the chip");
		nrfx_protect_check(bank);
		return res;
	}

	res = nrfx_protect_check(bank);
	if (res != ERROR_OK) {
		LOG_ERROR("Failed to check chip's write protection");
		return res;
	}

	res = get_flash_bank_by_addr(target, nrfx_uicr_base(chip->family), true,
				     &bank);
	if (res != ERROR_OK)
		return res;

	return ERROR_OK;
}

COMMAND_HANDLER(nrf5_handle_mass_erase_command)
{
	return nrfx_handle_mass_erase_command(cmd, 5);
}

#ifndef cat
#define cat(a,b) a ## b
#endif

#ifndef xcat
#define xcat(a,b) cat(a,b)
#endif

#define ficr_addr(r, f) \
	xcat(xcat(nrf,f),_ficr_registers)[NRFX_FICR_ ## r]

#define ficr5_addr(r) ficr_addr(r, 5)

#define uicr_addr(r, f) \
	xcat(xcat(nrf,f),_uicr_registers)[NRFX_UICR_ ## r]

#define uicr5_addr(r) uicr_addr(r, 5)

static int nrfx_info(struct flash_bank *bank, char *buf, int buf_size)
{
	int res, written = 0;

	struct nrfx_info *chip;
	uint32_t ficr[NRFX_FICR_NREGS];
	uint32_t uicr[NRFX_UICR_NREGS];

	res = nrfx_get_probed_chip_if_halted(bank, &chip);
	if (res != ERROR_OK)
		return res;


	for (size_t i = 0; i < NRFX_FICR_NREGS; i++) {
		res = ficr_read(chip, i, &ficr[i]);
		if (res == ERROR_TARGET_RESOURCE_NOT_AVAILABLE)
			/* Register is not implemented, go on */
			continue;
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read %" PRIx32,
				  chip->ficr_registers[i]);
			return res;
		}
	}

	for (size_t i = 0; i < NRFX_UICR_NREGS; i++) {
		res = uicr_read(chip, i, &uicr[i]);
		if (res == ERROR_TARGET_RESOURCE_NOT_AVAILABLE)
			/* Register is not implemented, go on */
			continue;
		if (res != ERROR_OK) {
			LOG_ERROR("Couldn't read %" PRIx32,
				  chip->uicr_registers[i]);
			return res;
		}
	}

	res = snprintf(buf, buf_size,
		       "\n[factory information control block]\n\n");
	if (res < 0)
		return res;
	written += res;
	if (ficr_is_implemented(chip, NRFX_FICR_PART)) {
		res = snprintf(buf + written, buf_size - written,
			       "part: %"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_PART)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_VARIANT)) {
		union {
			uint32_t ul;
			char str[5];
		} v;
		v.str[4] = 0;
		v.ul = ficr[reg_index(NRFX_FICR_VARIANT)];
		res = snprintf(buf + written, buf_size - written,
			       "variant: %s\n", v.str);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_PACKAGE)) {
		res = snprintf(buf + written, buf_size - written,
			       "package code: %"PRIu32"\n",
			       ficr[reg_index(NRFX_FICR_PACKAGE)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_RAM)) {
		res = snprintf(buf + written, buf_size - written,
			       "total RAM: %uKB\n",
			       ficr[reg_index(NRFX_FICR_RAM)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_CODEPAGESIZE)) {
		res = snprintf(buf + written, buf_size - written,
			       "code page size: %"PRIu32"B\n",
			       ficr[reg_index(NRFX_FICR_CODEPAGESIZE)]);
		if (res < 0)
			return res;
		written += res;
		if (ficr_is_implemented(chip, NRFX_FICR_CODESIZE)) {
			res = snprintf(buf + written, buf_size - written,
				       "code memory size: %"PRIu32"KB\n",
				       (ficr[reg_index(NRFX_FICR_CODEPAGESIZE)]*
					ficr[reg_index(NRFX_FICR_CODESIZE)])
				       >> 10);
			if (res < 0)
				return res;
			written += res;
		}
	}
	if (ficr_is_implemented(chip, NRFX_FICR_FLASH)) {
		res = snprintf(buf + written, buf_size - written,
			       "code memory size: %uKB\n",
			       ficr[reg_index(NRFX_FICR_FLASH)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_CLENR0)) {
		res = snprintf(buf + written, buf_size - written,
			       "code region 0 size: %"PRIu32"kB\n",
			       (ficr[reg_index(NRFX_FICR_CLENR0)] ==
				0xFFFFFFFF) ? 0 :
			       ficr[reg_index(NRFX_FICR_CLENR0)] >> 10);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_PPFC)) {
		res = snprintf(buf + written, buf_size - written,
			       "pre-programmed code: %s\n",
			       ((ficr[reg_index(NRFX_FICR_PPFC)] & 0xFF) ==
				0x00) ?
			       "present" : "not present");
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_NUMRAMBLOCK)) {
		res = snprintf(buf + written, buf_size - written,
			       "number of ram blocks: %"PRIu32"\n",
			       ficr[reg_index(NRFX_FICR_NUMRAMBLOCK)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_SIZERAMBLOCK0)) {
		res = snprintf(buf + written, buf_size - written,
			       "ram block 0 size: %"PRIu32"B\n",
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK0)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_SIZERAMBLOCK1)) {
		res = snprintf(buf + written, buf_size - written,
			       "ram block 1 size: %"PRIu32"B\n",
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK1)] ==
			       0xFFFFFFFF ? 0 :
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK1)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_SIZERAMBLOCK2)) {
		res = snprintf(buf + written, buf_size - written,
			       "ram block 1 size: %"PRIu32"B\n",
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK2)] ==
			       0xFFFFFFFF ? 0 :
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK2)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_SIZERAMBLOCK3)) {
		res = snprintf(buf + written, buf_size - written,
			       "ram block 1 size: %"PRIu32"B\n",
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK3)] ==
			       0xFFFFFFFF ? 0 :
			       ficr[reg_index(NRFX_FICR_SIZERAMBLOCK3)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_CONFIGID)) {
		res = snprintf(buf + written, buf_size - written,
			       "config id: %" PRIx32 "\n",
			       ficr[reg_index(NRFX_FICR_CONFIGID)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_DEVICEID0) &&
	    ficr_is_implemented(chip, NRFX_FICR_DEVICEID1)) {
		res = snprintf(buf + written, buf_size - written,
			       "device id: 0x%"PRIx32"%08"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_DEVICEID0)],
			       ficr[reg_index(NRFX_FICR_DEVICEID1)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_ER0) &&
	    ficr_is_implemented(chip, NRFX_FICR_ER1) &&
	    ficr_is_implemented(chip, NRFX_FICR_ER2) &&
	    ficr_is_implemented(chip, NRFX_FICR_ER3)) {
		res = snprintf(buf + written, buf_size - written,
			       "encryption root: 0x%08"PRIx32"%08"PRIx32"%08"PRIx32"%08"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_ER0)],
			       ficr[reg_index(NRFX_FICR_ER1)],
			       ficr[reg_index(NRFX_FICR_ER2)],
			       ficr[reg_index(NRFX_FICR_ER3)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_IR0) &&
	    ficr_is_implemented(chip, NRFX_FICR_IR1) &&
	    ficr_is_implemented(chip, NRFX_FICR_IR2) &&
	    ficr_is_implemented(chip, NRFX_FICR_IR3)) {
		res = snprintf(buf + written, buf_size - written,
			       "identity root: 0x%08"PRIx32"%08"PRIx32"%08"PRIx32"%08"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_IR0)],
			       ficr[reg_index(NRFX_FICR_IR1)],
			       ficr[reg_index(NRFX_FICR_IR2)],
			       ficr[reg_index(NRFX_FICR_IR3)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_DEVICEADDRTYPE)) {
		res = snprintf(buf + written, buf_size - written,
			       "device address type: 0x%"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_DEVICEADDRTYPE)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_DEVICEADDR0) &&
	    ficr_is_implemented(chip, NRFX_FICR_DEVICEADDR1)) {
		res = snprintf(buf + written, buf_size - written,
			       "device address: 0x%"PRIx32"%08"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_DEVICEADDR0)],
			       ficr[reg_index(NRFX_FICR_DEVICEADDR1)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_OVERRIDEN)) {
		res = snprintf(buf + written, buf_size - written,
			       "override enable: %"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_OVERRIDEN)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_NRF_1MBIT0) &&
	    ficr_is_implemented(chip, NRFX_FICR_NRF_1MBIT1) &&
	    ficr_is_implemented(chip, NRFX_FICR_NRF_1MBIT2) &&
	    ficr_is_implemented(chip, NRFX_FICR_NRF_1MBIT3) &&
	    ficr_is_implemented(chip, NRFX_FICR_NRF_1MBIT4)) {
		res = snprintf(buf + written, buf_size - written,
			       "NRF_1MBIT values: %"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_NRF_1MBIT0)],
			       ficr[reg_index(NRFX_FICR_NRF_1MBIT1)],
			       ficr[reg_index(NRFX_FICR_NRF_1MBIT2)],
			       ficr[reg_index(NRFX_FICR_NRF_1MBIT3)],
			       ficr[reg_index(NRFX_FICR_NRF_1MBIT4)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (ficr_is_implemented(chip, NRFX_FICR_BLE_1MBIT0) &&
	    ficr_is_implemented(chip, NRFX_FICR_BLE_1MBIT1) &&
	    ficr_is_implemented(chip, NRFX_FICR_BLE_1MBIT2) &&
	    ficr_is_implemented(chip, NRFX_FICR_BLE_1MBIT3) &&
	    ficr_is_implemented(chip, NRFX_FICR_BLE_1MBIT4)) {
		res = snprintf(buf + written, buf_size - written,
			       "BLE_1MBIT values: %"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32"\n",
			       ficr[reg_index(NRFX_FICR_BLE_1MBIT0)],
			       ficr[reg_index(NRFX_FICR_BLE_1MBIT1)],
			       ficr[reg_index(NRFX_FICR_BLE_1MBIT2)],
			       ficr[reg_index(NRFX_FICR_BLE_1MBIT3)],
			       ficr[reg_index(NRFX_FICR_BLE_1MBIT4)]);
		if (res < 0)
			return res;
		written += res;
	}
	res = snprintf(buf + written, buf_size - written,
		       "\n[user information control block]\n\n");
	if (res < 0)
		return res;
	written += res;
	if (uicr_is_implemented(chip, NRFX_UICR_CLENR0)) {
		res = snprintf(buf + written, buf_size - written,
			       "code region 0 size: %"PRIu32"kB\n",
			       uicr[reg_index(NRFX_UICR_CLENR0)] == 0xFFFFFFFF ?
			       0 : uicr[reg_index(NRFX_UICR_CLENR0)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (uicr_is_implemented(chip, NRFX_UICR_RBPCONF)) {
		res = snprintf(buf + written, buf_size - written,
			       "read back protection configuration: %"PRIx32"\n",
			       uicr[reg_index(NRFX_UICR_RBPCONF)] & 0xFFFF);
		if (res < 0)
			return res;
		written += res;
	}
	if (uicr_is_implemented(chip, NRFX_UICR_RBPCONF)) {
		res = snprintf(buf + written, buf_size - written,
			       "reset value for XTALFREQ: %"PRIx32"\n",
			       uicr[reg_index(NRFX_UICR_RBPCONF)] & 0xFFFF);
		if (res < 0)
			return res;
		written += res;
	}
	if (uicr_is_implemented(chip, NRFX_UICR_FWID)) {
		res = snprintf(buf + written, buf_size - written,
			       "firmware id: 0x%04"PRIx32"\n",
			       uicr[reg_index(NRFX_UICR_FWID)] & 0xFFFF);
		if (res < 0)
			return res;
		written += res;
	}
	if (uicr_is_implemented(chip, NRFX_UICR_APPROTECT)) {
		res = snprintf(buf + written, buf_size - written,
			       "APPROTECT: %"PRIx32"\n",
			       uicr[reg_index(NRFX_UICR_APPROTECT)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (uicr_is_implemented(chip, NRFX_UICR_SECUREAPPROTECT)) {
		res = snprintf(buf + written, buf_size - written,
			       "SECUREAPPROTECT: %"PRIx32"\n",
			       uicr[reg_index(NRFX_UICR_SECUREAPPROTECT)]);
		if (res < 0)
			return res;
		written += res;
	}
	if (uicr_is_implemented(chip, NRFX_UICR_ERASEPROTECT)) {
		res = snprintf(buf + written, buf_size - written,
			       "ERASEPROTECT: %"PRIx32"\n",
			       uicr[reg_index(NRFX_UICR_ERASEPROTECT)]);
		if (res < 0)
			return res;
		written += res;
	}
	return ERROR_OK;
}

static const struct command_registration nrf5_exec_command_handlers[] = {
	{
		.name		= "mass_erase",
		.handler	= nrf5_handle_mass_erase_command,
		.mode		= COMMAND_EXEC,
		.help		= "Erase all flash contents of the chip.",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration nrf5_command_handlers[] = {
	{
		.name	= "nrf5",
		.mode	= COMMAND_ANY,
		.help	= "nrf5 flash command group",
		.usage	= "",
		.chain	= nrf5_exec_command_handlers,
	},
	{
		.name	= "nrf51",
		.mode	= COMMAND_ANY,
		.help	= "nrf51 flash command group",
		.usage	= "",
		.chain	= nrf5_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

struct flash_driver nrf5_flash = {
	.name			= "nrf5",
	.commands		= nrf5_command_handlers,
	.flash_bank_command	= nrf51_flash_bank_command,
	.info			= nrfx_info,
	.erase			= nrfx_erase,
	.protect		= nrfx_protect,
	.write			= nrfx_write,
	.read			= default_flash_read,
	.probe			= nrfx_probe,
	.auto_probe		= nrfx_auto_probe,
	.erase_check		= default_flash_blank_check,
	.protect_check		= nrfx_protect_check,
	.free_driver_priv	= nrfx_free_driver_priv,
};

/* We need to retain the flash-driver name as well as the commands
 * for backwards compatability */
struct flash_driver nrf51_flash = {
	.name			= "nrf51",
	.commands		= nrf5_command_handlers,
	.flash_bank_command	= nrf51_flash_bank_command,
	.info			= nrfx_info,
	.erase			= nrfx_erase,
	.protect		= nrfx_protect,
	.write			= nrfx_write,
	.read			= default_flash_read,
	.probe			= nrfx_probe,
	.auto_probe		= nrfx_auto_probe,
	.erase_check		= default_flash_blank_check,
	.protect_check		= nrfx_protect_check,
	.free_driver_priv	= nrfx_free_driver_priv,
};

struct flash_driver nrf52_flash = {
	.name			= "nrf52",
	.commands		= nrf5_command_handlers,
	.flash_bank_command	= nrf52_flash_bank_command,
	.info			= nrfx_info,
	.erase			= nrfx_erase,
	.protect		= nrfx_protect,
	.write			= nrfx_write,
	.read			= default_flash_read,
	.probe			= nrfx_probe,
	.auto_probe		= nrfx_auto_probe,
	.erase_check		= default_flash_blank_check,
	.protect_check		= nrfx_protect_check,
	.free_driver_priv	= nrfx_free_driver_priv,
};

struct flash_driver nrf91_flash = {
	.name			= "nrf91",
	.commands		= nrf5_command_handlers,
	.flash_bank_command	= nrf91_flash_bank_command,
	.info			= nrfx_info,
	.erase			= nrfx_erase,
	.protect		= nrfx_protect,
	.write			= nrfx_write,
	.read			= default_flash_read,
	.probe			= nrfx_probe,
	.auto_probe		= nrfx_auto_probe,
	.erase_check		= default_flash_blank_check,
	.protect_check		= nrfx_protect_check,
	.free_driver_priv	= nrfx_free_driver_priv,
};
