// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Raspberry Pi RP1 Pinctrl
 *
 * Ported from Linux kernel driver:
 *   Copyright (C) 2023 Raspberry Pi Ltd.
 *
 * U-Boot port:
 *   Copyright (C) 2026
 *
 * This is a simplified pinctrl driver for RP1 that provides
 * basic pin muxing functionality for U-Boot.
 */

#include <dm.h>
#include <dm/pinctrl.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#define MODULE_NAME "pinctrl-rp1"
#define RP1_NUM_GPIOS	54

#define RP1_GPIO_CTRL			0x0004

/* GPIO Control register fields - use GENMASK and FIELD_PREP */
enum {
	RP1_GPIO_CTRL_FUNCSEL_MASK	= GENMASK(4, 0),
	RP1_GPIO_CTRL_OUTOVER_MASK	= GENMASK(13, 12),
	RP1_GPIO_CTRL_OEOVER_MASK	= GENMASK(15, 14),
};

/* PAD Control register fields */
enum {
	RP1_PAD_PULL_MASK		= GENMASK(3, 2),
	RP1_PAD_IN_ENABLE		= BIT(6),
	RP1_PAD_OUT_DISABLE		= BIT(7),
};

/* Function select values */
enum {
	RP1_FSEL_ALT0			= 0x00,
	RP1_FSEL_GPIO			= 0x05,
	RP1_FSEL_NONE_HW		= 0x1f,
};

/* Pull up/down values */
enum {
	RP1_PUD_OFF			= 0,
	RP1_PUD_DOWN			= 1,
	RP1_PUD_UP			= 2,
};

/* Output enable override values */
enum {
	RP1_OEOVER_PERI			= 0,
	RP1_OEOVER_DISABLE		= 2,
};

struct rp1_pinctrl_priv {
	void __iomem *gpio_base;
	void __iomem *pads_base;
};

static void __iomem *rp1_get_gpio_reg(struct rp1_pinctrl_priv *priv, unsigned int pin)
{
	/* Each GPIO has 2 registers (STATUS and CTRL), each 4 bytes */
	/* GPIO banks:
	 *   Bank 0: GPIOs 0-27  at offset 0x0000
	 *   Bank 1: GPIOs 28-33 at offset 0x4000
	 *   Bank 2: GPIOs 34-53 at offset 0x8000
	 */
	unsigned int bank_offset;
	unsigned int pin_offset;

	if (pin < 28) {
		bank_offset = 0x0000;
		pin_offset = pin;
	} else if (pin < 34) {
		bank_offset = 0x4000;
		pin_offset = pin - 28;
	} else {
		bank_offset = 0x8000;
		pin_offset = pin - 34;
	}

	return priv->gpio_base + bank_offset + (pin_offset * 8);
}

static void __iomem *rp1_get_pad_reg(struct rp1_pinctrl_priv *priv, unsigned int pin)
{
	unsigned int bank_offset;
	unsigned int pin_offset;

	if (pin < 28) {
		bank_offset = 0x0004;
		pin_offset = pin;
	} else if (pin < 34) {
		bank_offset = 0x4004;
		pin_offset = pin - 28;
	} else {
		bank_offset = 0x8004;
		pin_offset = pin - 34;
	}

	return priv->pads_base + bank_offset + (pin_offset * 4);
}

static int rp1_pinctrl_set_state(struct udevice *dev, struct udevice *config)
{
	struct rp1_pinctrl_priv *priv = dev_get_priv(dev);
	u32 pins[RP1_NUM_GPIOS];
	u32 func;
	int count;

	count = dev_read_u32_array(config, "pins", pins, RP1_NUM_GPIOS);
	if (count < 0)
		count = dev_read_u32_array(config, "brcm,pins", pins, RP1_NUM_GPIOS);
	if (count < 0)
		return 0;

	func = dev_read_u32_default(config, "function", RP1_FSEL_GPIO);
	if (func == (u32)-1)
		func = dev_read_u32_default(config, "brcm,function", RP1_FSEL_GPIO);

	for (int i = 0; i < count; i++) {
		u32 pin = pins[i];
		void __iomem *gpio_reg;
		void __iomem *pad_reg;
		u32 oeover_val;
		u32 pull;

		if (pin >= RP1_NUM_GPIOS)
			continue;

		gpio_reg = rp1_get_gpio_reg(priv, pin);
		pad_reg = rp1_get_pad_reg(priv, pin);

		/* Determine output enable override based on function */
		if (func == RP1_FSEL_GPIO)
			oeover_val = RP1_OEOVER_DISABLE;
		else
			oeover_val = RP1_OEOVER_PERI;

		/* Set function and output enable override in one operation */
		clrsetbits_le32(gpio_reg + RP1_GPIO_CTRL,
				RP1_GPIO_CTRL_FUNCSEL_MASK | RP1_GPIO_CTRL_OEOVER_MASK,
				FIELD_PREP(RP1_GPIO_CTRL_FUNCSEL_MASK, func) |
				FIELD_PREP(RP1_GPIO_CTRL_OEOVER_MASK, oeover_val));

		/* Configure pull up/down and enable input/output */
		pull = dev_read_u32_default(config, "brcm,pull", RP1_PUD_OFF);
		clrsetbits_le32(pad_reg,
				RP1_PAD_PULL_MASK | RP1_PAD_OUT_DISABLE,
				FIELD_PREP(RP1_PAD_PULL_MASK, pull) | RP1_PAD_IN_ENABLE);
	}

	return 0;
}

static int rp1_pinctrl_get_pin_muxing(struct udevice *dev, unsigned int selector,
				      char *buf, int size)
{
	struct rp1_pinctrl_priv *priv = dev_get_priv(dev);
	void __iomem *gpio_reg;
	u32 ctrl;
	u32 func;

	if (selector >= RP1_NUM_GPIOS)
		return -EINVAL;

	gpio_reg = rp1_get_gpio_reg(priv, selector);
	ctrl = readl(gpio_reg + RP1_GPIO_CTRL);
	func = FLD_GET(ctrl, RP1_GPIO_CTRL_FUNCSEL);

	snprintf(buf, size, "func%d", func);
	return 0;
}

static const struct pinctrl_ops rp1_pinctrl_ops = {
	.set_state = rp1_pinctrl_set_state,
	.get_pinIELD_GET(RP1_GPIO_CTRL_FUNCSEL_MASK, ctrl);

	snprintf(buf, size, "func%d", func);

static int rp1_pinctrl_probe(struct udevice *dev)
{
	struct rp1_pinctrl_priv *priv = dev_get_priv(dev);

	priv->gpio_base = dev_remap_addr_index(dev, 0);
	if (!priv->gpio_base)
		return -EINVAL;

	priv->pads_base = dev_remap_addr_index(dev, 2);
	if (!priv->pads_base)
		return -EINVAL;

	return 0;
}

static const struct udevice_id rp1_pinctrl_ids[] = {
	{ }
};

U_BOOT_DRIVER(rp1_pinctrl) = {
	.name		= "rp1-pinctrl",
	.id		= UCLASS_PINCTRL,
	.of_match	= rp1_pinctrl_ids,
	.ops		= &rp1_pinctrl_ops,
	.probe		= rp1_pinctrl_probe,
	.priv_auto	1_pinctrl_probe,
	.priv_auto = sizeof(struct rp1_pinctrl_priv),
};
