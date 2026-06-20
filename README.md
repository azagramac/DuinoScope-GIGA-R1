# ⚡ Arduino DuinoScope GIGA R1 ⚡

<img width="400" height="197" alt="arduino-giga-r1-board" src="https://github.com/user-attachments/assets/349ebc8a-7321-4fd4-be56-a8be39657dad" />


**DuinoScope GIGA R1**, a high-performance, feature-rich dual-channel digital oscilloscope built specifically for the **Arduino GIGA R1 WiFi** board and the **Arduino Giga Display Shield** (800x480 capacitive touch display). 

This project leverages the impressive dual independent **1 Msps ADCs** and hardware peripherals of the STM32H7 microcontroller to deliver a competent, responsive, and aesthetically stunning scope. It also features a real-time web-based control panel accessible over a local WiFi Access Point.

---

## 🚀 Key Features

*   📊 **Dual-Channel Trace Acquisition**: Independent sampling on channels `CH0` (A0 - Yellow trace) and `CH1` (A1 - Cyan trace).
*   ⚡ **High-Speed 1 Msps Sampling**: Exploits dual independent ADCs using hardware-optimized circular DMA buffers.
*   📱 **On-Board Capacitive GUI**: A sleek dashboard built on the Giga Display using `GU_Elements` and `LVGL` styling. Supports advanced touch gestures:
    *   **Pinch Zoom**: Seamlessly scale the Timebase on the fly.
    *   **Vertical Drag**: Adjust vertical trace offsets directly on the screen.
*   🌐 **Wireless Access Point & Captive Portal**:
    *   Generates a standalone hotspot named **`DuinoScope`** (Password: `12345678`).
    *   Captive Portal DNS automatically redirects connected devices to the control dashboard.
*   🖥️ **Real-Time Web Control Panel**:
    *   Responsive mobile-optimized interface with styled HSL color palettes and dark glassmorphism design.
    *   **Telemetry Parameters**: Real-time display of **Frequency (Frec)**, **Peak-to-Peak Voltage (Vp-p)**, **Maximum Voltage (Vmax)**, and **Minimum Voltage (Vmin)** for both channels.
    *   Interactive controls to toggle channels, modify Volts/Div, alter Timebase, and customize Trigger parameters.
*   🚦 **Smart RGB Status LED**:
    *   🔵 **Breathing Blue**: Waiting for WiFi connection / Disconnected.
    *   🔵 **Solid Blue (Dim)**: WiFi remote client connected.
    *   🟢 **Green (Dim)**: Standby with no signal activity.
    *   🟡 **Yellow (Solid)**: CH0 active and traces shown.
    *   🔵/🟢 **Cyan (Solid)**: CH1 active and traces shown.
*   🛡️ **Analog Front End (AFE) Support**: Integrates digital outputs (pins 3, 4, 5, and 6) to configure hardware attenuation and voltage ranges on an optional [Fscope-500k](https://oshwlab.com/fruitloop57/fscope-250k5-v2_copy_copy_copy_copy) daughterboard (handling ± inputs and 10x probes).

---

## 🛠️ Hardware & Port Specifications

| Hardware Component | Pin / Channel | Description / Parameters |
| :--- | :--- | :--- |
| **Channel 0 Input** | `A0` | Yellow trace analog signal input |
| **Channel 1 Input** | `A1` | Cyan trace analog signal input |
| **AFE CH0 Control** | Pins `3` & `4` | Range selection logic signals for CH0 |
| **AFE CH1 Control** | Pins `5` & `6` | Range selection logic signals for CH1 |
| **Trigger Output / Sync** | Pin `2` | Synchronization indicator output pin |
| **Status RGB LEDs** | `LEDR`, `LEDG`, `LEDB` | Active-low PWM-driven status indicators |

---

## 🌐 WiFi AP Configuration

When powered up, DuinoScope starts a secure local hotspot. You can connect your phone or laptop to access the web panel:

*   **SSID**: `DuinoScope`
*   **Password**: `12345678`
*   **Web Dashboard URL**: `http://192.168.3.1/` (Any browser request will automatically redirect here via Captive DNS).
*   **Polling Debounce**: The web UI polling interval is **250ms** with a **3-strike buffer** (750ms total debounce) to ensure telemetry updates remain smooth and avoid flickering connection state alerts on single-packet loss.


## 📱 Screenshots

<img width="300" alt="screenshot1" src="https://github.com/user-attachments/assets/b1fd0d22-37cc-4ee7-ae66-41aff6e92c71" />

<img width="300" alt="screenshot2" src="https://github.com/user-attachments/assets/ff904fb7-db2e-4548-8b01-697ef7f306a5" />

---

## 📚 Library Dependencies

To compile this project, ensure you have the following libraries installed in your Arduino IDE or CLI path:

1.  **`Arduino_AdvancedAnalog`** (v1.5.0): For fast, hardware-level trace buffer acquisition via DMA.
2.  **`lvgl`** (v8.3.11): The UI library used to build the modern graphical interface. Note that version 8.x is required.
3.  **`Arduino_H7_Video`** (v1.0): Built-in library (part of `arduino:mbed_giga` core) to interface with the display hardware.
4.  **`Arduino_GigaDisplayTouch`** (v1.1.0): Touch driver for the Giga Display Shield.
5.  **`Arduino_GigaDisplay_GFX`** (v1.1.0): Display driver interface.
6.  **`WiFi`** (v1.0) & **`WiFiUdp`**: Built-in libraries for local Access Point and DNS captive portal. 

---

## 💾 Installation & Compilation

### Using Arduino CLI

1. Put the Arduino GIGA R1 board in **DFU bootloader mode** by double-pressing the physical **RESET** button (the green LED near the USB port will start breathing).
2. Compile and upload using the command below:

```bash
arduino-cli compile --fqbn arduino:mbed_giga:giga --port 1-3.2 --upload /path/to/DuinoScope-GIGA-R1
```

*(Replace `/path/to/...` and `1-3.2` with your actual workspace folder and DFU port).*

### Using Arduino IDE

1. Open `DuinoScope-GIGA-R1.ino` in the Arduino IDE.
2. In **Tools -> Board**, select **Arduino GIGA R1**.
3. Plug in the board and select the appropriate serial port.
4. Click **Upload**. (If upload fails due to CDC access permissions, double-tap the RESET button to compile/upload in DFU mode).

---

## 📂 Project Structure

```bash
DuinoScope-GIGA-R1/
├── DuinoScope-GIGA-R1.ino  # Main sketch containing logic, display rendering & web server loop
├── duinoscope.h            # Scope structure definitions, timebase indexes, and volts/div parameters
├── duinoscope_html.h       # Embedded C++ raw string literal containing the compiled HTML portal
├── duinoscope_control_panel.html # Source HTML dashboard code (kept as design reference)
├── LICENSE                 # GNU General Public License v3.0
└── README.md               # This project documentation
```

---

## 💖 Acknowledgements

Special thanks to **[gilesp1729](https://github.com/gilesp1729)**, the original author of the code, for laying the foundation of this project and for his excellent work on the display drivers and initial oscilloscope implementation for the Arduino GIGA R1.

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See the [LICENSE](file:///DuinoScope-GIGA-R1/LICENSE) file for the full text.
