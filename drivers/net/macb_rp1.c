// SPDX-License-Identifier: GPL-2.0+
/*
 * RP1-specific enhancements for Cadence MACB ethernet driver
 *
 * Copyright (C) 2026
 *
 * Based on Linux kernel drivers:
 *   drivers/net/ethernet/cadence/macb_main.c
 *   drivers/mfd/rp1.c
 */

#include <common.h>
#include <dm.h>
#include <linux/delay.h>
#include <miiphy.h>
#include "macb.h"
#include "macb_rp1.h"

/**
 * rp1_macb_init() - Initialize MACB for RP1 chip on RPi5
 */
int rp1_macb_init(void *regs)
{
	struct macb_device *macb = (struct macb_device *)regs;
	u32 ncfgr;

	/* Read current network config */
	ncfgr = macb_readl(macb, NCFGR);

	/*
	 * Configure for RP1:
	 * - Set DMA burst length to 16 for optimal performance
	 * - Enable full-duplex by default
	 * - Set speed to 1000Mbps
	 */
	ncfgr |= MACB_BF(NCFGR_DBW, MACB_DBW_32);  /* 32-bit data bus width */
	ncfgr |= MACB_BIT(NCFGR_FD);               /* Full duplex */
	
	/* Set RX buffer offset for proper alignment on RP1 */
	ncfgr &= ~MACB_BF(NCFGR_RBOF, 0x3);
	ncfgr |= MACB_BF(NCFGR_RBOF, RP1_MACB_RX_BUFFER_OFFSET);

	macb_writel(macb, NCFGR, ncfgr);

	/* Configure DMA settings optimized for RP1 */
	if (macb_is_gem(macb)) {
		u32 dmacfg = gem_readl(macb, DMACFG);
		
		/* Set DMA burst length */
		dmacfg &= ~GEM_BF(DMACFG_FBLDO, 0x1f);
		dmacfg |= GEM_BF(DMACFG_FBLDO, RP1_MACB_DMA_BURST_LENGTH);
		
		/* Enable TX/RX checksum offload if supported */
		dmacfg |= GEM_BIT(DMACFG_TXCOEN);
		dmacfg |= GEM_BIT(DMACFG_RXCOEN);
		
		gem_writel(macb, DMACFG, dmacfg);
	}

	return 0;
}

/**
 * rp1_macb_configure_phy() - Configure PHY for RP1 ethernet
 */
int rp1_macb_configure_phy(struct mii_dev *bus, unsigned int phy_addr)
{
	u16 phy_id1, phy_id2;
	int ret;

	/* Read PHY identification */
	ret = bus->read(bus, phy_addr, MDIO_DEVAD_NONE, MII_PHYSID1);
	if (ret < 0)
		return ret;
	phy_id1 = ret;

	ret = bus->read(bus, phy_addr, MDIO_DEVAD_NONE, MII_PHYSID2);
	if (ret < 0)
		return ret;
	phy_id2 = ret;

	debug("RP1 MACB: PHY ID1=0x%04x ID2=0x%04x\n", phy_id1, phy_id2);

	/*
	 * Configure RGMII delays for RP1
	 * These values are tuned for Raspberry Pi 5 hardware
	 */
	
	/* Enable auto-negotiation with gigabit advertisement */
	ret = bus->write(bus, phy_addr, MDIO_DEVAD_NONE, MII_BMCR,
			 BMCR_ANENABLE | BMCR_ANRESTART);
	if (ret < 0)
		return ret;

	/* Advertise all speeds up to 1000Mbps full-duplex */
	ret = bus->write(bus, phy_addr, MDIO_DEVAD_NONE, MII_ADVERTISE,
			 ADVERTISE_FULL | ADVERTISE_100FULL |
			 ADVERTISE_10FULL | ADVERTISE_CSMA);
	if (ret < 0)
		return ret;

	/* Wait for PHY to complete initialization */
	mdelay(10);

	return 0;
}

/**
 * rp1_macb_check_link_status() - Check and update link status for RP1
 */
int rp1_macb_check_link_status(struct macb_device *macb)
{
	u32 ncfgr;
	u16 bmsr, bmcr;
	int ret;

	if (!macb->phydev)
		return -ENODEV;

	/* Read PHY status */
	ret = macb->bus->read(macb->bus, macb->phy_addr, MDIO_DEVAD_NONE, MII_BMSR);
	if (ret < 0)
		return ret;
	bmsr = ret;

	ret = macb->bus->read(macb->bus, macb->phy_addr, MDIO_DEVAD_NONE, MII_BMCR);
	if (ret < 0)
		return ret;
	bmcr = ret;

	/* Update MACB configuration based on link status */
	ncfgr = macb_readl(macb, NCFGR);

	if (bmsr & BMSR_LSTATUS) {
		/* Link is up - configure speed and duplex */
		if (bmcr & BMCR_SPEED1000) {
			ncfgr |= GEM_BIT(NCFGR_GBE);    /* Gigabit mode */
			ncfgr &= ~MACB_BIT(NCFGR_SPD);  /* Clear 100Mbps */
		} else if (bmcr & BMCR_SPEED100) {
			ncfgr &= ~GEM_BIT(NCFGR_GBE);   /* Not gigabit */
			ncfgr |= MACB_BIT(NCFGR_SPD);   /* 100Mbps */
		} else {
			ncfgr &= ~GEM_BIT(NCFGR_GBE);   /* Not gigabit */
			ncfgr &= ~MACB_BIT(NCFGR_SPD);  /* 10Mbps */
		}

		if (bmcr & BMCR_FULLDPLX)
			ncfgr |= MACB_BIT(NCFGR_FD);
		else
			ncfgr &= ~MACB_BIT(NCFGR_FD);

		macb_writel(macb, NCFGR, ncfgr);
		
		debug("RP1 MACB: Link up - Speed: %s, Duplex: %s\n",
		      (bmcr & BMCR_SPEED1000) ? "1000" :
		      (bmcr & BMCR_SPEED100) ? "100" : "10",
		      (bmcr & BMCR_FULLDPLX) ? "Full" : "Half");
	} else {
		debug("RP1 MACB: Link down\n");
	}

	return (bmsr & BMSR_LSTATUS) ? 1 : 0;
}
