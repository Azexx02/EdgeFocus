# Board Netlist vs Device Tree Notes

Source netlist workspace copy: `D:\Aze\Documents\developMP257\hardware\V2(3).tel`

## Present On This Carrier

- Core board connector: `ATK-CLMP257B-CORE` (`U1`)
- USB hub: `CH334R` (`U16`)
- Wi-Fi/Bluetooth module: `EWM103-WF8723DU` (`U14`)
- USB Type-C controller: `STUSB4500QTR` (`U10`)
- External MCU: `STM32G431RBT6` (`U17`)
- Digital microphone: `ICS-43434` (`U20`)
- Speaker amplifier: `MAX98357A` (`U21`)
- Motor drivers: `STSPIN830` (`U18`, `U19`)
- Display connectors:
  - DSI connector
  - LCD connector
- TF card / SDMMC1 signals
- RTC battery connector and RTC related nets

## Not Seen In This Netlist

These appear in the clean ATK system logs/device tree but are not present in the carrier netlist:

- `ES8388` audio codec
- `IMX335` camera sensor
- `DHT11`
- `DS18B20`
- PCIe endpoint or PCIe connector
- LVDS/RGB display connectors, unless implemented through a separate unlisted adapter

## USB Topology From Netlist

- Core USB host pair:
  - `USBH_D_N`: `U1.C5` to `U16.10`
  - `USBH_D_P`: `U1.C6` to `U16.11`
- Hub downstream to Wi-Fi module:
  - `HUB_D1_N`: `U16.7` to `U14.12`
  - `HUB_D1_P`: `U16.8` to `U14.13`
- Hub downstream to external USB connectors:
  - `HUB_D3_N/P`: `U16.3/4` to `USB2`
  - `HUB_D4_N/P`: `U16.1/2` to `USB1`
- Wi-Fi/BT enable:
  - `WIFI_BT_EN`: `U14.16`, `U14.17`, through `R68`
  - This net does not directly show a core-board GPIO in the netlist excerpt, so the module may be hard-enabled or controlled indirectly.

## Device Tree First-Cut Recommendation

Disable first:

- ES8388 audio codec and related audio graph nodes
- IMX335 camera and DCMIPP/CSI camera path
- DHT11 and DS18B20 nodes
- PCIe node
- Unused VDEC/VENC/GPU/display nodes that cause `/dev/dri/card0` waits
- LVDS/RGB display variants not present on this carrier

Keep or model:

- USB EHCI host and `CH334R` hub path
- RTL8723DU USB Wi-Fi/BT module
- STUSB4500 Type-C controller on I2C4 if the Type-C port is needed
- DSI/LCD connector only if that panel is actually attached
- ICS-43434 microphone and MAX98357A amplifier only after the base system boots reliably
- STM32G431 + STSPIN830 motor subsystem later, after Linux boot is stable

## Working Hypothesis

The clean ATK image is booting a device tree for a richer ATK evaluation board variant. This carrier is a subset/different design. Udev is probing devices described by the DT that are not present here, and one of those probes is stalling long enough for the 32-second secure watchdog to reset the system.
