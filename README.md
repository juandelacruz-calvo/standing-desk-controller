# Standing Desk Controller

A DIY standing desk height controller built with a **Wemos D1 Mini** (ESP8266) and an HC-SR04 ultrasonic sensor. It replaces (or augments) your desk's stock controller with programmable memory positions and closed-loop height targeting.

## Features

- **Closed-loop height control** using an HC-SR04 ultrasonic distance sensor mounted under the desk, pointing at the floor
- **3 memory presets** — short press to recall, long press to save current height
- **Manual up/down** — hold to move, release to stop
- **EEPROM persistence** — saved positions survive power cycles
- **Safety** — 30-second movement timeout, any button press cancels an auto-move
- **Serial debug output** — real-time height and status at 115200 baud
- **3D-printable enclosure** — parametric OpenSCAD design with flex buttons and slide-in PCB rails

## Hardware

### Components

| Component | Qty | Notes |
|---|---|---|
| Wemos D1 Mini (ESP8266) | 1 | USB serial; CP2102 or CH340 depending on clone |
| HC-SR04 ultrasonic sensor | 1 | Must be powered from 5V |
| Tactile push buttons | 5 | Connected between pin and GND |
| Existing desk controller | 1 | 3-pin: GND, UP, DOWN (active LOW) |

### Wiring

Pin names are **D0–D8** as silkscreened on the D1 Mini (not the raw GPIO number).

```
Wemos D1 Mini         HC-SR04
-------------         -------
D5  ───────────────── TRIG
D6  ───────────────── ECHO  (see note below if sensor runs at 5 V)
5V  ───────────────── VCC
GND ───────────────── GND

Wemos D1 Mini         Desk Motor Controller
-------------         ---------------------
D7  ───────────────── UP    (sink to GND when active)
D1  ───────────────── DOWN  (sink to GND when active)
GND ───────────────── GND

Wemos D1 Mini         Buttons (to GND)
-------------         ----------------
D2  ───────────────── UP button
D4  ───────────────── DOWN button
D8  ───────────────── MEM1 button
D3  ───────────────── MEM2 button
D0  ───────────────── MEM3 button
```

Buttons use the internal pull-up resistors (`INPUT_PULLUP`), so each button simply connects its pin to GND when pressed. No external resistors needed.

> **Power:** The HC-SR04 should be powered from **5 V** for reliable ranging. The D1 Mini provides 5 V on the `5V` pin when USB or external 5 V is present.

> **ECHO and 3.3 V GPIO:** Many HC-SR04 modules output a **5 V** pulse on `ECHO`. ESP8266 inputs are **3.3 V max**. Use a two-resistor voltage divider (e.g. 10 kΩ / 20 kΩ), a level shifter, or a 3.3 V–tolerant sensor so `ECHO` does not exceed 3.3 V on **D6**.

> **Boot pins:** **D3** (GPIO0) and **D8** (GPIO15) affect boot mode. Use normally-open buttons to GND; do not hold **D3** low while resetting or flashing. After boot, they behave like other inputs.

## Software Setup

### Prerequisites

