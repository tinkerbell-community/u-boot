/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * BCM2712-specific PCIe enhancements
 *
 * Copyright (C) 2026
 *
 * Based on Linux kernel driver:
 *   drivers/pci/controller/pcie-brcmstb.c
 */

#ifndef _PCIE_BCM2712_H
#define _PCIE_BCM2712_H

/**
 * bcm2712_pcie_early_init() - Perform BCM2712-specific PCIe early initialization
 * @pcie: Pointer to brcm_pcie structure
 *
 * This function handles BCM2712-specific initialization before link training:
 * - Configure PERST# timing for RP1
 * - Set up proper power sequencing
 * - Configure MDIO for PHY settings
 *
 * Return: 0 on success, negative error code on failure
 */
int bcm2712_pcie_early_init(struct brcm_pcie *pcie);

/**
 * bcm2712_pcie_optimize_link() - Optimize PCIe link for faster boot
 * @pcie: Pointer to brcm_pcie structure
 *
 * Optimizes PCIe link training for BCM2712:
 * - Configures optimal Gen3 settings
 * - Reduces link training time
 * - Sets up proper equalization
 *
 * Return: 0 on success, negative error code on failure
 */
int bcm2712_pcie_optimize_link(struct brcm_pcie *pcie);

/**
 * bcm2712_pcie_configure_rp1() - Configure PCIe for RP1 chip
 * @pcie: Pointer to brcm_pcie structure
 *
 * Performs RP1-specific PCIe configuration:
 * - Sets up inbound windows for RP1 peripherals
 * - Configures MSI for RP1 interrupts
 * - Optimizes DMA settings
 *
 * Return: 0 on success, negative error code on failure
 */
int bcm2712_pcie_configure_rp1(struct brcm_pcie *pcie);

/* BCM2712 PCIe register offsets and values */
#define BCM2712_PCIE_RC_CFG_LINK_CAP		0x04DC
#define BCM2712_PCIE_RC_CFG_LINK_STATUS		0x04DE
#define BCM2712_PCIE_RC_PL_PHY_CTL_15		0x184C

/* RP1-specific configuration */
#define RP1_VENDOR_ID				0x1de4
#define RP1_DEVICE_ID_C0			0x0001
#define RP1_DEVICE_REV_C0			2

/* Link training optimization values */
#define BCM2712_PCIE_GEN3_EQ_PRESET		0x3
#define BCM2712_PCIE_LINK_TIMEOUT_US		100000

#endif /* _PCIE_BCM2712_H */
