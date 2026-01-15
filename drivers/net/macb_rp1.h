/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RP1-specific enhancements for Cadence MACB ethernet driver
 *
 * Copyright (C) 2026
 *
 * This file contains RP1-specific initialization and configuration
 * for the Cadence GEM ethernet controller on Raspberry Pi 5
 */

#ifndef _MACB_RP1_H
#define _MACB_RP1_H

/**
 * rp1_macb_init() - Initialize MACB for RP1 chip on RPi5
 * @regs: Base address of MACB registers
 *
 * This function performs RP1-specific initialization:
 * - Sets up proper clock configuration for RP1
 * - Configures RGMII delay settings
 * - Sets up DMA burst parameters optimized for RP1
 *
 * Return: 0 on success, negative error code on failure
 */
int rp1_macb_init(void *regs);

/**
 * rp1_macb_configure_phy() - Configure PHY for RP1 ethernet
 * @bus: MII bus
 * @phy_addr: PHY address
 *
 * Performs RP1-specific PHY configuration including:
 * - RGMII timing adjustments
 * - Auto-negotiation parameters
 *
 * Return: 0 on success, negative error code on failure
 */
int rp1_macb_configure_phy(struct mii_dev *bus, unsigned int phy_addr);

/* RP1-specific MACB configuration values */
#define RP1_MACB_DMA_BURST_LENGTH	16
#define RP1_MACB_RX_BUFFER_OFFSET	2    /* 2-byte RX buffer offset for alignment */
#define RP1_MACB_RGMII_TX_DELAY		0x7  /* RGMII TX delay for RP1 */
#define RP1_MACB_RGMII_RX_DELAY		0x7  /* RGMII RX delay for RP1 */

#endif /* _MACB_RP1_H */