- [uv](https://docs.astral.sh/uv/) (Python package manager)

### Build and Flash

```bash
# Install dependencies and build
uv run pio run

# Build and upload to the board
uv run pio run -t upload

# Open serial monitor
uv run pio device monitor

# Clean build artifacts
uv run pio run -t clean
```

On Windows PowerShell, chain commands with `;` instead of `&&`:

```powershell
uv run pio run -t clean; uv run pio run
```

### Configuration

Edit the constants at the top of `src/main.cpp`:

| Constant | Default | Description |
|---|---|---|
| `HEIGHT_TOLERANCE_CM` | `0.5` | How close to target before stopping (cm) |
| `MOVE_TIMEOUT_MS` | `30000` | Max movement duration before auto-stop (ms) |
| `MAX_DISTANCE_CM` | `200` | HC-SR04 max range (cm) |
| `SENSOR_INTERVAL_MS` | `100` | How often to read the sensor during movement (ms) |

### WiFi and MQTT (Home Assistant)

1. **Wi‑Fi** is configured with **[WiFiManager](https://github.com/tzapu/WiFiManager)** (captive portal). On boot, if the board is not yet on your network, connect a phone or PC to the access point **`StandingDesk-Setup`**, open the captive page (often `192.168.4.1`), choose your home Wi‑Fi, and fill in **MQTT broker**, **port**, optional **user/password**, and **topic prefix**. Saving applies Wi‑Fi credentials and, when you use “Save” in the portal, persists MQTT settings into **EEPROM** (memory presets still use the first bytes of the same EEPROM).

2. **Optional** `src/secrets.h`: copy from `secrets.h.example`. If EEPROM has **no** saved MQTT block yet (first flash), defaults from `secrets.h` are used for the MQTT fields shown in the portal. You can leave everything empty and set MQTT only in the portal.

3. Flash the firmware. On first successful MQTT connection the device publishes **Home Assistant MQTT discovery** for two sensors (height cm, state text) and nine **buttons** (up, down, stop, three presets, three saves). In Home Assistant, add the **MQTT** integration pointed at your broker; new entities should appear automatically under a device named **Standing Desk**.

4. **State topics** (prefix defaults to `standing-desk` or your chosen prefix): `…/height`, `…/state`, `…/target`, `…/preset1`–`…/preset3`, and availability on `…/status` (`online` / `offline` LWT).

5. **Commands:** publish a plain text payload to `…/cmd`:

   | Payload | Action |
   |---|---|
   | `up` / `down` | Start moving (same idea as MQTT buttons; send `stop` when done) |
   | `stop` | Stop motor / cancel auto move |
   | `preset1` … `preset3` | Go to stored height |
   | `save1` … `save3` | Save current height to that preset |
   | `move_to 75.5` | Closed-loop move to height (cm) |

6. **Manual move from HA:** use the **Desk up** / **Desk down** buttons, then **Desk stop** when the desk is where you want it (mirrors “hold to move” on the physical panel).

7. **Change Wi‑Fi later:** erase saved Wi‑Fi (e.g. flash erase or use a small sketch that calls `WiFiManager`’s reset), or connect to the AP again if the portal is triggered when the stored network is unreachable (depends on WiFiManager behavior).

### Upload Notes (ESP8266)

If uploads fail:

- Install the USB–serial driver for your board (**CP210x** or **CH340** are common on D1 Mini clones).
- Set `upload_port` / `monitor_port` in `platformio.ini` to your COM port if auto-detect fails.
- Hold **RESET** briefly after starting upload if the port is busy; some boards need **GPIO0** low for flash mode (D1 Mini often handles this over USB).
- Use a USB cable that supports **data**, not charge-only.

## Button Controls

| Button | Short Press | Long Press (1s) |
|---|---|---|
| UP | Move desk up (hold) | — |
| DOWN | Move desk down (hold) | — |
| MEM1 | Move to saved position 1 | Save current height as position 1 |
| MEM2 | Move to saved position 2 | Save current height as position 2 |
| MEM3 | Move to saved position 3 | Save current height as position 3 |

Pressing any button during an auto-move cancels it immediately.

## 3D-Printable Enclosure

The `enclosure.scad` file contains a parametric enclosure designed in [OpenSCAD](https://openscad.org/):

- Rectangular box screws under the desk (hidden from view)
- Angled front panel extends past the desk edge with flex buttons
- HC-SR04 sensor holes on the bottom face
- Living-hinge button caps (circular slits with a flex bridge)
- Slide rails for a button PCB strip
- USB and wire exit slots on the back

All dimensions are parametric — adjust the variables in the `[Customizer]` sections at the top of the file to fit your components.

### Printing Tips

- Print the enclosure upside down (top face on the bed) for a clean button surface
- Use 0.2mm layer height or finer for the flex button slits
- PLA or PETG both work; PETG has better flex life for the living hinges
- The 1mm button slits may need slight tuning depending on your printer's accuracy

## Serial Output

On boot, the controller prints a diagnostic summary:

```
===========================
 Standing Desk Controller
===========================
[Pins] Wemos D1 Mini
  Sensor:  TRIG=D5(GPIO14) ECHO=D6(GPIO12)
  Motor:   UP=D7(GPIO13) DN=D1(GPIO5)
  Buttons: UP=D2(GPIO4) DN=D4(GPIO2) M1=D8(GPIO15) M2=D3(GPIO0) M3=D0(GPIO16)
[Sensor test]
  HC-SR04: OK  Height=72.3 cm
[Memory]
  MEM1: 74.0 cm
  MEM2: 110.5 cm
  MEM3: 90.0 cm
[Config]
  Tolerance: 0.5 cm
  Timeout:   30 s
  Max dist:  200 cm
===========================
Ready.
```

During operation it prints the current height every 2 seconds when idle, plus all state changes (moves, saves, cancels, timeouts).

## Project Structure

```
├── src/
│   └── main.cpp          # Arduino firmware
├── enclosure.scad        # 3D-printable enclosure (OpenSCAD)
├── platformio.ini        # PlatformIO build configuration
├── pyproject.toml        # Python/uv project (for PlatformIO CLI)
├── LICENSE               # CC BY-NC-SA 4.0
└── README.md
```

## License

This project is licensed under [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](https://creativecommons.org/licenses/by-nc-sa/4.0/).

You are free to share and adapt this work for non-commercial purposes, with attribution and under the same license.
