/*
 * Driver for Analog Devices ADV748X video decoder and HDMI receiver
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Authors:
 *	Koji Matsuoka <koji.matsuoka.xm@renesas.com>
 *	Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *	Kieran Bingham <kieran.bingham@ideasonboard.com>
 *
 * The ADV748x range of receivers have the following configurations:
 *
 *                  Analog   HDMI  MHL  4-Lane  1-Lane
 *                    In      In         CSI     CSI
 *       ADV7480               X    X     X
 *       ADV7481      X        X    X     X       X
 *       ADV7482      X        X          X       X
 */

#include <linux/i2c.h>

#ifndef _ADV748X_H_
#define _ADV748X_H_

/* I2C slave addresses */
#define ADV748X_I2C_IO			0x70	/* IO Map */
#define ADV748X_I2C_DPLL		0x26	/* DPLL Map */
#define ADV748X_I2C_CP			0x22	/* CP Map */
#define ADV748X_I2C_HDMI		0x34	/* HDMI Map */
#define ADV748X_I2C_EDID		0x36	/* EDID Map */
#define ADV748X_I2C_REPEATER		0x32	/* HDMI RX Repeater Map */
#define ADV748X_I2C_INFOFRAME		0x31	/* HDMI RX InfoFrame Map */
#define ADV748X_I2C_CEC			0x41	/* CEC Map */
#define ADV748X_I2C_SDP			0x79	/* SDP Map */
#define ADV748X_I2C_TXB			0x48	/* CSI-TXB Map */
#define ADV748X_I2C_TXA			0x4a	/* CSI-TXA Map */
#define ADV748X_I2C_WAIT		0xfe	/* Wait x msec */
#define ADV748X_I2C_EOR			0xff	/* End Mark */

/**
 * enum adv748x_ports - Device tree port number definitions
 *
 * The ADV748X ports define the mapping between subdevices
 * and the device tree specification
 */
enum adv748x_ports {
	ADV748X_PORT_HDMI = 0,
	ADV748X_PORT_AIN1 = 1,
	ADV748X_PORT_AIN2 = 2,
	ADV748X_PORT_AIN3 = 3,
	ADV748X_PORT_AIN4 = 4,
	ADV748X_PORT_AIN5 = 5,
	ADV748X_PORT_AIN6 = 6,
	ADV748X_PORT_AIN7 = 7,
	ADV748X_PORT_AIN8 = 8,
	ADV748X_PORT_TTL = 9,
	ADV748X_PORT_TXA = 10,
	ADV748X_PORT_TXB = 11,
	ADV748X_PORT_MAX = 12,
};

enum adv748x_csi2_pads {
	ADV748X_CSI2_SINK,
	ADV748X_CSI2_SOURCE,
	ADV748X_CSI2_NR_PADS,
};

/* CSI2 transmitters can have 2 internal connections, HDMI/AFE */
#define ADV748X_CSI2_MAX_SUBDEVS 2

struct adv748x_csi2 {
	struct adv748x_state *state;
	struct v4l2_mbus_framefmt format;

	struct media_pad pads[ADV748X_CSI2_NR_PADS];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev sd;
};

#define notifier_to_csi2(n) container_of(n, struct adv748x_csi2, notifier)
#define adv748x_sd_to_csi2(sd) container_of(sd, struct adv748x_csi2, sd)

enum adv748x_hdmi_pads {
	ADV748X_HDMI_SINK,
	ADV748X_HDMI_SOURCE,
	ADV748X_HDMI_NR_PADS,
};

struct adv748x_hdmi {
	struct media_pad pads[ADV748X_HDMI_NR_PADS];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev sd;

	struct v4l2_dv_timings timings;
	struct v4l2_fract aspect_ratio;

	struct {
		u8 edid[512];
		u32 present;
		unsigned int blocks;
	} edid;
};

#define adv748x_ctrl_to_hdmi(ctrl) \
	container_of(ctrl->handler, struct adv748x_hdmi, ctrl_hdl)
#define adv748x_sd_to_hdmi(sd) container_of(sd, struct adv748x_hdmi, sd)

enum adv748x_afe_pads {
	ADV748X_AFE_SINK_AIN0,
	ADV748X_AFE_SINK_AIN1,
	ADV748X_AFE_SINK_AIN2,
	ADV748X_AFE_SINK_AIN3,
	ADV748X_AFE_SINK_AIN4,
	ADV748X_AFE_SINK_AIN5,
	ADV748X_AFE_SINK_AIN6,
	ADV748X_AFE_SINK_AIN7,
	ADV748X_AFE_SOURCE,
	ADV748X_AFE_NR_PADS,
};

struct adv748x_afe {
	struct media_pad pads[ADV748X_AFE_NR_PADS];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev sd;

	bool streaming;
	v4l2_std_id curr_norm;
};

