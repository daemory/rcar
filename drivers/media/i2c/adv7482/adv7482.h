/*
 * Driver for Analog Devices ADV7482 video decoder and HDMI receiver
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
 */

#include <linux/i2c.h>

#ifndef _ADV7482_H_
#define _ADV7482_H_

/* I2C slave addresses */
#define ADV7482_I2C_IO			0x70	/* IO Map */
#define ADV7482_I2C_DPLL		0x26	/* DPLL Map */
#define ADV7482_I2C_CP			0x22	/* CP Map */
#define ADV7482_I2C_HDMI		0x34	/* HDMI Map */
#define ADV7482_I2C_EDID		0x36	/* EDID Map */
#define ADV7482_I2C_REPEATER		0x32	/* HDMI RX Repeater Map */
#define ADV7482_I2C_INFOFRAME		0x31	/* HDMI RX InfoFrame Map */
#define ADV7482_I2C_CEC			0x41	/* CEC Map */
#define ADV7482_I2C_SDP			0x79	/* SDP Map */
#define ADV7482_I2C_TXB			0x48	/* CSI-TXB Map */
#define ADV7482_I2C_TXA			0x4A	/* CSI-TXA Map */
#define ADV7482_I2C_WAIT		0xFE	/* Wait x mesec */
#define ADV7482_I2C_EOR			0xFF	/* End Mark */

enum adv7482_pages {
	ADV7482_PAGE_IO,
	ADV7482_PAGE_DPLL,
	ADV7482_PAGE_CP,
	ADV7482_PAGE_HDMI,
	ADV7482_PAGE_EDID,
	ADV7482_PAGE_REPEATER,
	ADV7482_PAGE_INFOFRAME,
	ADV7482_PAGE_CEC,
	ADV7482_PAGE_SDP,
	ADV7482_PAGE_TXB,
	ADV7482_PAGE_TXA,
	ADV7482_PAGE_MAX,
};

/**
 * enum adv7482_ports - Device tree port number definitions
 *
 * The ADV7482 ports define the mapping between subdevices
 * and the device tree specification
 */
enum adv7482_ports {
	ADV7482_PORT_HDMI = 0,
	ADV7482_PORT_AIN1 = 1,
	ADV7482_PORT_AIN2 = 2,
	ADV7482_PORT_AIN3 = 3,
	ADV7482_PORT_AIN4 = 4,
	ADV7482_PORT_AIN5 = 5,
	ADV7482_PORT_AIN6 = 6,
	ADV7482_PORT_AIN7 = 7,
	ADV7482_PORT_AIN8 = 8,
	ADV7482_PORT_TTL = 9,
	ADV7482_PORT_TXA = 10,
	ADV7482_PORT_TXB = 11,
};

/**
 * struct adv7482_hdmi_cp - State of HDMI CP sink
 * @timings:		Timings for {g,s}_dv_timings
 */
struct adv7482_hdmi_cp {
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_dv_timings timings;
	struct v4l2_subdev sd;
	struct media_pad pads[2];
};

/**
 * struct adv7482_sdp - State of SDP sink
 * @streaming:		Flag if SDP is currently streaming
 * @curr_norm:		Current video standard
 */
struct adv7482_sdp {
	struct media_pad pads[2];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev sd;

	bool streaming;
	v4l2_std_id curr_norm;
};

/**
 * struct adv7482_state - State of ADV7482
 * @dev:		(OF) device
 * @client:		I2C client
 * @sd:			v4l2 subdevice
 * @mutex:		protect against sink state changes while streaming
 *
 * @pads:		media pads exposed
 *
 * @cp:			state of CP HDMI
 * @sdp:		state of SDP
 *
 * @ctrl_hdl		control handler
 */
struct adv7482_state {
	struct device *dev;
	struct i2c_client *client;

	/* i2c clients */
	struct i2c_client *clients[ADV7482_PAGE_MAX];

	/* Regmaps */
	struct regmap *regmap[ADV7482_PAGE_MAX];

	struct mutex mutex;

	struct adv7482_hdmi_cp cp;
	struct adv7482_sdp sdp;
};

#define adv7482_hdmi_to_state(a) container_of(a, struct adv7482_state, cp.sd)
#define adv7482_sdp_to_state(a) container_of(a, struct adv7482_state, sdp.sd)

#define adv_err(a, fmt, arg...)	dev_err(a->dev, fmt, ##arg)
#define adv_info(a, fmt, arg...) dev_info(a->dev, fmt, ##arg)
#define adv_dbg(a, fmt, arg...)	dev_dbg(a->dev, fmt, ##arg)

/* Register handling */
int adv7482_read(struct adv7482_state *state, u8 addr, u8 reg);
int adv7482_write(struct adv7482_state *state, u8 addr, u8 reg, u8 value);

#define io_read(s, r) adv7482_read(s, ADV7482_I2C_IO, r)
#define io_write(s, r, v) adv7482_write(s, ADV7482_I2C_IO, r, v)
#define io_clrset(s, r, m, v) io_write(s, r, (io_read(s, r) & ~m) | v)

#define hdmi_read(s, r) adv7482_read(s, ADV7482_I2C_HDMI, r)
#define hdmi_read16(s, r, m) (((hdmi_read(s, r) << 8) | hdmi_read(s, r+1)) & m)
#define hdmi_write(s, r, v) adv7482_write(s, ADV7482_I2C_HDMI, r, v)
#define hdmi_clrset(s, r, m, v) hdmi_write(s, r, (hdmi_read(s, r) & ~m) | v)

#define sdp_read(s, r) adv7482_read(s, ADV7482_I2C_SDP, r)
#define sdp_write(s, r, v) adv7482_write(s, ADV7482_I2C_SDP, r, v)
#define sdp_clrset(s, r, m, v) sdp_write(s, r, (sdp_read(s, r) & ~m) | v)

#define cp_read(s, r) adv7482_read(s, ADV7482_I2C_CP, r)
#define cp_write(s, r, v) adv7482_write(s, ADV7482_I2C_CP, r, v)
#define cp_clrset(s, r, m, v) cp_write(s, r, (cp_read(s, r) & ~m) | v)

#define txa_read(s, r) adv7482_read(s, ADV7482_I2C_TXA, r)
#define txa_write(s, r, v) adv7482_write(s, ADV7482_I2C_TXA, r, v)
#define txa_clrset(s, r, m, v) txa_write(s, r, (txa_read(s, r) & ~m) | v)

#define txb_read(s, r) adv7482_read(s, ADV7482_I2C_TXB, r)
#define txb_write(s, r, v) adv7482_write(s, ADV7482_I2C_TXB, r, v)
#define txb_clrset(s, r, m, v) txb_write(s, r, (txb_read(s, r) & ~m) | v)

void adv7482_subdev_init(struct v4l2_subdev *sd, struct adv7482_state *state,
		const struct v4l2_subdev_ops *ops, const char * ident);

int adv7482_cp_probe(struct adv7482_state *state);
void adv7482_cp_remove(struct adv7482_state *state);

int adv7482_sdp_probe(struct adv7482_state *state);
void adv7482_sdp_remove(struct adv7482_state *state);

int adv7482_txa_power(struct adv7482_state *state, bool on);
int adv7482_txb_power(struct adv7482_state *state, bool on);

#endif /* _ADV7482_H_ */
