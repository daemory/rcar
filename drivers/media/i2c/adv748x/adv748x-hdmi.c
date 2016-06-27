/*
 * Driver for Analog Devices ADV748X HDMI receiver and Component Processor (CP)
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/mutex.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>

#include <uapi/linux/v4l2-dv-timings.h>

#include "adv748x.h"

/* -----------------------------------------------------------------------------
 * Register definitions
 */

#define ADV748X_CP_VID_ADJ_2			0x91
#define ADV748X_CP_VID_ADJ_2_INTERLACED		BIT(6)
#define ADV748X_CP_VID_ADJ_2_INTERLACED_3D	BIT(4)

/* -----------------------------------------------------------------------------
 * HDMI and CP
 */

#define ADV748X_HDMI_MIN_WIDTH		640
#define ADV748X_HDMI_MAX_WIDTH		1920
#define ADV748X_HDMI_MIN_HEIGHT		480
#define ADV748X_HDMI_MAX_HEIGHT		1200
#define ADV748X_HDMI_MIN_PIXELCLOCK	0		/* unknown */
#define ADV748X_HDMI_MAX_PIXELCLOCK	162000000

static const struct v4l2_dv_timings_cap adv748x_hdmi_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	/* Min pixelclock value is unknown */
	V4L2_INIT_BT_TIMINGS(ADV748X_HDMI_MIN_WIDTH, ADV748X_HDMI_MAX_WIDTH,
			     ADV748X_HDMI_MIN_HEIGHT, ADV748X_HDMI_MAX_HEIGHT,
			     ADV748X_HDMI_MIN_PIXELCLOCK,
			     ADV748X_HDMI_MAX_PIXELCLOCK,
			     V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT,
			     V4L2_DV_BT_CAP_INTERLACED |
			     V4L2_DV_BT_CAP_PROGRESSIVE)
};

struct adv748x_hdmi_video_standards {
	struct v4l2_dv_timings timings;
	u8 vid_std;
	u8 v_freq;
};

static const struct adv748x_hdmi_video_standards
adv748x_hdmi_video_standards[] = {
	{ V4L2_DV_BT_CEA_720X480I59_94, 0x40, 0x00 },
	{ V4L2_DV_BT_CEA_720X576I50, 0x41, 0x01 },
	{ V4L2_DV_BT_CEA_720X480P59_94, 0x4a, 0x00 },
	{ V4L2_DV_BT_CEA_720X576P50, 0x4b, 0x00 },
	{ V4L2_DV_BT_CEA_1280X720P60, 0x53, 0x00 },
	{ V4L2_DV_BT_CEA_1280X720P50, 0x53, 0x01 },
	{ V4L2_DV_BT_CEA_1280X720P30, 0x53, 0x02 },
	{ V4L2_DV_BT_CEA_1280X720P25, 0x53, 0x03 },
	{ V4L2_DV_BT_CEA_1280X720P24, 0x53, 0x04 },
	{ V4L2_DV_BT_CEA_1920X1080I60, 0x54, 0x00 },
	{ V4L2_DV_BT_CEA_1920X1080I50, 0x54, 0x01 },
	{ V4L2_DV_BT_CEA_1920X1080P60, 0x5e, 0x00 },
	{ V4L2_DV_BT_CEA_1920X1080P50, 0x5e, 0x01 },
	{ V4L2_DV_BT_CEA_1920X1080P30, 0x5e, 0x02 },
	{ V4L2_DV_BT_CEA_1920X1080P25, 0x5e, 0x03 },
	{ V4L2_DV_BT_CEA_1920X1080P24, 0x5e, 0x04 },
	/* SVGA */
	{ V4L2_DV_BT_DMT_800X600P56, 0x80, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P60, 0x81, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P72, 0x82, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P75, 0x83, 0x00 },
	{ V4L2_DV_BT_DMT_800X600P85, 0x84, 0x00 },
	/* SXGA */
	{ V4L2_DV_BT_DMT_1280X1024P60, 0x85, 0x00 },
	{ V4L2_DV_BT_DMT_1280X1024P75, 0x86, 0x00 },
	/* VGA */
	{ V4L2_DV_BT_DMT_640X480P60, 0x88, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P72, 0x89, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P75, 0x8a, 0x00 },
	{ V4L2_DV_BT_DMT_640X480P85, 0x8b, 0x00 },
	/* XGA */
	{ V4L2_DV_BT_DMT_1024X768P60, 0x8c, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P70, 0x8d, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P75, 0x8e, 0x00 },
	{ V4L2_DV_BT_DMT_1024X768P85, 0x8f, 0x00 },
	/* UXGA */
	{ V4L2_DV_BT_DMT_1600X1200P60, 0x96, 0x00 },
};

