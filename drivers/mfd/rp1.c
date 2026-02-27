// SPDX-License-Identifier: GPL-2.0
/*
 * RP1 Multi-Function Device Driver for Raspberry Pi 5
 *
 * Copyright (c) 2024 EPAM Systems
 * Copyright (c) 2018-22 Raspberry Pi Ltd.
 *
 * Based on Linux kernel driver:
 *   drivers/mfd/rp1.c
 *
 * This driver initializes the RP1 I/O chip on Raspberry Pi 5, which connects
 * to the BCM2712 SoC via PCIe Gen 3. It manages:
 * - PCI device enumeration and BAR mapping
 * - Chip identification and platform detection
 * - Child device binding (GPIO, clocks, reset controllers, etc.)
 *
 * The RP1 chip provides various I/O peripherals including GPIOs, UARTs,
 * SPI, I2C, PWM, USB, Ethernet, and more.
 */

#include <asm/io.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dm/of_access.h>
#include <dm/lists.h>
#include <dt-bindings/mfd/rp1.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/types.h>
#include <pci.h>

/* RP1 Chip Identification */
#define RP1_B0_CHIP_ID		0x10001927
#define RP1_C0_CHIP_ID		0x20001927

/* Platform Type Bits */
#define RP1_PLATFORM_ASIC	BIT(1)
#define RP1_PLATFORM_FPGA	BIT(0)

#define RP1_DRIVER_NAME		"rp1"

/* PCI Device Revision for RP1 C0 */
#define PCI_DEVICE_REV_RP1_C0	2

/* SYSINFO Register Offsets */
#define SYSINFO_CHIP_ID		0x0000
#define SYSINFO_PLATFORM	0x0004

/* PCIE APBS (Advanced Peripheral Bus Subsystem) Offsets for MSIX */
#define PCIE_MSIX_CFG_BASE	0x0008
#define PCIE_MSIX_CFG_IACK_EN	0x0000
#define PCIE_MSIX_CFG_IACK	0x0004
#define PCIE_MSIX_CFG_TEST	0x0008

/* MSIX Configuration Bit Fields */
#define MSIX_CFG_ENABLE		BIT(0)
#define MSIX_CFG_IACK_ENABLE	BIT(0)

/**
 * struct rp1_dev - RP1 device instance data
 * @pdev: PCI device handle
 * @bar_start: Physical start address of mapped BAR
 * @bar_end: Physical end address of mapped BAR
 */
struct rp1_dev {
	struct udevice *pdev;
	phys_addr_t bar_start;
	phys_addr_t bar_end;
};


/**
 * rp1_io_to_phys() - Convert RP1 internal offset to physical address
 * @rp1: RP1 device instance
 * @offset: Register offset within RP1 address space
 *
 * Return: Physical address in PCI BAR space
 */
static inline dma_addr_t rp1_io_to_phys(struct rp1_dev *rp1, u32 offset)
{
	return rp1->bar_start + offset;
}

/**
 * rp1_reg_read() - Read a register from RP1 peripheral block
 * @rp1: RP1 device instance
 * @base_addr: Base address of peripheral (e.g., RP1_SYSINFO_BASE)
 * @offset: Register offset within peripheral
 *
 * Maps the peripheral register block, reads the value, then unmaps.
 * This is used during initialization before permanent mappings are established.
 *
 * Return: Register value
 */
static u32 rp1_reg_read(struct rp1_dev *rp1, u32 base_addr, u32 offset)
{
	phys_addr_t phys = rp1_io_to_phys(rp1, base_addr);
	void __iomem *regblock = ioremap(phys, 0x1000);
	u32 value;

	if (!regblock) {
		pr_err("RP1: Failed to map register block at 0x%x\n", base_addr);
		return 0;
	}

	value = readl(regblock + offset);
	iounmap(regblock);

	return value;
}

/**
 * rp1_get_bar_region() - Map PCI BAR1 for RP1 register access
 * @dev: PCI device
 * @bar_start: Output parameter for BAR start address
 * @bar_end: Output parameter for BAR end address
 *
 * BAR1 contains the RP1 internal peripheral register space.
 *
 * Return: 0 on success, negative error code on failure
 */
static int rp1_get_bar_region(struct udevice *dev, phys_addr_t *bar_start,
			       phys_addr_t *bar_end)
{
	void *bar_addr;

	bar_addr = dm_pci_map_bar(dev, PCI_BASE_ADDRESS_1, 0, 0,
				  PCI_REGION_TYPE, PCI_REGION_MEM);
	if (!bar_addr)
		return -ENOMEM;

	*bar_start = (phys_addr_t)bar_addr;
	
	/* BAR1 size is typically 16MB for RP1 peripheral space */
	*bar_end = *bar_start + 0x1000000 - 1;

	return 0;
}


/**
 * rp1_bind_child_devices() - Bind child devices from device tree
 * @dev: RP1 parent device
 *
 * Scans the device tree for child nodes and binds appropriate drivers.
 * This populates child devices like GPIO, clocks, reset controllers, etc.
 *
 * Return: 0 on success, negative error code on failure
 */
static int rp1_bind_child_devices(struct udevice *dev)
{
	ofnode node = dev_ofnode(dev);
	ofnode child;
	int ret;

	if (!ofnode_valid(node))
		return 0;

	/* Bind all child nodes to their respective drivers */
	ofnode_for_each_subnode(child, node) {
		const char *name = ofnode_get_name(child);
		
		if (!ofnode_is_enabled(child)) {
			dev_dbg(dev, "Skipping disabled child: %s\n", name);
			continue;
		}

		ret = lists_bind_fdt(dev, child, NULL, NULL, false);
		if (ret && ret != -ENOENT) {
			dev_err(dev, "Failed to bind child %s: %d\n", name, ret);
			return ret;
		}

		dev_dbg(dev, "Bound child device: %s\n", name);
	}

	return 0;
}

