# Compact ATtiny85 Open-Source Airsoft EFC

An open-source, compact (~28 mm) Electronic Fire Control (EFC) and MOSFET board designed for Airsoft Automatic Electric Guns (AEGs). Designed completely in **KiCad** using open-source tools, this project aims to provide a reliable, high-performance, programmable DIY alternative to commercial MOSFET units.

---

## Key Features

* **Sensorless Cycle Detection:** Unlike standard EFC units that rely on delicate optical sensors or mechanical microswitches, this board performs cycle monitoring **without additional physical sensors**, minimizing internal moving parts and potential failure points.
* **Compact Round Design:** ~28 mm footprint tailored for tight spaces (e.g., buffer tubes or gearboxes).
* **High-Current Power Stage:** Engineered with massive copper traces, an active gate driver, and a low $R_{DS(on)}$ power MOSFET to easily handle peak motor currents (~40 A+).
* **100% Free & Open Source:** All schematics, PCB layouts, and firmware are freely available and built using open-source toolchains.

---

## Technical Specifications

| Component | Specification |
| :--- | :--- |
| **Microcontroller** | Microchip ATtiny85-20SU |
| **Gate Driver** | TC4428EOA (High-speed dual MOSFET driver) |
| **Power MOSFET** | IRFS7430TRLPBF ($1.3\text{ m}\Omega$, 40 V, D2PAK) |
| **Voltage Regulator** | HT7550-1 (5 V LDO, SOT-89) |
| **Freewheel Diode** | SS510 (5 A / 100 V Schottky) |
| **PCB Specs** | 2-layer, 28mm by 33mm size |

---

## Firmware & Flashing

* **Current Bootloader:** The current firmware revision utilizes the **Micronucleus bootloader** for flashing code via USB.
* **Roadmap Notice:** Future hardware and firmware revisions are planned to transition directly to an **ISP Programmer** interface via dedicated pogo-pin programming pads to maximize available flash memory and streamline assembly.

---

## Testing & Field Status

This hardware and firmware configuration is currently verified and field-tested under the following setups:

* **Gear Ratios:** Tested on **13:1** and **18:1** gear sets.
* **Power Source:** Tested exclusively with **2S LiPo** battery packs (7.4 V nominal / 8.4 V fully charged).

> **Note:** Testing for 3S LiPo (11.1 V) setups and ultra-high-speed gear ratios (e.g., DSG / 12:1) is planned for future iterations.

---

## Project Structure

```text
├── schematics/        # KiCad PCB layout (.kicad_pcb)
├── src/               # Platform.io & Ardnuino compatible ATtiny85 C++ source code
├── docs/              # TODO
└── README.md
```

---

## License

This project is licensed under the **MIT License** – feel free to modify, manufacture, and distribute for personal or commercial use.