/*
 * Driver for Analog Devices ADV7482 HDMI receiver
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Authors:
 *	Koji Matsuoka
 *	Niklas Söderlund
 *	Kieran Bingham
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
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
 * Register manipulation
 */

/**
 * struct adv7482_reg_value - Register write instruction
 * @addr:		I2C slave address
 * @reg:		I2c register
 * @value:		value to write to @addr at @reg
 */
struct adv7482_reg_value {
	u8 addr;
	u8 reg;
	u8 value;
};

static int adv7482_write_regs(struct adv7482_state *state,
			      const struct adv7482_reg_value *regs)
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

	while (regs->addr != ADV7482_I2C_EOR) {

		if (regs->addr == ADV7482_I2C_WAIT)
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

int adv7482_write(struct adv7482_state *state, u8 addr, u8 reg, u8 value)
{
	struct adv7482_reg_value regs[2];
	int ret;

	regs[0].addr = addr;
	regs[0].reg = reg;
	regs[0].value = value;
	regs[1].addr = ADV7482_I2C_EOR;
	regs[1].reg = 0xFF;
	regs[1].value = 0xFF;

	ret = adv7482_write_regs(state, regs);

	return ret;
}

int adv7482_read(struct adv7482_state *state, u8 addr, u8 reg)
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

static const struct adv7482_reg_value adv7482_power_up_txa_4lane[] = {

	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_txa_4lane[] = {

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_TXA, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_up_txb_1lane[] = {

	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV7482_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */

	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_txb_1lane[] = {

	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_TXB, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

int adv7482_txa_power(struct adv7482_state *state, bool on)
{
	int val, ret;

	val = txa_read(state, 0x1e);
	if (val < 0)
		return val;

	if (on && ((val & 0x40) == 0))
		ret = adv7482_write_regs(state, adv7482_power_up_txa_4lane);
	else
		ret = adv7482_write_regs(state, adv7482_power_down_txa_4lane);

	return ret;
}

int adv7482_txb_power(struct adv7482_state *state, bool on)
{
	int val, ret;

	val = txb_read(state, 0x1e);
	if (val < 0)
		return val;

	if (on && ((val & 0x40) == 0))
		ret = adv7482_write_regs(state, adv7482_power_up_txb_1lane);
	else
		ret = adv7482_write_regs(state, adv7482_power_down_txb_1lane);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations adv7482_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * HW setup
 */

static const struct adv7482_reg_value adv7482_sw_reset[] = {

	{ADV7482_I2C_IO, 0xFF, 0xFF},	/* SW reset */
	{ADV7482_I2C_WAIT, 0x00, 0x05},	/* delay 5 */
	{ADV7482_I2C_IO, 0x01, 0x76},	/* ADI Required Write */
	{ADV7482_I2C_IO, 0xF2, 0x01},	/* Enable I2C Read Auto-Increment */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_slave_address[] = {
	{ADV7482_I2C_IO, 0xF3, ADV7482_I2C_DPLL * 2},	/* DPLL */
	{ADV7482_I2C_IO, 0xF4, ADV7482_I2C_CP * 2},	/* CP */
	{ADV7482_I2C_IO, 0xF5, ADV7482_I2C_HDMI * 2},	/* HDMI */
	{ADV7482_I2C_IO, 0xF6, ADV7482_I2C_EDID * 2},	/* EDID */
	{ADV7482_I2C_IO, 0xF7, ADV7482_I2C_REPEATER * 2}, /* HDMI RX Repeater */
	{ADV7482_I2C_IO, 0xF8, ADV7482_I2C_INFOFRAME * 2},/* HDMI RX InfoFrame*/
	{ADV7482_I2C_IO, 0xFA, ADV7482_I2C_CEC * 2},	/* CEC */
	{ADV7482_I2C_IO, 0xFB, ADV7482_I2C_SDP * 2},	/* SDP */
	{ADV7482_I2C_IO, 0xFC, ADV7482_I2C_TXB * 2},	/* CSI-TXB */
	{ADV7482_I2C_IO, 0xFD, ADV7482_I2C_TXA * 2},	/* CSI-TXA */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* Supported Formats For Script Below */
/* - 01-29 HDMI to MIPI TxA CSI 4-Lane - RGB888: */
static const struct adv7482_reg_value adv7482_init_txa_4lane[] = {
	/* Disable chip powerdown & Enable HDMI Rx block */
	{ADV7482_I2C_IO, 0x00, 0x40},

	{ADV7482_I2C_REPEATER, 0x40, 0x83}, /* Enable HDCP 1.1 */

	{ADV7482_I2C_HDMI, 0x00, 0x08},	/* Foreground Channel = A */
	{ADV7482_I2C_HDMI, 0x98, 0xFF},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x99, 0xA3},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9A, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9B, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9D, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0xCB, 0x09},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3D, 0x10},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3E, 0x7B},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3F, 0x5E},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x4E, 0xFE},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x4F, 0x18},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x57, 0xA3},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x58, 0x04},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x85, 0x10},	/* ADI Required Write */

	{ADV7482_I2C_HDMI, 0x83, 0x00},	/* Enable All Terminations */
	{ADV7482_I2C_HDMI, 0xA3, 0x01},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0xBE, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_HDMI, 0x6C, 0x01},	/* HPA Manual Enable */
	{ADV7482_I2C_HDMI, 0xF8, 0x01},	/* HPA Asserted */
	{ADV7482_I2C_HDMI, 0x0F, 0x00},	/* Audio Mute Speed Set to Fastest */
	/* (Smallest Step Size) */

	{ADV7482_I2C_IO, 0x04, 0x02},	/* RGB Out of CP */
	{ADV7482_I2C_IO, 0x12, 0xF0},	/* CSC Depends on ip Packets, SDR 444 */
	{ADV7482_I2C_IO, 0x17, 0x80},	/* Luma & Chroma can reach 254d */
	{ADV7482_I2C_IO, 0x03, 0x86},	/* CP-Insert_AV_Code */

	{ADV7482_I2C_CP, 0x7C, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_IO, 0x0C, 0xE0},	/* Enable LLC_DLL & Double LLC Timing */
	{ADV7482_I2C_IO, 0x0E, 0xDD},	/* LLC/PIX/SPI PINS TRISTATED AUD */
	/* Outputs Enabled */
	{ADV7482_I2C_IO, 0x10, 0xA0},	/* Enable 4-lane CSI Tx & Pixel Port */

	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */
	{ADV7482_I2C_TXA, 0xDB, 0x10},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xD6, 0x07},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x71, 0x33},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x72, 0x11},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV7482_I2C_CP, 0x37, 0x81},	/* Output Colorbars Pattern */