static void adv748x_hdmi_fill_format(struct adv748x_hdmi *hdmi,
				     struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));

	fmt->code = MEDIA_BUS_FMT_RGB888_1X24;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->field = hdmi->timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;

	fmt->width = hdmi->timings.bt.width;
	fmt->height = hdmi->timings.bt.height;
}

static void adv748x_fill_optional_dv_timings(struct v4l2_dv_timings *timings)
{
	v4l2_find_dv_timings_cap(timings, &adv748x_hdmi_timings_cap,
				 250000, NULL, NULL);
}

static bool adv748x_hdmi_has_signal(struct adv748x_state *state)
{
	int val;

	/* Check that VERT_FILTER and DG_REGEN is locked */
	val = hdmi_read(state, 0x07);
	return (val & BIT(7)) && (val & BIT(5));
}

static int adv748x_hdmi_read_pixelclock(struct adv748x_state *state)
{
	int a, b;

	a = hdmi_read(state, 0x51);
	b = hdmi_read(state, 0x52);
	if (a < 0 || b < 0)
		return -ENODATA;

	return ((a << 1) | (b >> 7)) * 1000000 + (b & 0x7f) * 1000000 / 128;
}

static int adv748x_hdmi_set_video_timings(struct adv748x_state *state,
					  const struct v4l2_dv_timings *timings)
{
	const struct adv748x_hdmi_video_standards *stds =
		adv748x_hdmi_video_standards;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(adv748x_hdmi_video_standards); i++) {
		if (!v4l2_match_dv_timings(timings, &stds[i].timings, 250000,
					   false))
			continue;
	}

	if (i >= ARRAY_SIZE(adv748x_hdmi_video_standards))
		return -EINVAL;

	/*
	 * The resolution of 720p, 1080i and 1080p is Hsync width of
	 * 40 pixelclock cycles. These resolutions must be shifted
	 * horizontally to the left in active video mode.
	 */
	switch (stds[i].vid_std) {
	case 0x53: /* 720p */
		cp_write(state, 0x8b, 0x43);
		cp_write(state, 0x8c, 0xd8);
		cp_write(state, 0x8b, 0x4f);
		cp_write(state, 0x8d, 0xd8);
		break;
	case 0x54: /* 1080i */
	case 0x5e: /* 1080p */
		cp_write(state, 0x8b, 0x43);
		cp_write(state, 0x8c, 0xd4);
		cp_write(state, 0x8b, 0x4f);
		cp_write(state, 0x8d, 0xd4);
		break;
	default:
		cp_write(state, 0x8b, 0x40);
		cp_write(state, 0x8c, 0x00);
		cp_write(state, 0x8b, 0x40);
		cp_write(state, 0x8d, 0x00);
		break;
	}

	io_write(state, 0x05, stds[i].vid_std);
	io_clrset(state, 0x03, 0x70, stds[i].v_freq << 4);

	return 0;
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_video_ops
 */

static int adv748x_hdmi_s_dv_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);
	int ret;

	if (!timings)
		return -EINVAL;

	if (v4l2_match_dv_timings(&hdmi->timings, timings, 0, false))
		return 0;

	if (!v4l2_valid_dv_timings(timings, &adv748x_hdmi_timings_cap,
				   NULL, NULL))
		return -ERANGE;

	adv748x_fill_optional_dv_timings(timings);

	mutex_lock(&state->mutex);

	ret = adv748x_hdmi_set_video_timings(state, timings);
	if (ret)
		goto error;

	hdmi->timings = *timings;

	cp_clrset(state, ADV748X_CP_VID_ADJ_2, ADV748X_CP_VID_ADJ_2_INTERLACED,
		  timings->bt.interlaced ?
				  ADV748X_CP_VID_ADJ_2_INTERLACED : 0);

	mutex_unlock(&state->mutex);

	return 0;

error:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv748x_hdmi_g_dv_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);

	mutex_lock(&state->mutex);

	*timings = hdmi->timings;

	mutex_unlock(&state->mutex);

	return 0;
}

