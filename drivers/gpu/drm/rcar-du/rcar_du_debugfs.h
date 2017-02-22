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
#include <linux/debugfs.h>

#ifndef __RCAR_DU_DEBUGFS_H__
#define __RCAR_DU_DEBUGFS_H__

/*
 * Helper for creating seq_file operations
 */
#define DEBUGFS_RO_ATTR(name) \
	static int name##_open(struct inode *inode, struct file *file) \
	{ return single_open(file, name, inode->i_private); }      \
	static const struct file_operations name##_fops = { \
		.owner = THIS_MODULE, \
		.open = name##_open, \
		.llseek = seq_lseek, \
		.read = seq_read, \
		.release = single_release \
	}

#ifdef CONFIG_DRM_RCAR_DEBUGFS
int rcar_du_debugfs_init(struct rcar_du_device *rcdu);
void rcar_du_debugfs_remove(struct rcar_du_device *rcdu);
char *rcar_du_reg_to_name(u32 offset);
#else
static inline int rcar_du_debugfs_init(struct rcar_du_device *rcdu)
{
	return 0;
}

static inline void rcar_du_debugfs_remove(struct rcar_du_device *rcdu) { };

static inline char *rcar_du_reg_to_name(u32 offset)
{
	return "<>";
}
#endif /* CONFIG_DRM_RCAR_DEBUGFS */

#endif /* __RCAR_DU_DEBUGFS_H__ */
