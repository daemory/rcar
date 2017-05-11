/*
 * Driver for Analog Devices ADV748X CSI-2 Transmitter
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
#include <media/v4l2-ioctl.h>

#include "adv748x.h"

static bool is_txa(struct adv748x_csi2 *tx)
{
	return (tx == &tx->state->txa);
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_internal_ops
 *
 * We use the internal registered operation to be able to ensure that our
 * incremental subdevices (not connected in the forward path) can be registered
 * against the resulting video path and media device.
 */

static int adv748x_csi2_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct adv748x_csi2 *tx = notifier_to_csi2(notifier);
	struct adv748x_state *state = tx->state;
	int ret;

	ret = v4l2_device_register_subdev_nodes(tx->sd.v4l2_dev);
	if (ret) {
		adv_err(state, "Failed to register subdev nodes\n");
		return ret;
	}

	/* Return early until we register TXB */
	if (is_txa(tx))
		return ret;

	if (!is_media_entity_v4l2_video_device(&state->txa.sd.entity))
		adv_err(state, "TXA is not a v4l2_video device yet");

	if (!is_media_entity_v4l2_video_device(&state->txb.sd.entity))
		adv_err(state, "TXB is not a v4l2_video device yet");

	if (!is_media_entity_v4l2_video_device(&state->hdmi.sd.entity))
		adv_err(state, "HDMI is not a v4l2_video device yet");

	if (!is_media_entity_v4l2_video_device(&state->afe.sd.entity))
		adv_err(state, "AFE is not a v4l2_video device yet");

	ret = adv748x_setup_links(state);
	if (ret) {
		adv_err(state, "Failed to setup entity links");
		return ret;
	}

	return ret;
}

static int adv748x_csi2_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct adv748x_csi2 *tx = notifier_to_csi2(notifier);
	struct adv748x_state *state = tx->state;

	v4l2_set_subdev_hostdata(subdev, tx);

	adv_info(state, "Bind %s -> %s", is_txa(tx)? "TXA":"TXB", subdev->name);

	return 0;
}
static void adv748x_csi2_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct adv748x_csi2 *tx = notifier_to_csi2(notifier);
	struct adv748x_state *state = tx->state;

	adv_info(state, "Unbind %s -> %s", is_txa(tx)? "TXA":"TXB",
			subdev->name);
}

static int adv748x_csi2_registered(struct v4l2_subdev *sd)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	struct adv748x_state *state = tx->state;
	int ret;

	adv_info(state, "Registered %s (%s)", is_txa(tx)? "TXA":"TXB",
			sd->name);

	/*
	 * Register HDMI on TXA, and AFE on TXB.
	 */
	if (is_txa(tx)) {
		tx->subdevs[0].match_type = V4L2_ASYNC_MATCH_FWNODE;
		tx->subdevs[0].match.fwnode.fwnode =
			of_fwnode_handle(state->endpoints[ADV748X_PORT_HDMI]);
	} else {
		/* TODO: This isn't right - Hardwiring to AIN8 ... ???? */
		tx->subdevs[0].match_type = V4L2_ASYNC_MATCH_FWNODE;
		tx->subdevs[0].match.fwnode.fwnode =
			of_fwnode_handle(state->endpoints[ADV748X_PORT_AIN8]);
	}

	tx->subdev_p[0] = &tx->subdevs[0];

	tx->notifier.num_subdevs = 1;
	tx->notifier.subdevs = tx->subdev_p;

	tx->notifier.bound = adv748x_csi2_notify_bound;
	tx->notifier.unbind = adv748x_csi2_notify_unbind;
	tx->notifier.complete = adv748x_csi2_notify_complete;

	ret = v4l2_async_subnotifier_register(&tx->sd, &tx->notifier);
	if (ret < 0) {
		adv_err(state, "Notifier registration failed\n");
		return ret;
	}

	return 0;
}

static void adv748x_csi2_unregistered(struct v4l2_subdev *sd)
{
	struct adv748x_csi2 *tx = container_of(sd, struct adv748x_csi2, sd);

	v4l2_async_subnotifier_unregister(&tx->notifier);
}

static const struct v4l2_subdev_internal_ops adv748x_csi2_internal_ops = {
	.registered = adv748x_csi2_registered,
	.unregistered = adv748x_csi2_unregistered,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 */

static int adv748x_csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static const struct v4l2_subdev_video_ops adv748x_csi2_video_ops = {
	.s_stream = adv748x_csi2_s_stream,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 *
 * The CSI2 bus pads, are ignorant to the data sizes or formats.
 * But we must support setting the pad formats for format propagation.
 * It would be nice if 'pass-through entities' could be handled generically in
 * core
 */

static struct v4l2_mbus_framefmt *
adv748x_csi2_get_pad_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    unsigned int pad, u32 which)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &tx->format;
	default:
		return NULL;
	}
}

static int adv748x_csi2_get_format(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_format *sdformat)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	struct adv748x_state *state = tx->state;
	struct v4l2_mbus_framefmt *mbusformat;

	mbusformat = adv748x_csi2_get_pad_format(sd, cfg, sdformat->pad,
						 sdformat->which);
	if (!mbusformat)
		return -EINVAL;

	mutex_lock(&state->mutex);

	sdformat->format = *mbusformat;

	mutex_unlock(&state->mutex);

	return 0;
}

static int adv748x_csi2_set_format(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_format *sdformat)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	struct adv748x_state *state = tx->state;
	struct media_pad *pad = &tx->pads[sdformat->pad];
	struct v4l2_mbus_framefmt *mbusformat;

	mbusformat = adv748x_csi2_get_pad_format(sd, cfg, sdformat->pad,
						 sdformat->which);
	if (!mbusformat)
		return -EINVAL;

	mutex_lock(&state->mutex);

	if (pad->flags & MEDIA_PAD_FL_SOURCE)
		sdformat->format = tx->format;

	*mbusformat = sdformat->format;

	mutex_unlock(&state->mutex);

	return 0;
}

static const struct v4l2_subdev_pad_ops adv748x_csi2_pad_ops = {
	.get_fmt = adv748x_csi2_get_format,
	.set_fmt = adv748x_csi2_set_format,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops adv748x_csi2_ops = {
	.video = &adv748x_csi2_video_ops,
	.pad = &adv748x_csi2_pad_ops,
};

int adv748x_csi2_probe(struct adv748x_state *state, struct adv748x_csi2 *tx)
{
	struct device_node *ep;
	int ret;

	/* We can not use container_of to get back to the state with two TXs */
	tx->state = state;

	adv748x_subdev_init(&tx->sd, state, &adv748x_csi2_ops,
			is_txa(tx) ? "txa" : "txb");

	ep = state->endpoints[is_txa(tx) ? ADV748X_PORT_TXA : ADV748X_PORT_TXB];

	/* Ensure that matching is based upon the endpoint fwnodes */
	tx->sd.fwnode = of_fwnode_handle(ep);

	/* Register internal ops for Incremental subdev discovery */
	tx->sd.internal_ops = &adv748x_csi2_internal_ops;

	tx->pads[ADV748X_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	tx->pads[ADV748X_CSI2_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&tx->sd.entity, ADV748X_CSI2_NR_PADS,
				     tx->pads);
	if (ret)
		return ret;

	ret = v4l2_async_register_subdev(&tx->sd);
	if (ret)
		return ret;

	return 0;
}


void adv748x_csi2_remove(struct adv748x_csi2 *tx)
{
	v4l2_async_unregister_subdev(&tx->sd);
	media_entity_cleanup(&tx->sd.entity);
}
