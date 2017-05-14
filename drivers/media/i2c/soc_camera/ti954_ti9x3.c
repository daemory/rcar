/*
 * TI ti954-(ti913/ti953) FPDLinkIII driver
 *
 * Copyright (C) 2017 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>

#include "ti9x4_ti9x3.h"

struct ti954_ti9x3_priv {
	struct v4l2_subdev	sd[4];
	struct device_node	*sd_of_node[4];
	int			des_addr;
	int			links;
	int			lanes;
	int			csi_rate;
	const char		*forwarding_mode;
	const char		*cable_mode;
	atomic_t		use_count;
	struct i2c_client	*client;
	int			ti9x3_addr_map[4];
	char			chip_id[6];
	struct regulator	*poc_supply[4]; /* PoC power supply */
	int			xtal_gpio;
};

static int indirect_write(struct i2c_client *client, unsigned int page, u8 reg, u8 val)
{
	if (page > 7)
		return -EINVAL;

	reg8_write(client, 0xb0, page << 2);
	reg8_write(client, 0xb1, reg);
	reg8_write(client, 0xb2, val);

	return 0;
}

#if 0
static int indirect_read(struct i2c_client *client, unsigned int page, u8 reg, u8 *val)
{
	if (page > 7)
		return -EINVAL;

	reg8_write(client, 0xb0, page << 2);
	reg8_write(client, 0xb1, reg);
	reg8_read(client, 0xb2, val);

	return 0;
}
#endif

static void ti954_ti9x3_read_chipid(struct i2c_client *client)
{
	struct ti954_ti9x3_priv *priv = i2c_get_clientdata(client);

	/* Chip ID */
	reg8_read(client, 0xf1, &priv->chip_id[0]);
	reg8_read(client, 0xf2, &priv->chip_id[1]);
	reg8_read(client, 0xf3, &priv->chip_id[2]);
	reg8_read(client, 0xf4, &priv->chip_id[3]);
	reg8_read(client, 0xf5, &priv->chip_id[4]);
	priv->chip_id[5] = '\0';
}

static void ti954_ti9x3_initial_setup(struct i2c_client *client)
{
	struct ti954_ti9x3_priv *priv = i2c_get_clientdata(client);

	/* Initial setup */
	client->addr = priv->des_addr;				/* TI954 I2C */
	reg8_write(client, 0x08, 0x1c);				/* I2C glitch filter depth */
	reg8_write(client, 0x0a, 0x79);				/* I2C high pulse width */
	reg8_write(client, 0x0b, 0x79);				/* I2C low pulse width */
	reg8_write(client, 0x0d, 0xb9);				/* VDDIO 3.3V */
	switch (priv->csi_rate) {
	case 1600: /* REFCLK = 25MHZ */
	case 1450: /* REFCLK = 22.5MHZ */
		reg8_write(client, 0x1f, 0x00);			/* CSI rate 1.5/1.6Gbps */
		break;
	case 800: /* REFCLK = 25MHZ */
		reg8_write(client, 0x1f, 0x02);			/* CSI rate 800Mbps */
		break;
	case 400: /* REFCLK = 25MHZ */
		reg8_write(client, 0x1f, 0x03);			/* CSI rate 400Mbps */
		break;
	default:
		dev_err(&client->dev, "unsupported CSI rate %d\n", priv->csi_rate);
	}

	if (strcmp(priv->forwarding_mode, "round-robin") == 0) {
		reg8_write(client, 0x21, 0x01);			/* Round Robin forwarding enable */
	} else if (strcmp(priv->forwarding_mode, "synchronized") == 0) {
		reg8_write(client, 0x21, 0x44);			/* Basic Syncronized forwarding enable (FrameSync must be enabled!!) */
	}

	reg8_write(client, 0x32, 0x01);				/* Select TX (CSI) port 0 */
	reg8_write(client, 0x33, ((priv->lanes - 1) ^ 0x3) << 4); /* disable CSI output, set CSI lane count, non-continuous CSI mode */
	reg8_write(client, 0x20, 0xf0);				/* disable port forwarding */
#if 0
	/* FrameSync setup for REFCLK=25MHz,   FPS=30: period_counts=1/2/FPS*25MHz  =1/2/30*25Mhz  =416666 -> FS_TIME=416666 */
	/* FrameSync setup for REFCLK=22.5MHz, FPS=30: period_counts=1/2/FPS*22.5Mhz=1/2/30*22.5Mhz=375000 -> FS_TIME=375000 */
// #define FS_TIME (priv->csi_rate == 1450 ? 376000 : 417666)
 #define FS_TIME (priv->csi_rate == 1450 ? 385000 : 428000) // FPS=29.2 (new vendor's firmware AWB restriction?)
	reg8_write(client, 0x1a, FS_TIME >> 16);		/* FrameSync time 24bit */
	reg8_write(client, 0x1b, (FS_TIME >> 8) & 0xff);
	reg8_write(client, 0x1c, FS_TIME & 0xff);
	reg8_write(client, 0x18, 0x43);				/* Enable FrameSync, 50/50 mode, Frame clock from 25MHz */
#else
	/* FrameSync setup for REFCLK=25MHz,   FPS=30: period_counts=1/FPS/12mks=1/30/12e-6=2777 -> HI=2, LO=2775 */
	/* FrameSync setup for REFCLK=22.5MHz, FPS=30: period_counts=1/FPS/13.333mks=1/30/13.333e-6=2500 -> HI=2, LO=2498 */
 #define FS_TIME (priv->csi_rate == 1450 ? (2498+15) : (2775+15))
	reg8_write(client, 0x19, 2 >> 8);			/* FrameSync high time MSB */
	reg8_write(client, 0x1a, 2 & 0xff);			/* FrameSync high time LSB */
	reg8_write(client, 0x1b, FS_TIME >> 8);			/* FrameSync low time MSB */
	reg8_write(client, 0x1c, FS_TIME & 0xff);			/* FrameSync low time LSB */
	reg8_write(client, 0x18, 0x01);				/* Enable FrameSync, HI/LO mode, Frame clock from port0 */
#endif
}