#endif
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* TODO:KPB: This may be 'private' to CVBS?, and is currently duplicated! */
#define ADV7482_SDP_INPUT_CVBS_AIN8 0x07

/* 02-01 Analog CVBS to MIPI TX-B CSI 1-Lane - */
/* Autodetect CVBS Single Ended In Ain 1 - MIPI Out */
static const struct adv7482_reg_value adv7482_init_txb_1lane[] = {

	{ADV7482_I2C_IO, 0x00, 0x30},  /* Disable chip powerdown powerdown Rx */
	{ADV7482_I2C_IO, 0xF2, 0x01},  /* Enable I2C Read Auto-Increment */

	{ADV7482_I2C_IO, 0x0E, 0xFF},  /* LLC/PIX/AUD/SPI PINS TRISTATED */

	{ADV7482_I2C_SDP, 0x0f, 0x00}, /* Exit Power Down Mode */
	{ADV7482_I2C_SDP, 0x52, 0xCD},/* ADI Required Write */
	/* TODO: do not use hard codeded INSEL */
	{ADV7482_I2C_SDP, 0x00, ADV7482_SDP_INPUT_CVBS_AIN8},
	{ADV7482_I2C_SDP, 0x0E, 0x80},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x9C, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x9C, 0xFF},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x0E, 0x00},	/* ADI Required Write */

	/* ADI recommended writes for improved video quality */
	{ADV7482_I2C_SDP, 0x80, 0x51},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x81, 0x51},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x82, 0x68},	/* ADI Required Write */

	{ADV7482_I2C_SDP, 0x03, 0x42},  /* Tri-S Output , PwrDwn 656 pads */
	{ADV7482_I2C_SDP, 0x04, 0xB5},	/* ITU-R BT.656-4 compatible */
	{ADV7482_I2C_SDP, 0x13, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_SDP, 0x17, 0x41},	/* Select SH1 */
	{ADV7482_I2C_SDP, 0x31, 0x12},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0xE6, 0x4F},  /* V bit end pos manually in NTSC */

#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV7482_I2C_SDP, 0x0C, 0x01},	/* ColorBar */
	{ADV7482_I2C_SDP, 0x14, 0x01},	/* ColorBar */
