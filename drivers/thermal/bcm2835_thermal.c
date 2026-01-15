// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Broadcom BCM2835 SoC temperature sensor
 *
 * Ported from Linux kernel driver:
 *   Copyright (C) 2016 Martin Sperl
 *
 * U-Boot port:
 *   Copyright (C) 2026
 */

#include <dm.h>
#include <thermal.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#define BCM2835_TS_TSENSCTL			0x00
#define BCM2835_TS_TSENSSTAT			0x04

#define BCM2835_TS_TSENSCTL_PRWDW		BIT(0)
#define BCM2835_TS_TSENSCTL_RSTB		BIT(1)

/*
 * bandgap reference voltage in 6 mV increments
 * 000b = 1178 mV, 001b = 1184 mV, ... 111b = 1220 mV
 */
#define BCM2835_TS_TSENSCTL_CTRL_BITS		3
#define BCM2835_TS_TSENSCTL_CTRL_SHIFT		2
#define BCM2835_TS_TSENSCTL_CTRL_MASK		    \
	GENMASK(BCM2835_TS_TSENSCTL_CTRL_BITS +     \
		BCM2835_TS_TSENSCTL_CTRL_SHIFT - 1, \
		BCM2835_TS_TSENSCTL_CTRL_SHIFT)
#define BCM2835_TS_TSENSCTL_CTRL_DEFAULT	1
#define BCM2835_TS_TSENSCTL_EN_INT		BIT(5)
#define BCM2835_TS_TSENSCTL_DIRECT		BIT(6)
#define BCM2835_TS_TSENSCTL_CLR_INT		BIT(7)
#define BCM2835_TS_TSENSCTL_THOLD_SHIFT		8
#define BCM2835_TS_TSENSCTL_THOLD_BITS		10
#define BCM2835_TS_TSENSCTL_THOLD_MASK		     \
	GENMASK(BCM2835_TS_TSENSCTL_THOLD_BITS +     \
		BCM2835_TS_TSENSCTL_THOLD_SHIFT - 1, \
		BCM2835_TS_TSENSCTL_THOLD_SHIFT)
/*
 * time how long the block to be asserted in reset
 * which based on a clock counter (TSENS clock assumed)
 */
#define BCM2835_TS_TSENSCTL_RSTDELAY_SHIFT	18
#define BCM2835_TS_TSENSCTL_RSTDELAY_BITS	8
#define BCM2835_TS_TSENSCTL_REGULEN		BIT(26)

#define BCM2835_TS_TSENSSTAT_DATA_BITS		10
#define BCM2835_TS_TSENSSTAT_DATA_SHIFT		0
#define BCM2835_TS_TSENSSTAT_DATA_MASK		     \
	GENMASK(BCM2835_TS_TSENSSTAT_DATA_BITS +     \
		BCM2835_TS_TSENSSTAT_DATA_SHIFT - 1, \
		BCM2835_TS_TSENSSTAT_DATA_SHIFT)
#define BCM2835_TS_TSENSSTAT_VALID		BIT(10)
#define BCM2835_TS_TSENSSTAT_INTERRUPT		BIT(11)

struct bcm2835_thermal_priv {
	void __iomem *regs;
	int slope;
	int offset;
};

static int bcm2835_thermal_adc2temp(u32 adc, int offset, int slope)
{
	return offset + slope * adc;
}

static int bcm2835_thermal_get_temp(struct udevice *dev, int *temp)
{
	struct bcm2835_thermal_priv *priv = dev_get_priv(dev);
	u32 val = readl(priv->regs + BCM2835_TS_TSENSSTAT);

	if (!(val & BCM2835_TS_TSENSSTAT_VALID))
		return -EIO;

	val &= BCM2835_TS_TSENSSTAT_DATA_MASK;

	*temp = bcm2835_thermal_adc2temp(val, priv->offset, priv->slope);

	return 0;
}

static const struct dm_thermal_ops bcm2835_thermal_ops = {
	.get_temp = bcm2835_thermal_get_temp,
};

static int bcm2835_thermal_probe(struct udevice *dev)
{
	struct bcm2835_thermal_priv *priv = dev_get_priv(dev);
	u32 val;

	priv->regs = dev_remap_addr(dev);
	if (!priv->regs)
		return -EINVAL;

	/* Default slope and offset from devicetree or use defaults */
	priv->slope = dev_read_u32_default(dev, "slope", -487);
	priv->offset = dev_read_u32_default(dev, "offset", 410040);

	/* Enable the sensor */
	val = BIT(0) | BIT(1) | BIT(2) | BIT(3);
	val |= (BCM2835_TS_TSENSCTL_CTRL_DEFAULT <<
		BCM2835_TS_TSENSCTL_CTRL_SHIFT);
	val |= BCM2835_TS_TSENSCTL_REGULEN;
	writel(val, priv->regs + BCM2835_TS_TSENSCTL);

	/* Wait for sensor to stabilize */
	udelay(1000);

	return 0;
}

static const struct udevice_id bcm2835_thermal_ids[] = {
	{ .compatible = "brcm,bcm2835-thermal" },
	{ .compatible = "brcm,bcm2836-thermal" },
	{ .compatible = "brcm,bcm2837-thermal" },
	{ .compatible = "brcm,bcm2711-thermal" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(bcm2835_thermal) = {
	.name = "bcm2835-thermal",
	.id = UCLASS_THERMAL,
	.of_match = bcm2835_thermal_ids,
	.ops = &bcm2835_thermal_ops,
	.probe = bcm2835_thermal_probe,
	.priv_auto = sizeof(struct bcm2835_thermal_priv),
};
