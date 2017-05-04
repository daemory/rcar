/*
 * Driver for Analog Devices ADV748X HDMI receiver with AFE
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

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>

#include "adv748x.h"

/* -----------------------------------------------------------------------------
 * Register manipulation
 */

/**
 * struct adv748x_reg_value - Register write instruction
 * @addr:		I2C slave address
 * @reg:		I2c register
 * @value:		value to write to @addr at @reg
 */
struct adv748x_reg_value {
	u8 addr;
	u8 reg;
	u8 value;
};

static int adv748x_write_regs(struct adv748x_state *state,
			      const struct adv748x_reg_value *regs)
{
	struct i2c_msg msg;
	u8 data_buf[2];
	int ret = -EINVAL;

	if (!state->client->adapter) {
		adv_err(state, "No adapter for regs write\n");
		return -ENODEV;
	}

	msg.flags = 0;
	msg.len = 2;
	msg.buf = &data_buf[0];

	while (regs->addr != ADV748X_I2C_EOR) {

		if (regs->addr == ADV748X_I2C_WAIT)
			msleep(regs->value);
		else {
			msg.addr = regs->addr;
			data_buf[0] = regs->reg;
			data_buf[1] = regs->value;

			ret = i2c_transfer(state->client->adapter, &msg, 1);
			if (ret < 0) {
				adv_err(state,
					"Error regs addr: 0x%02x reg: 0x%02x\n",
					regs->addr, regs->reg);
				break;
			}
		}
		regs++;
	}

	return (ret < 0) ? ret : 0;
}

int adv748x_write(struct adv748x_state *state, u8 addr, u8 reg, u8 value)
{
	struct adv748x_reg_value regs[2];
	int ret;

	regs[0].addr = addr;
	regs[0].reg = reg;
	regs[0].value = value;
	regs[1].addr = ADV748X_I2C_EOR;
	regs[1].reg = 0xFF;
	regs[1].value = 0xFF;

	ret = adv748x_write_regs(state, regs);

	return ret;
}

int adv748x_read(struct adv748x_state *state, u8 addr, u8 reg)
{
	struct i2c_msg msg[2];
	u8 reg_buf, data_buf;
	int ret;

	if (!state->client->adapter) {
		adv_err(state, "No adapter reading addr: 0x%02x reg: 0x%02x\n",
			addr, reg);
		return -ENODEV;
	}

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg_buf;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &data_buf;

	reg_buf = reg;

	ret = i2c_transfer(state->client->adapter, msg, 2);
	if (ret < 0) {
		trace_printk("Error reading addr: 0x%02x reg: 0x%02x\n",
			addr, reg);
		adv_err(state, "Error reading addr: 0x%02x reg: 0x%02x\n",
			addr, reg);
		return ret;
	}

	return data_buf;
}

/* -----------------------------------------------------------------------------
 * TXA and TXB
 */

