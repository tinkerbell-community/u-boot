# Fix: Ethernet Device Enumeration on Raspberry Pi 5

## Problem Description

U-Boot on Raspberry Pi 5 was unable to find ethernet devices, even though:
- The RP1 MFD driver was implemented and enabled
- The device tree contained the ethernet node
- The MACB ethernet driver had RP1 support
- All necessary configurations were enabled

### Error Output
```
U-Boot> dhcp
drivers/core/uclass.c:346-uclass_find_device_by_seq() 0
drivers/core/uclass.c:361-uclass_find_device_by_seq()    - not found
...
net/eth-uclass.c:341-            eth_init() No ethernet found.
```

## Root Cause

The RP1 MFD driver (`drivers/mfd/rp1.c`) was not binding its child devices from the device tree. The driver's `bind()` function only set the device name but did not scan for child devices.

### Device Tree Structure
```
pcie@1000120000 (PCIe controller)
└── rp1_nexus (compatible = "pci1de4,1")  ← RP1 MFD driver binds here
    └── pci-ep-bus@1 (compatible = "simple-bus")
        ├── ethernet@40100000 (compatible = "raspberrypi,rp1-gem")
        ├── clocks@40018000 (compatible = "raspberrypi,rp1-clocks")
        ├── pinctrl@400d0000 (compatible = "raspberrypi,rp1-gpio")
        ├── usb@40200000
        └── usb@40300000
```

Without calling `dm_scan_fdt_dev()`, the `pci-ep-bus@1` and its children were never bound, so no ethernet device was available.

## Solution

Added `dm_scan_fdt_dev()` call in the RP1 driver's `bind()` function to scan and bind child devices from the device tree.

### Code Changes

**File:** `drivers/mfd/rp1.c`

```c
static int rp1_bind(struct udevice *dev)
{
	int ret;

	device_set_name(dev, RP1_DRIVER_NAME);

	/* Scan and bind child devices from device tree
	 * The RP1 MFD contains child devices (GPIO, clocks, Ethernet, USB)
	 * that need to be bound and probed
	 */
	ret = dm_scan_fdt_dev(dev);
	if (ret) {
		dev_err(dev, "Failed to bind child devices: %d\n", ret);
		return ret;
	}

	return 0;
}
```

## How It Works

1. **Board Late Init** (`board/raspberrypi/rpi/rpi.c`):
   - Calls `dm_pci_find_device()` to locate RP1 device
   - Calls `device_probe()` to initialize RP1 MFD driver

2. **RP1 Bind** (`drivers/mfd/rp1.c`):
   - Sets device name
   - Calls `dm_scan_fdt_dev()` to bind child devices from DT
   - Binds `pci-ep-bus@1` with simple-bus driver

3. **Simple Bus Post-Bind** (`drivers/core/simple-bus.c`):
   - Automatically called for `pci-ep-bus@1` node
   - Calls `dm_scan_fdt_dev()` again to bind its children
   - Binds ethernet, GPIO, clocks, USB devices

4. **Ethernet Device Available**:
   - MACB driver binds to `ethernet@40100000`
   - Device appears in `dm tree` and is available for `dhcp` command

## Testing

### Verification Steps

1. **Build U-Boot:**
   ```bash
   export CROSS_COMPILE=aarch64-none-elf-
   export HOSTLDLIBS_mkimage="-lssl -lcrypto"
   make rpi_5_defconfig
   make -j$(nproc)
   ```

2. **Flash to SD Card** and boot Raspberry Pi 5

3. **Check Device Tree in U-Boot:**
   ```
   U-Boot> fdt list /axi/pcie@1000120000/rp1_nexus/pci-ep-bus@1
   ```
   Should show child devices (ethernet, GPIO, clocks, etc.)

4. **Check Device Model Tree:**
   ```
   U-Boot> dm tree | grep -A10 rp1
   ```
   Should show:
   ```
    pci_generic_drv  [ + ]   |       |-- rp1
    simple_bus       [ + ]   |       |   `-- pci-ep-bus@1
    clk_rp1          [   ]   |       |       |-- clocks@40018000
    rp1_gpio         [   ]   |       |       |-- pinctrl@400d0000
    eth_macb         [ + ]   |       |       |-- ethernet@40100000
    ...
   ```

5. **Test Ethernet:**
   ```
   U-Boot> dhcp
   ```
   Should successfully obtain IP address and download files

### Expected Output (Success)

```
U-Boot> dhcp
MACB: PHY present at 1
MACB: PHY up
MACB: Starting link... Link up
DHCP client bound to address 192.168.1.100
```

## Files Modified

- `drivers/mfd/rp1.c` - Added `dm_scan_fdt_dev()` call in bind function

## Dependencies

- `CONFIG_MFD_RP1=y` - RP1 MFD driver enabled
- `CONFIG_CLK_RP1=y` - RP1 clock driver enabled
- `CONFIG_RP1_GPIO=y` - RP1 GPIO driver enabled
- `CONFIG_MACB=y` - MACB Ethernet driver enabled
- `CONFIG_SIMPLE_BUS=y` - Simple bus driver enabled (default)

## References

- U-Boot Device Model: `doc/develop/driver-model/design.rst`
- Simple Bus Driver: `drivers/core/simple-bus.c`
- RP1 Device Tree: `dts/upstream/src/arm64/broadcom/rp1-nexus.dtsi`
- Community Patches: `.github/instructions/community-patches.instructions.md`
