# DE1-SoC Cyclone V HPS–FPGA IoT Controller  

End-to-end **hardware–software co-design** on the **Terasic DE1-SoC (Cyclone V + ARM HPS)** demonstrating real-time sensor acquisition, FPGA acceleration, Linux userspace control, and networked visualization.

This project integrates a **custom FPGA I²C master**, **PWM controller**, and **HPS Linux application** to read motion data from an **MPU6050**, compute orientation on the ARM processor, and drive LEDs via memory-mapped FPGA registers.

---

## What This Project Demonstrates (Resume-Focused)
- Custom **FPGA I²C controller (FSM-based)** for timing-critical sensor communication  
- **HPS–FPGA interaction** using AXI bridges and Linux `mmap()` / MMIO  
- Real-time **sensor → compute → actuation** control loop  
- **Oscilloscope-verified I²C timing** (SCL frequency, setup/hold, data transfer)  
- Clean separation of **FPGA acceleration** and **Linux application logic**

---

## System Overview

### FPGA (Programmable Logic)
- **I²C Master IP**
  - Generates START/STOP, ACK/NACK
  - Burst reads from MPU6050 registers
- **PWM Generator**
  - Duty cycle controlled via AXI-mapped registers
  - Drives onboard LEDs based on sensor orientation
- **Platform Designer (Qsys) System**
  - Exposes control/status registers to the HPS

### HPS (ARM Cortex-A9, Embedded Linux)
- Maps FPGA registers using `/dev/mem` and `mmap()`
- Reads raw accelerometer/gyroscope values
- Computes orientation/tilt in userspace C
- Writes PWM duty cycle back to FPGA
- Serves live data over Ethernet (web access)

---

## Repository Structure
```

.
├── fpga-rtl/                 # FPGA design (custom IP + Qsys system)
│   ├── pwm_gen.v             # PWM controller
│   ├── soc_system/           # Platform Designer generated system
│   └── ghrd_top.v            # Top-level FPGA integration
│
├── hps-c/                    # Linux userspace application
│   ├── main.c                # mmap/MMIO access + control logic
│   ├── hps_0.h               # Register definitions
│   └── Makefile
│
├── Images/
│   └── Oscilloscope/         # Hardware validation evidence
│       ├── scl_freq.PNG      # I²C clock frequency verification
│       ├── setup-time.PNG    # Setup/hold timing
│       └── data_tranfer.PNG  # SDA/SCL data transfer waveform
│
├── Module4_test/             # Quartus project for Module 4 integration
│   ├── *.qpf / *.qsf         # Project and constraint files
│   ├── output_files/         # Reports, SOF, timing summaries
│   └── simulation/           # ModelSim RTL/gate simulation
│
└── README.md

```

---

## Build & Run (High-Level)

### FPGA
1. Open the Quartus project in `Module4_test/`
2. Open `soc_system.qsys` in Platform Designer and generate HDL
3. Compile the design and program the FPGA (`.sof`)

### HPS
1. Build the Linux userspace application in `hps-c/`
2. Copy the binary to the DE1-SoC Linux filesystem
3. Run the application to start sensor acquisition and control loop

---

## Verification & Results
- **I²C bus validated on oscilloscope**, confirming:
  - Correct START/STOP sequencing
  - Stable ~100 kHz SCL frequency
  - Proper data and ACK timing
- **Real-time LED brightness modulation** in response to board tilt
- **Successful HPS–FPGA register transactions** verified via MMIO reads/writes
- Resource utilization within Cyclone V limits with timing met

---

## Tools & Technologies
- **Verilog / SystemVerilog**
- **Quartus Prime & Platform Designer (Qsys)**
- **Embedded Linux (ARM Cortex-A9)**
- **C (Linux userspace, MMIO)**
- Oscilloscope-based hardware validation

---

## Academic Context
This repository corresponds to **Module 4 (IoT Control System)** of **Project 3** in  
**ECEN 5863 – Programmable Logic Embedded System Design**.

---

## Author
**Abhishek Nadgir**  
Professional Master’s Student – Embedded Systems Engineering  
University of Colorado Boulder
```

---

### Optional (Strongly Recommended for Resume)

I suggest **one small cleanup commit**:

* Add a `.gitignore` to exclude `db/`, `incremental_db/`, and large Quartus auto-generated files
* Keep **`Images/Oscilloscope`**, **`fpga-rtl`**, and **`hps-c`** visible

If you want, I can:

* Trim this to a **shorter recruiter-friendly README**
* Add **architecture diagrams**
* Rewrite it in **STAR-style bullet points** for your resume

Repo structure source: 
