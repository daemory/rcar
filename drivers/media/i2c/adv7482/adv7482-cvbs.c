/*
 * Driver for Analog Devices ADV7482 HDMI receiver
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-of.h>

#include "adv7482.h"

/* -----------------------------------------------------------------------------
 * SDP
 */

#define ADV7482_SDP_INPUT_CVBS_AIN1			0x00
#define ADV7482_SDP_INPUT_CVBS_AIN2			0x01
#define ADV7482_SDP_INPUT_CVBS_AIN3			0x02
#define ADV7482_SDP_INPUT_CVBS_AIN4			0x03
#define ADV7482_SDP_INPUT_CVBS_AIN5			0x04
#define ADV7482_SDP_INPUT_CVBS_AIN6			0x05
#define ADV7482_SDP_INPUT_CVBS_AIN7			0x06
#define ADV7482_SDP_INPUT_CVBS_AIN8			0x07

#define ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM		0x0
#define ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM_PED	0x1
#define ADV7482_SDP_STD_AD_PAL_N_NTSC_J_SECAM		0x2
#define ADV7482_SDP_STD_AD_PAL_N_NTSC_M_SECAM		0x3
#define ADV7482_SDP_STD_NTSC_J				0x4
#define ADV7482_SDP_STD_NTSC_M				0x5
#define ADV7482_SDP_STD_PAL60				0x6
#define ADV7482_SDP_STD_NTSC_443			0x7
#define ADV7482_SDP_STD_PAL_BG				0x8
#define ADV7482_SDP_STD_PAL_N				0x9
#define ADV7482_SDP_STD_PAL_M				0xa
#define ADV7482_SDP_STD_PAL_M_PED			0xb
#define ADV7482_SDP_STD_PAL_COMB_N			0xc
#define ADV7482_SDP_STD_PAL_COMB_N_PED			0xd
#define ADV7482_SDP_STD_PAL_SECAM			0xe
#define ADV7482_SDP_STD_PAL_SECAM_PED			0xf

static int adv7482_sdp_read_ro_map(struct adv7482_state *state, u8 reg)
{
	int ret;

	/* Select SDP Read-Only Main Map */
	ret = sdp_write(state, 0x0e, 0x01);
	if (ret < 0)
		return ret;

	return sdp_read(state, reg);
}

static int adv7482_sdp_status(struct adv7482_state *state, u32 *signal,
			      v4l2_std_id *std)
{
	int info;

	/* Read status from reg 0x10 of SDP RO Map */
	info = adv7482_sdp_read_ro_map(state, 0x10);
	if (info < 0)
		return info;

	if (signal)
		*signal = info & BIT(0) ? 0 : V4L2_IN_ST_NO_SIGNAL;

	if (std) {
		*std = V4L2_STD_UNKNOWN;

		/* Standard not valid if there is no signal */
		if (info & BIT(0)) {
			switch (info & 0x70) {
			case 0x00:
				*std = V4L2_STD_NTSC;
				break;
			case 0x10:
				*std = V4L2_STD_NTSC_443;
				break;
			case 0x20:
				*std = V4L2_STD_PAL_M;
				break;
			case 0x30:
				*std = V4L2_STD_PAL_60;
				break;
			case 0x40:
				*std = V4L2_STD_PAL;
				break;
			case 0x50:
				*std = V4L2_STD_SECAM;
				break;
			case 0x60:
				*std = V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
				break;
			case 0x70:
				*std = V4L2_STD_SECAM;
				break;
			default:
				*std = V4L2_STD_UNKNOWN;
				break;
			}
		}
	}

	return 0;
}

static void adv7482_sdp_fill_format(struct adv7482_state *state,
				    struct v4l2_mbus_framefmt *fmt)
{
	v4l2_std_id std;
	memset(fmt, 0, sizeof(*fmt));

	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->field = V4L2_FIELD_INTERLACED;

	fmt->width = 720;

	if (state->sdp.curr_norm == V4L2_STD_ALL)
		adv7482_sdp_status(state, NULL,  &std);
	else
		std = state->sdp.curr_norm;

	fmt->height = std & V4L2_STD_525_60 ? 480 : 576;
}

