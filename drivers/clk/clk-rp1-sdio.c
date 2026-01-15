// SPDX-License-Identifier: GPL-2.0+
/*
 * SDIO clock driver for RP1
 *
 * Ported from Linux kernel driver:
 *   Copyright (C) 2023 Raspberry Pi Ltd.
 *
 * U-Boot port:
 *   Copyright (C) 2026
 *
 * Simplified version for U-Boot - provides basic SDIO clock support
 */

#include <dm.h>
#include <clk-uclass.h>
#include <asm/io.h>
#include <linux/bitops.h>

#define MODE		0x00
#define LOCAL		0x08
#define USE_LOCAL	0x0C

#define MODE_SRC_SEL_MASK		0x00030000
#define MODE_SRC_SEL_CLK_ALT_SRC	(1 << 16)
#define MODE_SRC_SEL_PLL_SYS_VCO	(2 << 16)

#define LOCAL_CLK_GEN_SEL		BIT(12)
#define LOCAL_CARD_CLK_EN		BIT(16)
#define LOCAL_FREQ_SEL_MASK		0x000003ff

#define USE_LOCAL_FREQ_SEL		BIT(0)
#define USE_LOCAL_CLK_GEN_SEL		BIT(12)
#define USE_LOCAL_CARD_CLK_EN		BIT(16)

struct rp1_sdio_clk_priv {
	void __iomem *regs;
};

static int rp1_sdio_clk_enable(struct clk *clk)
{
	struct rp1_sdio_clk_priv *priv = dev_get_priv(clk->dev);
	u32 val;

	/* Enable card clock using local control */
	val = readl(priv->regs + USE_LOCAL);
	val |= USE_LOCAL_CARD_CLK_EN;
	writel(val, priv->regs + USE_LOCAL);

	val = readl(priv->regs + LOCAL);
	val |= LOCAL_CARD_CLK_EN;
	writel(val, priv->regs + LOCAL);

	return 0;
}

static int rp1_sdio_clk_disable(struct clk *clk)
{
	struct rp1_sdio_clk_priv *priv = dev_get_priv(clk->dev);
	u32 val;

	/* Disable card clock */
	val = readl(priv->regs + LOCAL);
	val &= ~LOCAL_CARD_CLK_EN;
	writel(val, priv->regs + LOCAL);

	return 0;
}

static ulong rp1_sdio_clk_set_rate(struct clk *clk, ulong rate)
{
	struct rp1_sdio_clk_priv *priv = dev_get_priv(clk->dev);
	u32 val;
	u32 div;

	/*
	 * Simplified divider calculation
	 * Assumes PLL_SYS_VCO source at ~2GHz
	 * div = (source_rate / (2 * target_rate)) - 1
	 */
	if (rate > 0) {
		div = (1000000000UL / rate) - 1;
		if (div > 0x3ff)
			div = 0x3ff;
	} else {
		div = 0;
	}

	/* Use local frequency control */
	val = readl(priv->regs + USE_LOCAL);
	val |= USE_LOCAL_FREQ_SEL | USE_LOCAL_CLK_GEN_SEL;
	writel(val, priv->regs + USE_LOCAL);

	/* Set divider and select divided clock mode */
	val = readl(priv->regs + LOCAL);
	val &= ~LOCAL_FREQ_SEL_MASK;
	val |= (div & LOCAL_FREQ_SEL_MASK);
	val |= LOCAL_CLK_GEN_SEL; /* Divided clock mode */
	writel(val, priv->regs + LOCAL);

	return 0;
}

static ulong rp1_sdio_clk_get_rate(struct clk *clk)
{
	struct rp1_sdio_clk_priv *priv = dev_get_priv(clk->dev);
	u32 val;
	u32 div;

	val = readl(priv->regs + LOCAL);
	div = val & LOCAL_FREQ_SEL_MASK;

	/* Return approximate rate based on divider */
	if (div > 0)
		return 1000000000UL / (div + 1);
	
	return 0;
}

static const struct clk_ops rp1_sdio_clk_ops = {
	.enable = rp1_sdio_clk_enable,
	.disable = rp1_sdio_clk_disable,
	.set_rate = rp1_sdio_clk_set_rate,
	.get_rate = rp1_sdio_clk_get_rate,
};

static int rp1_sdio_clk_probe(struct udevice *dev)
{
	struct rp1_sdio_clk_priv *priv = dev_get_priv(dev);
	u32 val;

	priv->regs = dev_remap_addr(dev);
	if (!priv->regs)
		return -EINVAL;

	/* Initialize - select PLL_SYS_VCO as source */
	val = readl(priv->regs + MODE);
	val &= ~MODE_SRC_SEL_MASK;
	val |= MODE_SRC_SEL_PLL_SYS_VCO;
	writel(val, priv->regs + MODE);

	return 0;
}

static const struct udevice_id rp1_sdio_clk_ids[] = {
	{ .compatible = "raspberrypi,rp1-sdio-clk" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(rp1_sdio_clk) = {
	.name = "rp1-sdio-clk",
	.id = UCLASS_CLK,
	.of_match = rp1_sdio_clk_ids,
	.ops = &rp1_sdio_clk_ops,
	.probe = rp1_sdio_clk_probe,
	.priv_auto = sizeof(struct rp1_sdio_clk_priv),
};