static const struct adv748x_reg_value adv748x_power_up_txa_4lane[] = {

	{ADV748X_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV748X_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */

	{ADV748X_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV748X_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV748X_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv748x_reg_value adv748x_power_down_txa_4lane[] = {

	{ADV748X_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0x1E, 0x00},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV748X_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV748X_I2C_TXA, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv748x_reg_value adv748x_power_up_txb_1lane[] = {

	{ADV748X_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV748X_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */

	{ADV748X_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV748X_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV748X_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv748x_reg_value adv748x_power_down_txb_1lane[] = {

	{ADV748X_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0x1E, 0x00},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0x00, 0x81},	/* Enable 4-lane MIPI */
	{ADV748X_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV748X_I2C_TXB, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

int adv748x_txa_power(struct adv748x_state *state, bool on)
{
	int val, ret;

	val = txa_read(state, 0x1e);
	if (val < 0)
		return val;

	if (on && ((val & 0x40) == 0))
		ret = adv748x_write_regs(state, adv748x_power_up_txa_4lane);
	else
		ret = adv748x_write_regs(state, adv748x_power_down_txa_4lane);

	return ret;
}

int adv748x_txb_power(struct adv748x_state *state, bool on)
{
	int val, ret;

	val = txb_read(state, 0x1e);
	if (val < 0)
		return val;

	if (on && ((val & 0x40) == 0))
		ret = adv748x_write_regs(state, adv748x_power_up_txb_1lane);
	else
		ret = adv748x_write_regs(state, adv748x_power_down_txb_1lane);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations adv748x_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * HW setup
 */

static const struct adv748x_reg_value adv748x_sw_reset[] = {

	{ADV748X_I2C_IO, 0xFF, 0xFF},	/* SW reset */
	{ADV748X_I2C_WAIT, 0x00, 0x05},	/* delay 5 */
	{ADV748X_I2C_IO, 0x01, 0x76},	/* ADI Required Write */
	{ADV748X_I2C_IO, 0xF2, 0x01},	/* Enable I2C Read Auto-Increment */
	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv748x_reg_value adv748x_set_slave_address[] = {
	{ADV748X_I2C_IO, 0xF3, ADV748X_I2C_DPLL << 1},	/* DPLL */
	{ADV748X_I2C_IO, 0xF4, ADV748X_I2C_CP << 1},	/* CP */
	{ADV748X_I2C_IO, 0xF5, ADV748X_I2C_HDMI << 1},	/* HDMI */
	{ADV748X_I2C_IO, 0xF6, ADV748X_I2C_EDID << 1},	/* EDID */
	{ADV748X_I2C_IO, 0xF7, ADV748X_I2C_REPEATER << 1}, /* HDMI RX Repeater */
	{ADV748X_I2C_IO, 0xF8, ADV748X_I2C_INFOFRAME << 1},/* HDMI RX InfoFrame*/
	{ADV748X_I2C_IO, 0xFA, ADV748X_I2C_CEC << 1},	/* CEC */
	{ADV748X_I2C_IO, 0xFB, ADV748X_I2C_SDP << 1},	/* SDP */
	{ADV748X_I2C_IO, 0xFC, ADV748X_I2C_TXB << 1},	/* CSI-TXB */
	{ADV748X_I2C_IO, 0xFD, ADV748X_I2C_TXA << 1},	/* CSI-TXA */
	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* Supported Formats For Script Below */
/* - 01-29 HDMI to MIPI TxA CSI 4-Lane - RGB888: */
static const struct adv748x_reg_value adv748x_init_txa_4lane[] = {
	/* Disable chip powerdown & Enable HDMI Rx block */
	{ADV748X_I2C_IO, 0x00, 0x40},

	{ADV748X_I2C_REPEATER, 0x40, 0x83}, /* Enable HDCP 1.1 */

	{ADV748X_I2C_HDMI, 0x00, 0x08},	/* Foreground Channel = A */
	{ADV748X_I2C_HDMI, 0x98, 0xFF},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x99, 0xA3},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x9A, 0x00},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x9B, 0x0A},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x9D, 0x40},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0xCB, 0x09},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x3D, 0x10},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x3E, 0x7B},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x3F, 0x5E},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x4E, 0xFE},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x4F, 0x18},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x57, 0xA3},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x58, 0x04},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0x85, 0x10},	/* ADI Required Write */

	{ADV748X_I2C_HDMI, 0x83, 0x00},	/* Enable All Terminations */
	{ADV748X_I2C_HDMI, 0xA3, 0x01},	/* ADI Required Write */
	{ADV748X_I2C_HDMI, 0xBE, 0x00},	/* ADI Required Write */

	{ADV748X_I2C_HDMI, 0x6C, 0x01},	/* HPA Manual Enable */
	{ADV748X_I2C_HDMI, 0xF8, 0x01},	/* HPA Asserted */
	{ADV748X_I2C_HDMI, 0x0F, 0x00},	/* Audio Mute Speed Set to Fastest */
	/* (Smallest Step Size) */

	{ADV748X_I2C_IO, 0x04, 0x02},	/* RGB Out of CP */
	{ADV748X_I2C_IO, 0x12, 0xF0},	/* CSC Depends on ip Packets, SDR 444 */
	{ADV748X_I2C_IO, 0x17, 0x80},	/* Luma & Chroma can reach 254d */
	{ADV748X_I2C_IO, 0x03, 0x86},	/* CP-Insert_AV_Code */

	{ADV748X_I2C_CP, 0x7C, 0x00},	/* ADI Required Write */

	{ADV748X_I2C_IO, 0x0C, 0xE0},	/* Enable LLC_DLL & Double LLC Timing */
	{ADV748X_I2C_IO, 0x0E, 0xDD},	/* LLC/PIX/SPI PINS TRISTATED AUD */
	/* Outputs Enabled */
	{ADV748X_I2C_IO, 0x10, 0xA0},	/* Enable 4-lane CSI Tx & Pixel Port */

	{ADV748X_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV748X_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */
	{ADV748X_I2C_TXA, 0xDB, 0x10},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0xD6, 0x07},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0x71, 0x33},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0x72, 0x11},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */

	{ADV748X_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV748X_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV748X_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV748X_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV748X_I2C_CP, 0x37, 0x81},	/* Output Colorbars Pattern */