static int adv748x_hdmi_query_dv_timings(struct v4l2_subdev *sd,
					 struct v4l2_dv_timings *timings)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);
	struct v4l2_bt_timings *bt = &timings->bt;
	int pixelclock;
	int polarity;

	if (!timings)
		return -EINVAL;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));

	if (!adv748x_hdmi_has_signal(state))
		return -ENOLINK;

	pixelclock = adv748x_hdmi_read_pixelclock(state);
	if (pixelclock < 0)
		return -ENODATA;

	timings->type = V4L2_DV_BT_656_1120;

	bt->interlaced = hdmi_read(state, 0x0b) & BIT(5) ?
				V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;
	bt->width = hdmi_read16(state, 0x07, 0x1fff);
	bt->height = hdmi_read16(state, 0x09, 0x1fff);
	bt->hfrontporch = hdmi_read16(state, 0x20, 0x1fff);
	bt->hsync = hdmi_read16(state, 0x22, 0x1fff);
	bt->hbackporch = hdmi_read16(state, 0x24, 0x1fff);
	bt->vfrontporch = hdmi_read16(state, 0x2a, 0x3fff) / 2;
	bt->vsync = hdmi_read16(state, 0x2e, 0x3fff) / 2;
	bt->vbackporch = hdmi_read16(state, 0x32, 0x3fff) / 2;
	bt->pixelclock = pixelclock;


	polarity = hdmi_read(state, 0x05);
	bt->polarities = (polarity & BIT(4) ? V4L2_DV_VSYNC_POS_POL : 0) |
		(polarity & BIT(5) ? V4L2_DV_HSYNC_POS_POL : 0);

	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height += hdmi_read16(state, 0x0b, 0x1fff);
		bt->il_vfrontporch = hdmi_read16(state, 0x2c, 0x3fff) / 2;
		bt->il_vsync = hdmi_read16(state, 0x30, 0x3fff) / 2;
		bt->il_vbackporch = hdmi_read16(state, 0x34, 0x3fff) / 2;
	}

	adv748x_fill_optional_dv_timings(timings);

	if (!adv748x_hdmi_has_signal(state)) {
		adv_info(state, "HDMI signal lost during readout\n");
		return -ENOLINK;
	}

	/*
	 * TODO: No interrupt handling is implemented yet.
	 * There should be an IRQ when a cable is plugged and a the new
	 * timings figured out and stored to state. This the next best thing
	 */
	hdmi->timings = *timings;

	adv_dbg(state, "HDMI %dx%d%c clock: %llu Hz pol: %x "
		"hfront: %d hsync: %d hback: %d "
		"vfront: %d vsync: %d vback: %d "
		"il_vfron: %d il_vsync: %d il_vback: %d\n",
		bt->width, bt->height,
		bt->interlaced == V4L2_DV_INTERLACED ? 'i' : 'p',
		bt->pixelclock, bt->polarities,
		bt->hfrontporch, bt->hsync, bt->hbackporch,
		bt->vfrontporch, bt->vsync, bt->vbackporch,
		bt->il_vfrontporch, bt->il_vsync, bt->il_vbackporch);

	return 0;
}

static int adv748x_hdmi_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);

	mutex_lock(&state->mutex);

	*status = adv748x_hdmi_has_signal(state) ? 0 : V4L2_IN_ST_NO_SIGNAL;

	mutex_unlock(&state->mutex);

	return 0;
}

static int adv748x_hdmi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);
	int ret;

	mutex_lock(&state->mutex);

	ret = adv748x_txa_power(state, enable);
	if (ret)
		goto done;

	if (adv748x_hdmi_has_signal(state))
		adv_dbg(state, "Detected HDMI signal\n");
	else
		adv_dbg(state, "Couldn't detect HDMI video signal\n");

done:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv748x_hdmi_g_pixelaspect(struct v4l2_subdev *sd,
				      struct v4l2_fract *aspect)
{
	aspect->numerator = 1;
	aspect->denominator = 1;

	return 0;
}

static const struct v4l2_subdev_video_ops adv748x_video_ops_hdmi = {
	.s_dv_timings = adv748x_hdmi_s_dv_timings,
	.g_dv_timings = adv748x_hdmi_g_dv_timings,
	.query_dv_timings = adv748x_hdmi_query_dv_timings,
	.g_input_status = adv748x_hdmi_g_input_status,
	.s_stream = adv748x_hdmi_s_stream,
	.g_pixelaspect = adv748x_hdmi_g_pixelaspect,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 */

static int adv748x_hdmi_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_RGB888_1X24;

	return 0;
}

static int adv748x_hdmi_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);

	adv748x_hdmi_fill_format(hdmi, &format->format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		format->format.code = fmt->code;
	}

	return 0;
}

static int adv748x_hdmi_set_pad_format(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_format *format)
{
	struct adv748x_hdmi *hdmi = adv748x_sd_to_hdmi(sd);

	adv748x_hdmi_fill_format(hdmi, &format->format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		fmt->code = format->format.code;
	}

	return 0;
}