#define adv748x_ctrl_to_afe(ctrl) \
	container_of(ctrl->handler, struct adv748x_afe, ctrl_hdl)
#define adv748x_sd_to_afe(sd) container_of(sd, struct adv748x_afe, sd)

/**
 * struct adv748x_state - State of ADV748X
 * @dev:		(OF) device
 * @client:		I2C client
 * @mutex:		protect global state
 * @intrq1		INTRQ1 IRQ number
 * @intrq2		INTRQ2 IRQ number
 * @endpoints:		parsed device node endpoints for each port
 *
 * @hdmi:		state of HDMI receiver context
 * @afe:		state of AFE receiver context
 * @txa:		state of TXA transmitter context
 * @txb:		state of TXB transmitter context
 */
struct adv748x_state {
	struct device *dev;
	struct i2c_client *client;
	struct mutex mutex;

	int intrq1;
	int intrq2;

	struct device_node *endpoints[ADV748X_PORT_MAX];

	struct adv748x_hdmi hdmi;
	struct adv748x_afe afe;

	struct adv748x_csi2 txa;
	struct adv748x_csi2 txb;
};

#define adv748x_hdmi_to_state(h) container_of(h, struct adv748x_state, hdmi)
#define adv748x_afe_to_state(a) container_of(a, struct adv748x_state, afe)

#define adv_err(a, fmt, arg...)	dev_err(a->dev, fmt, ##arg)
#define adv_info(a, fmt, arg...) dev_info(a->dev, fmt, ##arg)
#define adv_dbg(a, fmt, arg...)	dev_dbg(a->dev, fmt, ##arg)

/* Register handling */
int adv748x_read(struct adv748x_state *state, u8 addr, u8 reg);
int adv748x_write(struct adv748x_state *state, u8 addr, u8 reg, u8 value);

#define io_read(s, r) adv748x_read(s, ADV748X_I2C_IO, r)
#define io_write(s, r, v) adv748x_write(s, ADV748X_I2C_IO, r, v)
#define io_clrset(s, r, m, v) io_write(s, r, (io_read(s, r) & ~m) | v)

#define hdmi_read(s, r) adv748x_read(s, ADV748X_I2C_HDMI, r)
#define hdmi_read16(s, r, m) (((hdmi_read(s, r) << 8) | hdmi_read(s, r+1)) & m)
#define hdmi_write(s, r, v) adv748x_write(s, ADV748X_I2C_HDMI, r, v)
#define hdmi_clrset(s, r, m, v) hdmi_write(s, r, (hdmi_read(s, r) & ~m) | v)

#define sdp_read(s, r) adv748x_read(s, ADV748X_I2C_SDP, r)
#define sdp_write(s, r, v) adv748x_write(s, ADV748X_I2C_SDP, r, v)
#define sdp_clrset(s, r, m, v) sdp_write(s, r, (sdp_read(s, r) & ~m) | v)

#define cp_read(s, r) adv748x_read(s, ADV748X_I2C_CP, r)
#define cp_write(s, r, v) adv748x_write(s, ADV748X_I2C_CP, r, v)
#define cp_clrset(s, r, m, v) cp_write(s, r, (cp_read(s, r) & ~m) | v)

#define txa_read(s, r) adv748x_read(s, ADV748X_I2C_TXA, r)
#define txa_write(s, r, v) adv748x_write(s, ADV748X_I2C_TXA, r, v)
#define txa_clrset(s, r, m, v) txa_write(s, r, (txa_read(s, r) & ~m) | v)

#define txb_read(s, r) adv748x_read(s, ADV748X_I2C_TXB, r)
#define txb_write(s, r, v) adv748x_write(s, ADV748X_I2C_TXB, r, v)
#define txb_clrset(s, r, m, v) txb_write(s, r, (txb_read(s, r) & ~m) | v)

void adv748x_subdev_init(struct v4l2_subdev *sd, struct adv748x_state *state,
			 const struct v4l2_subdev_ops *ops, u32 function,
			 const char *ident);

int adv748x_register_subdevs(struct adv748x_state *state,
			     struct v4l2_device *v4l2_dev);

int adv748x_txa_power(struct adv748x_state *state, bool on);
int adv748x_txb_power(struct adv748x_state *state, bool on);

int adv748x_afe_init(struct adv748x_afe *afe);
void adv748x_afe_cleanup(struct adv748x_afe *afe);

int adv748x_csi2_init(struct adv748x_state *state, struct adv748x_csi2 *tx);
void adv748x_csi2_cleanup(struct adv748x_csi2 *tx);

int adv748x_hdmi_init(struct adv748x_hdmi *hdmi);
void adv748x_hdmi_cleanup(struct adv748x_hdmi *hdmi);

#endif /* _ADV748X_H_ */
