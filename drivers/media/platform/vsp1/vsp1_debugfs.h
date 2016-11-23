/*
 * vsp1_debugfs.h  --  R-Car VSP1 DebugFS system
 *
 * Copyright (C) 2016 Renesas Corporation
 *
 * Contact: Kieran Bingham (kieran@bingham.xyz)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __VSP1_DEBUGFS_H__
#define __VSP1_DEBUGFS_H__

#ifdef CONFIG_DEBUG_FS
int vsp1_debugfs_init(struct vsp1_device *vsp1);
void vsp1_debugfs_remove(struct vsp1_device *vsp1);
char *vsp1_reg_to_name(u32 offset);

void vsp1_debugfs_create_video_stats(struct vsp1_video *video,
		const char *name);
void vsp1_debugfs_cleanup_video_stats(struct vsp1_video *video);
#else
static inline int vsp1_debugfs_init(struct vsp1_device *vsp1)
{
	return 0;
}

static inline void vsp1_debugfs_remove(struct vsp1_device *vsp1)
{
	return;
}

static inline char *vsp1_reg_to_name(u32 offset) {
	return "<>";
}

static inline vsp1_debugfs_create_video_stats(struct vsp1_video *video,
		const char *name) { };
static inline vsp1_debugfs_cleanup_video_stats(struct vsp1_video *video) { };
#endif /* CONFIG_DEBUG_FS */

#endif /* __VSP1_DEBUGFS_H__ */