#endif
	/* Enable 1-Lane MIPI Tx, */
	/* enable pixel output and route SD through Pixel port */
	{ADV7482_I2C_IO, 0x10, 0x70},

	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV7482_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */
	{ADV7482_I2C_TXB, 0xD2, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x71, 0x33},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x72, 0x11},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */
	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */

	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static int adv7482_reset(struct adv7482_state *state)
{
	int ret;

	ret = adv7482_write_regs(state, adv7482_sw_reset);
	if (ret < 0)
		return ret;

	ret = adv7482_write_regs(state, adv7482_set_slave_address);
	if (ret < 0)
		return ret;

	/* Init and power down TXA */
	ret = adv7482_write_regs(state, adv7482_init_txa_4lane);
	if (ret)
		return ret;
	adv7482_txa_power(state, 0);
	/* Set VC 0 */
	txa_clrset(state, 0x0d, 0xc0, 0x00);

	/* Init and power down TXB */
	ret = adv7482_write_regs(state, adv7482_init_txb_1lane);
	if (ret)
		return ret;
	adv7482_txb_power(state, 0);
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

static int adv7482_print_info(struct adv7482_state *state)
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

void adv7482_subdev_init(struct v4l2_subdev *sd, struct adv7482_state *state,
		const struct v4l2_subdev_ops *ops, const char * ident)
{
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = state->dev->driver->owner;
	sd->dev = state->dev;

	v4l2_set_subdevdata(sd, state);

	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x %s",
		state->dev->driver->name, i2c_adapter_id(state->client->adapter),
		state->client->addr, ident);

	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;
	sd->entity.ops = &adv7482_media_ops;
}

struct adv7482_reg_pages {
	const char *name;
	u8 addr;
};

static const struct adv7482_reg_pages adv7482_registers[] = {
		[ADV7482_PAGE_IO] = { "io", 0x70 },
		[ADV7482_PAGE_DPLL] = { "dpll", 0x26 },
		[ADV7482_PAGE_CP] = { "cp", 0x22 },
		[ADV7482_PAGE_HDMI] = { "hdmi", 0x34 },
		[ADV7482_PAGE_EDID] = { "edid", 0x36 },
		[ADV7482_PAGE_REPEATER] = { "repeater", 0x32 },
		[ADV7482_PAGE_INFOFRAME] = { "infoframe", 0x31 },
		[ADV7482_PAGE_CEC] = { "cec", 0x41 },
		[ADV7482_PAGE_SDP] = { "sdp", 0x79 },
		[ADV7482_PAGE_TXB] = { "txb", 0x48 },
		[ADV7482_PAGE_TXA] = { "txa", 0x4A },
};

static int adv7482_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adv7482_state *state;

	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = devm_kzalloc(&client->dev, sizeof(struct adv7482_state),
			     GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->mutex);

	state->dev = &client->dev;
	state->client = client;
	i2c_set_clientdata(client, state);

	state->clients[ADV7482_PAGE_IO] = client;

	/* SW reset ADV7482 to its default values */
	ret = adv7482_reset(state);
	if (ret) {
		adv_err(state, "Failed to reset hardware");
		return ret;
	}

	ret = adv7482_print_info(state);
	if (ret)
		return ret;

	/* Initialise HDMI */
	ret = adv7482_cp_probe(state);
	if (ret) {
		adv_err(state, "Failed to probe CP");
		return ret;
	}

	/* Initialise CVBS */
	ret = adv7482_sdp_probe(state);
	if (ret) {
		adv_err(state, "Failed to probe SDP");
		return ret;
	}

	return 0;
}

static int adv7482_remove(struct i2c_client *client)
{
	struct adv7482_state *state = i2c_get_clientdata(client);

	/* These need to call down into each of the subdevs and allow them
	 * to do any removal of controls and unregister their subdevs.
	 */

	adv7482_sdp_remove(state);
	adv7482_cp_remove(state);

	mutex_destroy(&state->mutex);

	return 0;
}

static const struct i2c_device_id adv7482_id[] = {
	{ "adv7482", 0 },
	{ },
};

static const struct of_device_id adv7482_of_table[] = {
	{ .compatible = "adi,adv7482", },
	{ }
};
MODULE_DEVICE_TABLE(of, adv7482_of_ids);

static struct i2c_driver adv7482_driver = {
	.driver = {
		.name = "adv7482",
		.of_match_table = of_match_ptr(adv7482_of_table),
	},
	.probe = adv7482_probe,
	.remove = adv7482_remove,
	.id_table = adv7482_id,
};

module_i2c_driver(adv7482_driver);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("ADV7482 video decoder");
MODULE_LICENSE("GPL v2");