static int adv7482_sdp_std(v4l2_std_id std)
{
	if (std == V4L2_STD_ALL)
		return ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM;
	if (std == V4L2_STD_PAL_60)
		return ADV7482_SDP_STD_PAL60;
	if (std == V4L2_STD_NTSC_443)
		return ADV7482_SDP_STD_NTSC_443;
	if (std == V4L2_STD_PAL_N)
		return ADV7482_SDP_STD_PAL_N;
	if (std == V4L2_STD_PAL_M)
		return ADV7482_SDP_STD_PAL_M;
	if (std == V4L2_STD_PAL_Nc)
		return ADV7482_SDP_STD_PAL_COMB_N;
	if (std & V4L2_STD_PAL)
		return ADV7482_SDP_STD_PAL_BG;
	if (std & V4L2_STD_NTSC)
		return ADV7482_SDP_STD_NTSC_M;
	if (std & V4L2_STD_SECAM)
		return ADV7482_SDP_STD_PAL_SECAM;

	return -EINVAL;
}

static int adv7482_sdp_set_video_standard(struct adv7482_state *state,
					  v4l2_std_id std)
{
	int sdpstd;

	sdpstd = adv7482_sdp_std(std);
	if (sdpstd < 0)
		return sdpstd;

	sdp_clrset(state, 0x02, 0xf0, (sdpstd & 0xf) << 4);

	return 0;
}

static int adv7482_g_pixelaspect(struct v4l2_subdev *sd,
				 struct v4l2_fract *aspect)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);
	v4l2_std_id std;

	/* TODO:KPB: Is this still true? */
	/* TODO: this needs to be sink pad aware */

	if (state->sdp.curr_norm == V4L2_STD_ALL)
		adv7482_sdp_status(state, NULL,  &std);
	else
		std = state->sdp.curr_norm;

	if (std & V4L2_STD_525_60) {
		aspect->numerator = 11;
		aspect->denominator = 10;
	} else {
		aspect->numerator = 54;
		aspect->denominator = 59;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_video_ops
 */

static int adv7482_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);

	if (state->sdp.curr_norm == V4L2_STD_ALL)
		adv7482_sdp_status(state, NULL,  norm);
	else
		*norm = state->sdp.curr_norm;

	return 0;
}


static int adv7482_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7482_sdp_set_video_standard(state, std);
	if (ret < 0)
		goto out;

	state->sdp.curr_norm = std;

out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	if (state->sdp.streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Set auto detect mode */
	ret = adv7482_sdp_set_video_standard(state, V4L2_STD_ALL);
	if (ret)
		goto unlock;

	msleep(100);

	/* Read detected standard */
	ret = adv7482_sdp_status(state, NULL, std);
unlock:
	mutex_unlock(&state->mutex);

	return ret;
}

static int adv7482_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	*norm = V4L2_STD_ALL;

	return 0;
}

static int adv7482_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7482_sdp_status(state, status, NULL);

	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);
	int ret, signal = V4L2_IN_ST_NO_SIGNAL;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7482_txb_power(state, enable);
	if (ret)
		goto error;

	state->sdp.streaming = enable;

	adv7482_sdp_status(state, &signal, NULL);
	if (signal != V4L2_IN_ST_NO_SIGNAL)
		adv_dbg(state, "Detected SDP signal\n");
	else
		adv_info(state, "Couldn't detect SDP video signal\n");

error:
	mutex_unlock(&state->mutex);
	return ret;
}

static const struct v4l2_subdev_video_ops adv7482_video_ops_cvbs = {
	.g_std = adv7482_g_std,
	.s_std = adv7482_s_std,
	.querystd = adv7482_querystd,
	.g_tvnorms = adv7482_g_tvnorms,
	.g_input_status = adv7482_g_input_status,
	.s_stream = adv7482_s_stream,
	.g_pixelaspect = adv7482_g_pixelaspect,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 */

static int adv7482_enum_mbus_code_cvbs(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	trace_printk("Subdev: %s, Pad %u (%s)",
			sd->name, code->pad, (code->pad == ADV7482_SOURCE_TXA) ? "HDMI" :
					     (code->pad == ADV7482_SOURCE_TXB) ? "CVBS" : "Other");

	switch (code->pad) {
	case ADV7482_SOURCE_TXB:
		code->code = MEDIA_BUS_FMT_UYVY8_2X8;
		break;
	case ADV7482_SOURCE_TXA:
		/* CVBS does not currently support outputting on TXA */
	default:
		return -EINVAL;
	}

	return 0;
}


static int adv7482_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);


	trace_printk("Subdev: %s, Pad %u (%s)",
			sd->name, format->pad, (format->pad == ADV7482_SOURCE_TXA) ? "HDMI" :
					       (format->pad == ADV7482_SOURCE_TXB) ? "CVBS" : "Other");

	switch (format->pad) {
	case ADV7482_SOURCE_TXB:
		adv7482_sdp_fill_format(state, &format->format);
		break;
	case ADV7482_SOURCE_TXA:
		/* CVBS does not currently support outputting on TXA */
	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		format->format.code = fmt->code;
	}

	return 0;
}