/**
 * rp1_probe() - Initialize RP1 device
 * @dev: RP1 device to probe
 *
 * Performs the following initialization:
 * 1. Enables PCI bus mastering and memory access
 * 2. Configures BAR addresses (HACK for Linux kernel compatibility)
 * 3. Maps BAR1 for register access
 * 4. Verifies chip ID
 * 5. Detects platform type (ASIC vs FPGA)
 *
 * Return: 0 on success, negative error code on failure
 */
static int rp1_probe(struct udevice *dev)
{
	struct rp1_dev *rp1 = dev_get_priv(dev);
	u32 chip_id, platform;
	int ret;

	rp1->pdev = dev;

	/* Enable PCI bus mastering and memory space access */
	dm_pci_clrset_config16(dev, PCI_COMMAND, 0,
			       PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);

	/*
	 * HACK: Set BAR addresses in specific order to match Linux kernel.
	 *
	 * The Linux RP1 driver relies on PCI BAR configuration that initializes
	 * BARs sorted by size (larger BARs first). This results in consistent
	 * BAR ordering across boots in Linux.
	 *
	 * U-Boot's PCI enumeration doesn't perform this sorting, which can lead
	 * to different BAR assignments compared to Linux. This causes issues
	 * with address translation between U-Boot and Linux.
	 *
	 * Hardcode BAR addresses to match Linux kernel expectations:
	 * - BAR0 at 0x40000 (smaller BAR)
	 * - BAR1 at 0x00000 (larger BAR with peripheral registers)
	 *
	 * TODO: Remove this hack once RP1 driver is mainlined in Linux kernel
	 * and proper address-agnostic driver design is established.
	 */
	dm_pci_write_config32(dev, PCI_BASE_ADDRESS_1, 0x0);
	dm_pci_write_config32(dev, PCI_BASE_ADDRESS_0, 0x40000);

	/* Map BAR1 containing RP1 peripheral register space */
	ret = rp1_get_bar_region(dev, &rp1->bar_start, &rp1->bar_end);
	if (ret) {
		dev_err(dev, "Failed to map BAR1: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "BAR1 mapped: 0x%llx - 0x%llx\n",
		(unsigned long long)rp1->bar_start,
		(unsigned long long)rp1->bar_end);

	/* Read and verify chip identification */
	chip_id = rp1_reg_read(rp1, RP1_SYSINFO_BASE, SYSINFO_CHIP_ID);
	platform = rp1_reg_read(rp1, RP1_SYSINFO_BASE, SYSINFO_PLATFORM);

	dev_info(dev, "RP1 chip_id=0x%08x%s\n", chip_id,
		 (platform & RP1_PLATFORM_FPGA) ? " (FPGA)" : " (ASIC)");

	if (chip_id != RP1_C0_CHIP_ID) {
		dev_err(dev, "Unsupported chip ID: 0x%08x (expected 0x%08x)\n",
			chip_id, RP1_C0_CHIP_ID);
		return -ENODEV;
	}

	dev_info(dev, "RP1 initialized successfully\n");

	return 0;
}


/**
 * rp1_bind() - Bind RP1 device and its children
 * @dev: RP1 device to bind
 *
 * Sets the device name and binds child devices from device tree.
 *
 * Return: 0 on success, negative error code on failure
 */
static int rp1_bind(struct udevice *dev)
{
	int ret;

	device_set_name(dev, RP1_DRIVER_NAME);

	/* Bind child devices (GPIO, clocks, reset, etc.) */
	ret = rp1_bind_child_devices(dev);
	if (ret)
		dev_err(dev, "Failed to bind child devices: %d\n", ret);

	return ret;
}

/* PCI Device ID table for RP1 */
static const struct pci_device_id rp1_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RPI, PCI_DEVICE_ID_RP1_C0) },
	{ }
};

/**
 * rp1_pcie_read_config() - Stub PCI config read callback
 * @bus: PCI bus
 * @bdf: PCI bus/device/function identifier
 * @offset: Configuration space offset
 * @valuep: Output pointer for value
 * @size: Access size
 *
 * This is a placeholder to satisfy the U-Boot PCI subsystem which requires
 * a read_config callback. Returns 0 for all reads.
 *
 * Return: Always 0
 */
static int rp1_pcie_read_config(const struct udevice *bus, pci_dev_t bdf,
				uint offset, ulong *valuep,
				enum pci_size_t size)
{
	/*
	 * PCI subsystem requires read_config callback even for devices
	 * that don't implement custom configuration space access.
	 * Return 0 to indicate successful read of default value.
	 */
	*valuep = 0;
	return 0;
}

/* PCI operations for RP1 */
static const struct dm_pci_ops rp1_pcie_ops = {
	.read_config = rp1_pcie_read_config,
};

/* U-Boot driver definition for RP1 */
U_BOOT_DRIVER(rp1_driver) = {
	.name		= RP1_DRIVER_NAME,
	.id		= UCLASS_PCI_GENERIC,
	.probe		= rp1_probe,
	.bind		= rp1_bind,
	.priv_auto	= sizeof(struct rp1_dev),
	.ops		= &rp1_pcie_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};

/* PCI driver binding */
U_BOOT_PCI_DEVICE(rp1_driver, rp1_pci_ids);
