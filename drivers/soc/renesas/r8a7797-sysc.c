/*
 * Renesas R-Car V3M System Controller
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a7797-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7797_areas[] __initconst = {
	{ "always-on",	    0, 0, R8A7797_PD_ALWAYS_ON,	-1, PD_ALWAYS_ON },
	{ "ca53-scu",	0x140, 0, R8A7797_PD_CA53_SCU,	R8A7797_PD_ALWAYS_ON,
	  PD_SCU },
	{ "ca53-cpu0",	0x200, 0, R8A7797_PD_CA53_CPU0,	R8A7797_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "ca53-cpu1",	0x200, 1, R8A7797_PD_CA53_CPU1,	R8A7797_PD_CA53_SCU,
	  PD_CPU_NOCR },
	{ "cr7",	0x240, 0, R8A7797_PD_CR7,	R8A7797_PD_ALWAYS_ON },
	{ "a3ir",	0x180, 0, R8A7797_PD_A3IR,	R8A7797_PD_ALWAYS_ON },
	{ "a2ir0",	0x400, 0, R8A7797_PD_A2IR0,	R8A7797_PD_ALWAYS_ON },
	{ "a2ir1",	0x400, 1, R8A7797_PD_A2IR1,	R8A7797_PD_A2IR0 },
	{ "a2ir2",	0x400, 2, R8A7797_PD_A2IR2,	R8A7797_PD_A2IR0 },
	{ "a2ir3",	0x400, 3, R8A7797_PD_A2IR3,	R8A7797_PD_A2IR0 },
	{ "a2sc0",	0x400, 4, R8A7797_PD_A2SC0,	R8A7797_PD_ALWAYS_ON },
	{ "a2sc1",	0x400, 5, R8A7797_PD_A2SC1,	R8A7797_PD_A2SC0 },
};

const struct rcar_sysc_info r8a7797_sysc_info __initconst = {
	.areas = r8a7797_areas,
	.num_areas = ARRAY_SIZE(r8a7797_areas),
};