static int adv7482_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7482_state *state = adv7482_cvbs_to_state(sd);

	switch (format->pad) {
	case ADV7482_SOURCE_TXB:
		adv7482_sdp_fill_format(state, &format->format);
		break;
	case ADV7482_SOURCE_TXA:
		/* CVBS does not currently support outputting on TXA */
	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		fmt->code = format->format.code;
	}

	return 0;
}

static const struct v4l2_subdev_pad_ops adv7482_pad_ops_cvbs = {
	.enum_mbus_code = adv7482_enum_mbus_code_cvbs,
	.set_fmt = adv7482_set_pad_format,
	.get_fmt = adv7482_get_pad_format,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops adv7482_ops_cvbs= {
	.video = &adv7482_video_ops_cvbs,
	.pad = &adv7482_pad_ops_cvbs,
};

/* -----------------------------------------------------------------------------
 * Controls
 */

/* Contrast */
#define ADV7482_SDP_REG_CON		0x08	/*Unsigned */
#define ADV7482_SDP_CON_MIN		0
#define ADV7482_SDP_CON_DEF		128
#define ADV7482_SDP_CON_MAX		255
/* Brightness*/
#define ADV7482_SDP_REG_BRI		0x0a	/*Signed */
#define ADV7482_SDP_BRI_MIN		-128
#define ADV7482_SDP_BRI_DEF		0
#define ADV7482_SDP_BRI_MAX		127
/* Hue */
#define ADV7482_SDP_REG_HUE		0x0b	/*Signed, inverted */
#define ADV7482_SDP_HUE_MIN		-127
#define ADV7482_SDP_HUE_DEF		0
#define ADV7482_SDP_HUE_MAX		128

/* Saturation */
#define ADV7482_SDP_REG_SD_SAT_CB	0xe3
#define ADV7482_SDP_REG_SD_SAT_CR	0xe4
#define ADV7482_SDP_SAT_MIN		0
#define ADV7482_SDP_SAT_DEF		128
#define ADV7482_SDP_SAT_MAX		255

static int __adv7482_sdp_s_ctrl(struct v4l2_ctrl *ctrl,
				struct adv7482_state *state)
{
	int ret;

	/*TODO:KPB: What is this non-descript write */
	ret = sdp_write(state, 0x0e, 0x00);
	if (ret < 0)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->val < ADV7482_SDP_BRI_MIN ||
		    ctrl->val > ADV7482_SDP_BRI_MAX)
			return -ERANGE;

		ret = sdp_write(state, ADV7482_SDP_REG_BRI, ctrl->val);
		break;
	case V4L2_CID_HUE:
		if (ctrl->val < ADV7482_SDP_HUE_MIN ||
		    ctrl->val > ADV7482_SDP_HUE_MAX)
			return -ERANGE;

		/*Hue is inverted according to HSL chart */
		ret = sdp_write(state, ADV7482_SDP_REG_HUE, -ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		if (ctrl->val < ADV7482_SDP_CON_MIN ||
		    ctrl->val > ADV7482_SDP_CON_MAX)
			return -ERANGE;

		ret = sdp_write(state, ADV7482_SDP_REG_CON, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		if (ctrl->val < ADV7482_SDP_SAT_MIN ||
		    ctrl->val > ADV7482_SDP_SAT_MAX)
			return -ERANGE;
		/*
		 *This could be V4L2_CID_BLUE_BALANCE/V4L2_CID_RED_BALANCE
		 *Let's not confuse the user, everybody understands saturation
		 */
		ret = sdp_write(state, ADV7482_SDP_REG_SD_SAT_CB, ctrl->val);
		if (ret)
			break;
		ret = sdp_write(state, ADV7482_SDP_REG_SD_SAT_CR, ctrl->val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int adv7482_sdp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct adv7482_state *state =
		container_of(ctrl->handler, struct adv7482_state, sdp.ctrl_hdl);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = __adv7482_sdp_s_ctrl(ctrl, state);

	mutex_unlock(&state->mutex);

	return ret;
}

static int adv7482_sdp_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct adv7482_state *state =
		container_of(ctrl->handler, struct adv7482_state, sdp.ctrl_hdl);
	unsigned int width, height, fps;
	v4l2_std_id std;

	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
		width = 720;
		if (state->sdp.curr_norm == V4L2_STD_ALL)
			adv7482_sdp_status(state, NULL,  &std);
		else
			std = state->sdp.curr_norm;

		height = std & V4L2_STD_525_60 ? 480 : 576;
		fps = std & V4L2_STD_525_60 ? 30 : 25;

		*ctrl->p_new.p_s64 = width * height * fps;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops adv7482_sdp_ctrl_ops = {
	.s_ctrl = adv7482_sdp_s_ctrl,
	.g_volatile_ctrl = adv7482_sdp_g_volatile_ctrl,
};

static int adv7482_sdp_init_controls(struct adv7482_state *state)
{
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&state->sdp.ctrl_hdl, 5);

	v4l2_ctrl_new_std(&state->sdp.ctrl_hdl, &adv7482_sdp_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, ADV7482_SDP_BRI_MIN,
			  ADV7482_SDP_BRI_MAX, 1, ADV7482_SDP_BRI_DEF);
	v4l2_ctrl_new_std(&state->sdp.ctrl_hdl, &adv7482_sdp_ctrl_ops,
			  V4L2_CID_CONTRAST, ADV7482_SDP_CON_MIN,
			  ADV7482_SDP_CON_MAX, 1, ADV7482_SDP_CON_DEF);
	v4l2_ctrl_new_std(&state->sdp.ctrl_hdl, &adv7482_sdp_ctrl_ops,
			  V4L2_CID_SATURATION, ADV7482_SDP_SAT_MIN,
			  ADV7482_SDP_SAT_MAX, 1, ADV7482_SDP_SAT_DEF);
	v4l2_ctrl_new_std(&state->sdp.ctrl_hdl, &adv7482_sdp_ctrl_ops,
			  V4L2_CID_HUE, ADV7482_SDP_HUE_MIN,
			  ADV7482_SDP_HUE_MAX, 1, ADV7482_SDP_HUE_DEF);
	ctrl = v4l2_ctrl_new_std(&state->sdp.ctrl_hdl, &adv7482_sdp_ctrl_ops,
				 V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	state->sdp.sd.ctrl_handler = &state->sdp.ctrl_hdl;
	if (state->sdp.ctrl_hdl.error) {
		v4l2_ctrl_handler_free(&state->sdp.ctrl_hdl);
		return state->sdp.ctrl_hdl.error;
	}

	return v4l2_ctrl_handler_setup(&state->sdp.ctrl_hdl);
}

int adv7482_sdp_probe(struct adv7482_state *state)
{
	unsigned int i;
	int ret;

	state->sdp.streaming = false;
	state->sdp.curr_norm = V4L2_STD_ALL;

	adv7482_subdev_init(&state->sdp.sd, state, &adv7482_ops_cvbs, "cvbs");

	for (i = ADV7482_SINK_HDMI; i < ADV7482_SOURCE_TXA; i++)
		state->sdp.pads[i].flags = MEDIA_PAD_FL_SINK;
	for (i = ADV7482_SOURCE_TXA; i <= ADV7482_SOURCE_TXB; i++)
		state->sdp.pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&state->sdp.sd.entity, ADV7482_PAD_MAX,
				     state->sdp.pads);

	ret = adv7482_sdp_init_controls(state);
	if (ret)
		return ret;

	ret = v4l2_async_register_subdev(&state->sdp.sd);
	if (ret)
		return ret;

	return 0;
}

void adv7482_sdp_remove(struct adv7482_state *state)
{
	v4l2_async_unregister_subdev(&state->sdp.sd);
	media_entity_cleanup(&state->sdp.sd.entity);
	v4l2_ctrl_handler_free(&state->sdp.ctrl_hdl);
}