#endif
	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* TODO:KPB: Need to work out how to provide AFE port select! More entities? */
#define ADV748X_SDP_INPUT_CVBS_AIN8 0x07

/* 02-01 Analog CVBS to MIPI TX-B CSI 1-Lane - */
/* Autodetect CVBS Single Ended In Ain 1 - MIPI Out */
static const struct adv748x_reg_value adv748x_init_txb_1lane[] = {

	{ADV748X_I2C_IO, 0x00, 0x30},  /* Disable chip powerdown powerdown Rx */
	{ADV748X_I2C_IO, 0xF2, 0x01},  /* Enable I2C Read Auto-Increment */

	{ADV748X_I2C_IO, 0x0E, 0xFF},  /* LLC/PIX/AUD/SPI PINS TRISTATED */

	{ADV748X_I2C_SDP, 0x0f, 0x00}, /* Exit Power Down Mode */
	{ADV748X_I2C_SDP, 0x52, 0xCD},/* ADI Required Write */
	/* TODO: do not use hard codeded INSEL */
	{ADV748X_I2C_SDP, 0x00, ADV748X_SDP_INPUT_CVBS_AIN8},
	{ADV748X_I2C_SDP, 0x0E, 0x80},	/* ADI Required Write */
	{ADV748X_I2C_SDP, 0x9C, 0x00},	/* ADI Required Write */
	{ADV748X_I2C_SDP, 0x9C, 0xFF},	/* ADI Required Write */
	{ADV748X_I2C_SDP, 0x0E, 0x00},	/* ADI Required Write */

	/* ADI recommended writes for improved video quality */
	{ADV748X_I2C_SDP, 0x80, 0x51},	/* ADI Required Write */
	{ADV748X_I2C_SDP, 0x81, 0x51},	/* ADI Required Write */
	{ADV748X_I2C_SDP, 0x82, 0x68},	/* ADI Required Write */

	{ADV748X_I2C_SDP, 0x03, 0x42},  /* Tri-S Output , PwrDwn 656 pads */
	{ADV748X_I2C_SDP, 0x04, 0xB5},	/* ITU-R BT.656-4 compatible */
	{ADV748X_I2C_SDP, 0x13, 0x00},	/* ADI Required Write */

	{ADV748X_I2C_SDP, 0x17, 0x41},	/* Select SH1 */
	{ADV748X_I2C_SDP, 0x31, 0x12},	/* ADI Required Write */
	{ADV748X_I2C_SDP, 0xE6, 0x4F},  /* V bit end pos manually in NTSC */

	/* TODO: Convert this to a control option */
#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV748X_I2C_SDP, 0x0C, 0x01},	/* ColorBar */
	{ADV748X_I2C_SDP, 0x14, 0x01},	/* ColorBar */
#endif
	/* Enable 1-Lane MIPI Tx, */
	/* enable pixel output and route SD through Pixel port */
	{ADV748X_I2C_IO, 0x10, 0x70},

	{ADV748X_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV748X_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */
	{ADV748X_I2C_TXB, 0xD2, 0x40},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0x71, 0x33},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0x72, 0x11},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */
	{ADV748X_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV748X_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */

	{ADV748X_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV748X_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV748X_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV748X_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV748X_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static int adv748x_reset(struct adv748x_state *state)
{
	int ret;

	ret = adv748x_write_regs(state, adv748x_sw_reset);
	if (ret < 0)
		return ret;

	ret = adv748x_write_regs(state, adv748x_set_slave_address);
	if (ret < 0)
		return ret;

	/* Init and power down TXA */
	ret = adv748x_write_regs(state, adv748x_init_txa_4lane);
	if (ret)
		return ret;

	adv748x_txa_power(state, 0);
	/* Set VC 0 */
	txa_clrset(state, 0x0d, 0xc0, 0x00);

	/* Init and power down TXB */
	ret = adv748x_write_regs(state, adv748x_init_txb_1lane);
	if (ret)
		return ret;

	adv748x_txb_power(state, 0);

	/* Set VC 0 */
	txb_clrset(state, 0x0d, 0xc0, 0x00);

	/* Disable chip powerdown & Enable HDMI Rx block */
	io_write(state, 0x00, 0x40);

	/* Enable 4-lane CSI Tx & Pixel Port */
	io_write(state, 0x10, 0xe0);

	/* Use vid_std and v_freq as freerun resolution for CP */
	cp_clrset(state, 0xc9, 0x01, 0x01);

	return 0;
}

static int adv748x_print_info(struct adv748x_state *state)
{
	int msb, lsb;

	lsb = io_read(state, 0xdf);
	msb = io_read(state, 0xe0);

	if (lsb < 0 || msb < 0) {
		adv_err(state, "Failed to read chip revision\n");
		return -EIO;
	}

	adv_info(state, "chip found @ 0x%02x revision %02x%02x\n",
		 state->client->addr << 1, lsb, msb);

	return 0;
}

/* -----------------------------------------------------------------------------
 * i2c driver
 */

void adv748x_subdev_init(struct v4l2_subdev *sd, struct adv748x_state *state,
		const struct v4l2_subdev_ops *ops, const char *ident)
{
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = state->dev->driver->owner;
	sd->dev = state->dev;

	v4l2_set_subdevdata(sd, state);

	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x %s",
		state->dev->driver->name,
		i2c_adapter_id(state->client->adapter),
		state->client->addr, ident);

	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;
	sd->entity.ops = &adv748x_media_ops;
}

static int adv748x_parse_dt(struct adv748x_state *state)
{
	struct device_node *ep_np = NULL;
	struct of_endpoint ep;
	unsigned int found = 0;
	int ret;

	for_each_endpoint_of_node(state->dev->of_node, ep_np) {
		ret = of_graph_parse_endpoint(ep_np, &ep);
		if (!ret) {
			adv_info(state, "Endpoint %s on port %d",
					of_node_full_name(ep.local_node),
					ep.port);

			if (ep.port > ADV748X_PORT_MAX) {
				adv_err(state, "Invalid endpoint %s on port %d",
					of_node_full_name(ep.local_node),
					ep.port);

				continue;
			}

			state->endpoints[ep.port] = ep_np;
			found++;
		}
	}

	return found ? 0 : -ENODEV;
}

int adv748x_setup_links(struct adv748x_state *state)
{
	int ret;
	int enabled = MEDIA_LNK_FL_ENABLED;

/*
 * HACK/Workaround:
 *
 * Currently non-immutable link resets go through the RVin
 * driver, and cause the links to fail, due to not being part of RVIN.
 * As a temporary workaround until the RVIN driver knows better than to parse
 * links that do not belong to it, use static immutable links for our internal
 * media paths.
 */
#define ADV748x_DEV_STATIC_LINKS
#ifdef ADV748x_DEV_STATIC_LINKS
	enabled |= MEDIA_LNK_FL_IMMUTABLE;
#endif

	/* TXA - Default link is with HDMI */
	ret = media_create_pad_link(&state->hdmi.sd.entity, 1,
				    &state->txa.sd.entity, 0, enabled);
	if (ret) {
		adv_err(state, "Failed to create HDMI-TXA pad links");
		return ret;
	}

#ifndef ADV748x_DEV_STATIC_LINKS
	ret = media_create_pad_link(&state->afe.sd.entity, ADV748X_AFE_SOURCE,
				    &state->txa.sd.entity, 0, 0);
	if (ret) {
		adv_err(state, "Failed to create AFE-TXA pad links");
		return ret;
	}
#endif

	/* TXB - Can only output from the AFE */
	ret = media_create_pad_link(&state->afe.sd.entity, ADV748X_AFE_SOURCE,
				    &state->txb.sd.entity, 0, enabled);
	if (ret) {
		adv_err(state, "Failed to create AFE-TXB pad links");
		return ret;
	}

	return 0;
}

static int adv748x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adv748x_state *state;
	struct device_node *ep;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = devm_kzalloc(&client->dev, sizeof(struct adv748x_state),
			     GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->mutex);

	state->dev = &client->dev;
	state->client = client;
	i2c_set_clientdata(client, state);

	/* SW reset ADV748X to its default values */
	ret = adv748x_reset(state);
	if (ret) {
		adv_err(state, "Failed to reset hardware");
		return ret;
	}

	ret = adv748x_print_info(state);
	if (ret)
		return ret;

	/* Discover and process ports declared by the Device tree endpoints */
	ret = adv748x_parse_dt(state);
	if (ret)
		return ret;

	/* Process HDMI/TXA */
	if (state->endpoints[ADV748X_PORT_HDMI]) {
		ep = state->endpoints[ADV748X_PORT_HDMI];
		adv_info(state, "Got TXA/HDMI EP: %s\n", of_node_full_name(ep));

		/* Initialise HDMI */
		ret = adv748x_hdmi_probe(state, ep);
		if (ret) {
			adv_err(state, "Failed to probe HDMI");
			return ret;
		}
	} else {
		adv_err(state, "No HDMI endpoint found at %s",
				of_node_full_name(state->dev->of_node));
	}

	/* Process AFE/TXB */
	/* TODO: EEP - HARDWARED AFE */
	if (state->endpoints[ADV748X_PORT_AIN8]) {
		ep = state->endpoints[ADV748X_PORT_AIN8];
		adv_info(state, "Got TXB/AFE EP: %s\n", of_node_full_name(ep));

		/* Initialise AFE */
		ret = adv748x_afe_probe(state, ep);
		if (ret) {
			adv_err(state, "Failed to probe AFE");
			return ret;
		}
	} else {
		/* if has_afe(state) */
		adv_err(state, "No AFE endpoint found at %s",
				of_node_full_name(state->dev->of_node));
	}


	/* Initialise TXA */
	ret = adv748x_csi2_probe(state, &state->txa);
	if (ret) {
		adv_err(state, "Failed to probe TXA");
		return ret;
	}

	/* Initialise TXB  (Not 7480) */
	ret = adv748x_csi2_probe(state, &state->txb);
	if (ret) {
		adv_err(state, "Failed to probe TXB");
		return ret;
	}

	return 0;
}

static int adv748x_remove(struct i2c_client *client)
{
	struct adv748x_state *state = i2c_get_clientdata(client);

	adv748x_afe_remove(state);
	adv748x_hdmi_remove(state);

	adv748x_csi2_remove(&state->txa);
	adv748x_csi2_remove(&state->txb);

	mutex_destroy(&state->mutex);

	return 0;
}

static const struct i2c_device_id adv748x_id[] = {
	{ "adv7481", 0 },
	{ "adv7482", 0 },
	{ },
};

static const struct of_device_id adv748x_of_table[] = {
	{ .compatible = "adi,adv7481", },
	{ .compatible = "adi,adv7482", },
	{ }
};
MODULE_DEVICE_TABLE(of, adv748x_of_table);

static struct i2c_driver adv748x_driver = {
	.driver = {
		.name = "adv748x",
		.of_match_table = of_match_ptr(adv748x_of_table),
	},
	.probe = adv748x_probe,
	.remove = adv748x_remove,
	.id_table = adv748x_id,
};

module_i2c_driver(adv748x_driver);

MODULE_AUTHOR("Kieran Bingham <kieran.bingham@ideasonboard.com>");
MODULE_DESCRIPTION("ADV748X video decoder");
MODULE_LICENSE("GPL v2");