static bool adv748x_hdmi_check_dv_timings(const struct v4l2_dv_timings *timings,
					  void *hdl)
{
	const struct adv748x_hdmi_video_standards *stds =
		adv748x_hdmi_video_standards;
	unsigned int i;

	for (i = 0; stds[i].timings.bt.width; i++)
		if (v4l2_match_dv_timings(timings, &stds[i].timings, 0, false))
			return true;

	return false;
}

static int adv748x_hdmi_enum_dv_timings(struct v4l2_subdev *sd,
					struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &adv748x_hdmi_timings_cap,
					adv748x_hdmi_check_dv_timings, NULL);
}

static int adv748x_hdmi_dv_timings_cap(struct v4l2_subdev *sd,
				       struct v4l2_dv_timings_cap *cap)
{
	*cap = adv748x_hdmi_timings_cap;
	return 0;
}

static const struct v4l2_subdev_pad_ops adv748x_pad_ops_hdmi = {
	.enum_mbus_code = adv748x_hdmi_enum_mbus_code,
	.set_fmt = adv748x_hdmi_set_pad_format,
	.get_fmt = adv748x_hdmi_get_pad_format,
	.dv_timings_cap = adv748x_hdmi_dv_timings_cap,
	.enum_dv_timings = adv748x_hdmi_enum_dv_timings,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops adv748x_ops_hdmi = {
	.video = &adv748x_video_ops_hdmi,
	.pad = &adv748x_pad_ops_hdmi,
};

/* -----------------------------------------------------------------------------
 * Controls
 */

#define ADV748X_IO_REG_04		0x04
#define ADV748X_IO_REG_04_FORCE_FR	BIT(0)	/* Force CP free-run */

#define ADV748X_CP_PAT_GEN		0x37	/* Pattern Generator */
#define ADV748X_CP_PAT_GEN_EN		BIT(7)

/* Contrast Control */
#define ADV748X_CP_CON		0x3a	/* Contrast (unsigned) */
#define ADV748X_CP_CON_MIN	0	/* Minimum contrast */
#define ADV748X_CP_CON_DEF	128	/* Default */
#define ADV748X_CP_CON_MAX	255	/* Maximum contrast */

/* Saturation Control */
#define ADV748X_CP_SAT		0x3b	/* Saturation (unsigned) */
#define ADV748X_CP_SAT_MIN	0	/* Minimum saturation */
#define ADV748X_CP_SAT_DEF	128	/* Default */
#define ADV748X_CP_SAT_MAX	255	/* Maximum saturation */

/* Brightness Control */
#define ADV748X_CP_BRI		0x3c	/* Brightness (signed) */
#define ADV748X_CP_BRI_MIN	-128	/* Luma is -512d */
#define ADV748X_CP_BRI_DEF	0	/* Luma is 0 */
#define ADV748X_CP_BRI_MAX	127	/* Luma is 508d */

/* Hue Control */
#define ADV748X_CP_HUE		0x3d	/* Hue (unsigned) */
#define ADV748X_CP_HUE_MIN	0	/* -90 degree */
#define ADV748X_CP_HUE_DEF	0	/* -90 degree */
#define ADV748X_CP_HUE_MAX	255	/* +90 degree */

/* Video adjustment register */
#define ADV748X_CP_VID_ADJ		0x3e
/* Enable color controls */
#define ADV748X_CP_VID_ADJ_ENABLE	BIT(7)

static const char * const hdmi_ctrl_patgen_menu[] = {
	"Disabled",
	"Plain Blue",
	"Color Bars",
	"Ramp Grey",
	"Ramp Blue",
	"Ramp Red",
	"Checkered"
};

static int adv748x_hdmi_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct adv748x_hdmi *hdmi = adv748x_ctrl_to_hdmi(ctrl);
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);
	int ret;
	u8 pattern;

	/* Enable video adjustment first */
	ret = cp_clrset(state, ADV748X_CP_VID_ADJ,
			ADV748X_CP_VID_ADJ_ENABLE,
			ADV748X_CP_VID_ADJ_ENABLE);
	if (ret < 0)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = cp_write(state, ADV748X_CP_BRI, ctrl->val);
		break;
	case V4L2_CID_HUE:
		ret = cp_write(state, ADV748X_CP_HUE, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = cp_write(state, ADV748X_CP_CON, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		ret = cp_write(state, ADV748X_CP_SAT, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		pattern = ctrl->val;

		/* Pattern is 0-indexed. Ctrl Menu is 1-indexed */
		if (pattern) {
			pattern--;
			pattern |= ADV748X_CP_PAT_GEN_EN;
		}

		ret = cp_write(state, ADV748X_CP_PAT_GEN, pattern);

		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int adv748x_hdmi_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct adv748x_hdmi *hdmi = adv748x_ctrl_to_hdmi(ctrl);
	unsigned int width, height, fps;

	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
	{
		struct v4l2_dv_timings timings;
		struct v4l2_bt_timings *bt = &timings.bt;

		/*
		 * TODO: Convert to non volatile control with updates through
		 * v4l2_ctrl_s_ctrl_int64() instead.
		 */

		adv748x_hdmi_query_dv_timings(&hdmi->sd, &timings);

		width = bt->width;
		height = bt->height;
		fps = DIV_ROUND_CLOSEST(bt->pixelclock,
					V4L2_DV_BT_FRAME_WIDTH(bt) *
					V4L2_DV_BT_FRAME_HEIGHT(bt));

		*ctrl->p_new.p_s64 = width * height * fps;
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops adv748x_hdmi_ctrl_ops = {
	.s_ctrl = adv748x_hdmi_s_ctrl,
	.g_volatile_ctrl = adv748x_hdmi_g_volatile_ctrl,
};

static int adv748x_hdmi_init_controls(struct adv748x_hdmi *hdmi)
{
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&hdmi->ctrl_hdl, 6);

	/* Use our mutex for the controls */
	hdmi->ctrl_hdl.lock = &state->mutex;

	v4l2_ctrl_new_std(&hdmi->ctrl_hdl, &adv748x_hdmi_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, ADV748X_CP_BRI_MIN,
			  ADV748X_CP_BRI_MAX, 1, ADV748X_CP_BRI_DEF);
	v4l2_ctrl_new_std(&hdmi->ctrl_hdl, &adv748x_hdmi_ctrl_ops,
			  V4L2_CID_CONTRAST, ADV748X_CP_CON_MIN,
			  ADV748X_CP_CON_MAX, 1, ADV748X_CP_CON_DEF);
	v4l2_ctrl_new_std(&hdmi->ctrl_hdl, &adv748x_hdmi_ctrl_ops,
			  V4L2_CID_SATURATION, ADV748X_CP_SAT_MIN,
			  ADV748X_CP_SAT_MAX, 1, ADV748X_CP_SAT_DEF);
	v4l2_ctrl_new_std(&hdmi->ctrl_hdl, &adv748x_hdmi_ctrl_ops,
			  V4L2_CID_HUE, ADV748X_CP_HUE_MIN,
			  ADV748X_CP_HUE_MAX, 1, ADV748X_CP_HUE_DEF);
	ctrl = v4l2_ctrl_new_std(&hdmi->ctrl_hdl, &adv748x_hdmi_ctrl_ops,
				 V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_new_std_menu_items(&hdmi->ctrl_hdl, &adv748x_hdmi_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(hdmi_ctrl_patgen_menu) - 1,
				     0, 0, hdmi_ctrl_patgen_menu);

	hdmi->sd.ctrl_handler = &hdmi->ctrl_hdl;
	if (hdmi->ctrl_hdl.error) {
		v4l2_ctrl_handler_free(&hdmi->ctrl_hdl);
		return hdmi->ctrl_hdl.error;
	}

	return v4l2_ctrl_handler_setup(&hdmi->ctrl_hdl);
}

int adv748x_hdmi_init(struct adv748x_hdmi *hdmi)
{
	struct adv748x_state *state = adv748x_hdmi_to_state(hdmi);
	static const struct v4l2_dv_timings cea720x480 =
		V4L2_DV_BT_CEA_720X480I59_94;
	int ret;

	hdmi->timings = cea720x480;

	adv748x_subdev_init(&hdmi->sd, state, &adv748x_ops_hdmi,
			    MEDIA_ENT_F_IO_DTV, "hdmi");

	hdmi->pads[ADV748X_HDMI_SINK].flags = MEDIA_PAD_FL_SINK;
	hdmi->pads[ADV748X_HDMI_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&hdmi->sd.entity,
				     ADV748X_HDMI_NR_PADS, hdmi->pads);
	if (ret)
		return ret;

	ret = adv748x_hdmi_init_controls(hdmi);
	if (ret)
		goto err_free_media;

	return 0;

err_free_media:
	media_entity_cleanup(&hdmi->sd.entity);

	return ret;
}

void adv748x_hdmi_cleanup(struct adv748x_hdmi *hdmi)
{
	v4l2_device_unregister_subdev(&hdmi->sd);
	media_entity_cleanup(&hdmi->sd.entity);
	v4l2_ctrl_handler_free(&hdmi->ctrl_hdl);
}