//#define SENSOR_ID 0x30  // ov10635
//#define SENSOR_ID 0x24  // ov490

static void ti954_ti9x3_fpdlink3_setup(struct i2c_client *client, int idx)
{
	struct ti954_ti9x3_priv *priv = i2c_get_clientdata(client);

	/* FPDLinkIII setup */
	client->addr = priv->des_addr;				/* TI954 I2C */
	reg8_write(client, 0x4c, (idx << 4) | (1 << idx));	/* Select RX port number */
	usleep_range(2000, 2500);				/* wait 2ms */
	reg8_write(client, 0x58, 0x58);				/* Back channel: pass-through/backchannel/CRC enable, Freq=2.5Mbps */
	reg8_write(client, 0x5c, priv->ti9x3_addr_map[idx] << 1); /* TI9X3 I2C addr */
//	reg8_write(client, 0x5d, SENSOR_ID << 1);		/* SENSOR I2C native - must be set by sensor driver */
//	reg8_write(client, 0x65, (0x60 + idx) << 1);		/* SENSOR I2C translated - must be set by sensor driver */
	if (strcmp(priv->cable_mode, "coax") == 0) {
		reg8_write(client, 0x6d, 0x7f);			/* Coax, RAW10 */
	} else if (strcmp(priv->cable_mode, "stp") == 0) {
		reg8_write(client, 0x6d, 0x78);			/* STP, CSI */
	}
	reg8_write(client, 0x70, (idx << 6) | 0x1e);		/* CSI data type: yuv422 8-bit, assign VC */
	reg8_write(client, 0x7c, 0x81);				/* BIT(7) - magic to Use RAW10 as 8-bit mode */
	reg8_write(client, 0x6e, 0x88);				/* Sensor reset: backchannel GPIO0/GPIO1 set low */
}

