/*
 * rcar_du_debugfs.h  --  R-Car DU DebugFS system
 *
 * Copyright (C) 2017 Renesas Corporation
 *
 * Contact: Kieran Bingham (kieran.bingham@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_regs.h"

#include "rcar_du_debugfs.h"

/*
 * Register maps can be generated with something similar to this expression:
 * cat rcar_du_regs.h | \
 *	grep -E "#define .*0[xX][0-9a-fA-F]{4,5}$" | \
 *	sed -r 's/^#define (\w*).*$/\tRCAR_DU_DBFS_REG(\1),/'
 */

/* Do not use __stringify() here as that will expand the macros */
#define RCAR_DU_DBFS_REG(reg) { #reg, reg, NULL }
#define RCAR_DU_DBFS_REG_DECODE(reg, func)  { #reg, reg, func }

static const struct debugfs_reg32 rcar_du_regset[] = {
	RCAR_DU_DBFS_REG(DSMR),
	RCAR_DU_DBFS_REG(DSSR),
	RCAR_DU_DBFS_REG(DSRCR),
	RCAR_DU_DBFS_REG(DIER),
	RCAR_DU_DBFS_REG(CPCR),
	RCAR_DU_DBFS_REG(DPPR),
	RCAR_DU_DBFS_REG(DEFR),
	RCAR_DU_DBFS_REG(DAPCR),
	RCAR_DU_DBFS_REG(DCPCR),
	RCAR_DU_DBFS_REG(DCPCR),
	RCAR_DU_DBFS_REG(DEFR2),
	RCAR_DU_DBFS_REG(DEFR3),
	RCAR_DU_DBFS_REG(DEFR4),
	RCAR_DU_DBFS_REG(DVCSR),
	RCAR_DU_DBFS_REG(DEFR5),
	RCAR_DU_DBFS_REG(DDLTR),
	RCAR_DU_DBFS_REG(DEFR6),
	RCAR_DU_DBFS_REG(DD1SSR),
	RCAR_DU_DBFS_REG(DD1SRCR),
	RCAR_DU_DBFS_REG(DD1IER),
	RCAR_DU_DBFS_REG(DEFR8),
	RCAR_DU_DBFS_REG(DOFLR),
	RCAR_DU_DBFS_REG(DIDSR),
	RCAR_DU_DBFS_REG(DEFR10),
	RCAR_DU_DBFS_REG(HDSR),
	RCAR_DU_DBFS_REG(HDER),
	RCAR_DU_DBFS_REG(VDSR),
	RCAR_DU_DBFS_REG(VDER),
	RCAR_DU_DBFS_REG(HCR),
	RCAR_DU_DBFS_REG(HSWR),
	RCAR_DU_DBFS_REG(VCR),
	RCAR_DU_DBFS_REG(VSPR),
	RCAR_DU_DBFS_REG(EQWR),
	RCAR_DU_DBFS_REG(SPWR),
	RCAR_DU_DBFS_REG(CLAMPSR),
	RCAR_DU_DBFS_REG(CLAMPWR),
	RCAR_DU_DBFS_REG(DESR),
	RCAR_DU_DBFS_REG(DEWR),
	RCAR_DU_DBFS_REG(CP1TR),
	RCAR_DU_DBFS_REG(CP2TR),
	RCAR_DU_DBFS_REG(CP3TR),
	RCAR_DU_DBFS_REG(CP4TR),
	RCAR_DU_DBFS_REG(DOOR),
	RCAR_DU_DBFS_REG(CDER),
	RCAR_DU_DBFS_REG(BPOR),
	RCAR_DU_DBFS_REG(RINTOFSR),
	RCAR_DU_DBFS_REG(DSHPR),
	RCAR_DU_DBFS_REG(PLANE_OFF),
	RCAR_DU_DBFS_REG(PnMWR),
	RCAR_DU_DBFS_REG(PnALPHAR),
	RCAR_DU_DBFS_REG(PnDSXR),
	RCAR_DU_DBFS_REG(PnDSYR),
	RCAR_DU_DBFS_REG(PnDPXR),
	RCAR_DU_DBFS_REG(PnDPYR),
	RCAR_DU_DBFS_REG(PnDSA0R),
	RCAR_DU_DBFS_REG(PnDSA1R),
	RCAR_DU_DBFS_REG(PnDSA2R),
	RCAR_DU_DBFS_REG(PnSPXR),
	RCAR_DU_DBFS_REG(PnSPYR),
	RCAR_DU_DBFS_REG(PnWASPR),
	RCAR_DU_DBFS_REG(PnWAMWR),
	RCAR_DU_DBFS_REG(PnBTR),
	RCAR_DU_DBFS_REG(PnTC1R),
	RCAR_DU_DBFS_REG(PnTC2R),
	RCAR_DU_DBFS_REG(PnTC3R),
	RCAR_DU_DBFS_REG(PnMLR),
	RCAR_DU_DBFS_REG(PnSWAPR),
	RCAR_DU_DBFS_REG(PnDDCR),
	RCAR_DU_DBFS_REG(PnDDCR2),
	RCAR_DU_DBFS_REG(PnDDCR4),
	RCAR_DU_DBFS_REG(APnMR),
	RCAR_DU_DBFS_REG(APnMWR),
	RCAR_DU_DBFS_REG(APnDSXR),
	RCAR_DU_DBFS_REG(APnDSYR),
	RCAR_DU_DBFS_REG(APnDPXR),
	RCAR_DU_DBFS_REG(APnDPYR),
	RCAR_DU_DBFS_REG(APnDSA0R),
	RCAR_DU_DBFS_REG(APnDSA1R),
	RCAR_DU_DBFS_REG(APnDSA2R),
	RCAR_DU_DBFS_REG(APnSPXR),
	RCAR_DU_DBFS_REG(APnSPYR),
	RCAR_DU_DBFS_REG(APnWASPR),
	RCAR_DU_DBFS_REG(APnWAMWR),
	RCAR_DU_DBFS_REG(APnBTR),
	RCAR_DU_DBFS_REG(APnMLR),
	RCAR_DU_DBFS_REG(APnSWAPR),
	RCAR_DU_DBFS_REG(DCMR),
	RCAR_DU_DBFS_REG(DCMWR),
	RCAR_DU_DBFS_REG(DCSAR),
	RCAR_DU_DBFS_REG(DCMLR),
	RCAR_DU_DBFS_REG(CP1_000R),
	RCAR_DU_DBFS_REG(CP1_255R),
	RCAR_DU_DBFS_REG(CP2_000R),
	RCAR_DU_DBFS_REG(CP2_255R),
	RCAR_DU_DBFS_REG(CP3_000R),
	RCAR_DU_DBFS_REG(CP3_255R),
	RCAR_DU_DBFS_REG(CP4_000R),
	RCAR_DU_DBFS_REG(CP4_255R),
	RCAR_DU_DBFS_REG(ESCR),
	RCAR_DU_DBFS_REG(ESCR2),
	RCAR_DU_DBFS_REG(OTAR),
	RCAR_DU_DBFS_REG(OTAR2),
	RCAR_DU_DBFS_REG(DORCR),
	RCAR_DU_DBFS_REG(DPTSR),
	RCAR_DU_DBFS_REG(DAPTSR),
	RCAR_DU_DBFS_REG(DS1PR),
	RCAR_DU_DBFS_REG(DS2PR),
	RCAR_DU_DBFS_REG(YNCR),
	RCAR_DU_DBFS_REG(YNOR),
	RCAR_DU_DBFS_REG(CRNOR),
	RCAR_DU_DBFS_REG(CBNOR),
	RCAR_DU_DBFS_REG(RCRCR),
	RCAR_DU_DBFS_REG(GCRCR),
	RCAR_DU_DBFS_REG(GCBCR),
	RCAR_DU_DBFS_REG(BCBCR),
};

/*
 * rcar_du_reg_to_name
 *
 * Find the name of the register which matches the offset given.
 * This function assumes that the regset has only unique offsets
 * in the table.
 */
char *rcar_du_reg_to_name(u32 offset)
{
	unsigned int i;
	static char notfound[16];

	for (i = 0; i < ARRAY_SIZE(rcar_du_regset); i++)
		if (rcar_du_regset[i].offset == offset)
			return rcar_du_regset[i].name;

	snprintf(notfound, sizeof(notfound), "<0x%08x>", offset);
	return notfound;
}