static int ti954_ti9x3_initialize(struct i2c_client *client)
{
	struct ti954_ti9x3_priv *priv = i2c_get_clientdata(client);
	int idx;

	dev_info(&client->dev, "LINKs=%d, LANES=%d, FORWARDING=%s, CABLE=%s, ID=%s\n",
			       priv->links, priv->lanes, priv->forwarding_mode, priv->cable_mode, priv->chip_id);

	ti954_ti9x3_initial_setup(client);

	for (idx = 0; idx < priv->links; idx++) {
		if (!IS_ERR(priv->poc_supply[idx])) {
			if (regulator_enable(priv->poc_supply[idx]))
				dev_err(&client->dev, "fail to enable POC%d regulator\n", idx);
		}

		ti954_ti9x3_fpdlink3_setup(client, idx);
	}

	client->addr = priv->des_addr;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ti954_ti9x3_g_register(struct v4l2_subdev *sd,
				      struct v4l2_dbg_register *reg)
{
	struct ti954_ti9x3_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;
	int ret;
	u8 val = 0;

	ret = reg8_read(client, (u8)reg->reg, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = sizeof(u8);

	return 0;
}

static int ti954_ti9x3_s_register(struct v4l2_subdev *sd,
				      const struct v4l2_dbg_register *reg)
{
	struct ti954_ti9x3_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	return reg8_write(client, (u8)reg->reg, (u8)reg->val);
}
#endif

static int ti954_ti9x3_s_power(struct v4l2_subdev *sd, int on)
{
	struct ti954_ti9x3_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	if (on) {
		if (atomic_inc_return(&priv->use_count) == 1)
			reg8_write(client, 0x20, 0x00);		/* enable port forwarding to CSI */
	} else {
		if (atomic_dec_return(&priv->use_count) == 0)
			reg8_write(client, 0x20, 0xf0);		/* disable port forwarding to CSI */
	}

	return 0;
}

static int ti954_ti9x3_registered_async(struct v4l2_subdev *sd)
{
	struct ti954_ti9x3_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	reg8_write(client, 0x33, ((priv->lanes - 1) ^ 0x3) << 4 | 0x1); /* enable CSI output, set CSI lane count, non-continuous CSI mode */

	return 0;
}

static struct v4l2_subdev_core_ops ti954_ti9x3_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ti954_ti9x3_g_register,
	.s_register		= ti954_ti9x3_s_register,
#endif
	.s_power		= ti954_ti9x3_s_power,
	.registered_async	= ti954_ti9x3_registered_async,
};

static struct v4l2_subdev_ops ti954_ti9x3_subdev_ops = {
	.core	= &ti954_ti9x3_subdev_core_ops,
};

static int ti954_ti9x3_parse_dt(struct i2c_client *client)
{
	struct ti954_ti9x3_priv *priv = i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	struct device_node *endpoint = NULL, *rendpoint = NULL;
	struct property *prop;
	int err, i;
	int sensor_delay;
	char forwarding_mode_default[20] = "round-robin"; /* round-robin, synchronized */
	char cable_mode_default[5] = "coax"; /* coax, stp */
	struct property *csi_rate_prop, *dvp_order_prop;
	u8 val = 0;

	if (of_property_read_u32(np, "ti,links", &priv->links))
		priv->links = 2;

	if (of_property_read_u32(np, "ti,lanes", &priv->lanes))
		priv->lanes = 4;

	priv->xtal_gpio = of_get_gpio(np, 0);
	if (priv->xtal_gpio > 0) {
		err = devm_gpio_request_one(&client->dev, priv->xtal_gpio, GPIOF_OUT_INIT_LOW, dev_name(&client->dev));
		if (err)
			dev_err(&client->dev, "cannot request XTAL gpio %d: %d\n", priv->xtal_gpio, err);
		else
			mdelay(250);
	}

	reg8_read(client, 0x00, &val);				/* read TI954 I2C address */
	if (val != (priv->des_addr << 1)) {
		prop = of_find_property(np, "reg", NULL);
		if (prop)
			of_remove_property(np, prop);
		return -ENODEV;
	}

	ti954_ti9x3_read_chipid(client);

	indirect_write(client, 7, 0x15, 0x30);
	gpio_set_value(priv->xtal_gpio, 1);
	usleep_range(5000, 5500);				/* wait 5ms */
	indirect_write(client, 7, 0x15, 0);

	if (!of_property_read_u32(np, "ti,sensor_delay", &sensor_delay))
		mdelay(sensor_delay);

	err = of_property_read_string(np, "ti,forwarding-mode", &priv->forwarding_mode);
	if (err)
		priv->forwarding_mode = forwarding_mode_default;

	err = of_property_read_string(np, "ti,cable-mode", &priv->cable_mode);
	if (err)
		priv->cable_mode = cable_mode_default;

	for (i = 0; ; i++) {
		endpoint = of_graph_get_next_endpoint(np, endpoint);
		if (!endpoint)
			break;

		of_node_put(endpoint);

		if (i < priv->links) {
			if (of_property_read_u32(endpoint, "ti9x3-addr", &priv->ti9x3_addr_map[i])) {
				dev_err(&client->dev, "ti9x3-addr not set\n");
				return -EINVAL;
			}
			priv->sd_of_node[i] = endpoint;
		}

		rendpoint = of_parse_phandle(endpoint, "remote-endpoint", 0);
		if (!rendpoint)
			continue;

		csi_rate_prop = of_find_property(endpoint, "csi-rate", NULL);
		if (csi_rate_prop) {
			of_property_read_u32(endpoint, "csi-rate", &priv->csi_rate);
			of_update_property(rendpoint, csi_rate_prop);
		}

		dvp_order_prop = of_find_property(endpoint, "dvp-order", NULL);
		if (dvp_order_prop)
			of_update_property(rendpoint, dvp_order_prop);
	}

	return 0;
}

static int ti954_ti9x3_probe(struct i2c_client *client,
			     const struct i2c_device_id *did)
{
	struct ti954_ti9x3_priv *priv;
	int err, i;
	char supply_name[10];

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);
	priv->des_addr = client->addr;
	priv->client = client;
	atomic_set(&priv->use_count, 0);

	err = ti954_ti9x3_parse_dt(client);
	if (err)
		goto out;

	for (i = 0; i < 4; i++) {
		sprintf(supply_name, "POC%d", i);
		priv->poc_supply[i] = devm_regulator_get_optional(&client->dev, supply_name);
	}

	err = ti954_ti9x3_initialize(client);
	if (err < 0)
		goto out;

	for (i = 0; i < priv->links; i++) {
		v4l2_subdev_init(&priv->sd[i], &ti954_ti9x3_subdev_ops);
		priv->sd[i].owner = client->dev.driver->owner;
		priv->sd[i].dev = &client->dev;
		priv->sd[i].grp_id = i;
		v4l2_set_subdevdata(&priv->sd[i], priv);
		priv->sd[i].of_node = priv->sd_of_node[i];

		snprintf(priv->sd[i].name, V4L2_SUBDEV_NAME_SIZE, "%s %d-%04x",
			 client->dev.driver->name, i2c_adapter_id(client->adapter),
			 client->addr);

		err = v4l2_async_register_subdev(&priv->sd[i]);
		if (err < 0)
			goto out;
	}

out:
	return err;
}

static int ti954_ti9x3_remove(struct i2c_client *client)
{
	struct ti954_ti9x3_priv *priv = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < priv->links; i++) {
		v4l2_async_unregister_subdev(&priv->sd[i]);
		v4l2_device_unregister_subdev(&priv->sd[i]);
	}

	return 0;
}

static const struct of_device_id ti954_ti9x3_dt_ids[] = {
	{ .compatible = "ti,ti954-ti9x3" },
	{},
};
MODULE_DEVICE_TABLE(of, ti954_ti9x3_dt_ids);

static const struct i2c_device_id ti954_ti9x3_id[] = {
	{ "ti954_ti9x3", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ti954_ti9x3_id);

static struct i2c_driver ti954_ti9x3_i2c_driver = {
	.driver	= {
		.name		= "ti954_ti9x3",
		.of_match_table	= of_match_ptr(ti954_ti9x3_dt_ids),
	},
	.probe		= ti954_ti9x3_probe,
	.remove		= ti954_ti9x3_remove,
	.id_table	= ti954_ti9x3_id,
};

module_i2c_driver(ti954_ti9x3_i2c_driver);

MODULE_DESCRIPTION("FPDLinkIII driver for TI954-TI9X3");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
