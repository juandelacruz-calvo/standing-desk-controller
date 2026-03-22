# Standing Desk Controller

A DIY standing desk height controller built with an Arduino Nano and an HC-SR04 ultrasonic sensor. It replaces (or augments) your desk's stock controller with programmable memory positions and closed-loop height targeting.

## Features

- **Closed-loop height control** using an HC-SR04 ultrasonic distance sensor mounted under the desk, pointing at the floor
- **3 memory presets** — short press to recall, long press to save current height
- **Manual up/down** — hold to move, release to stop
- **EEPROM persistence** — saved positions survive power cycles
- **Safety** — 30-second movement timeout, any button press cancels an auto-move
- **Serial debug output** — real-time height and status at 9600 baud
- **3D-printable enclosure** — parametric OpenSCAD design with flex buttons and slide-in PCB rails

## Hardware

### Components

| Component | Qty | Notes |
|---|---|---|
| Arduino Nano (ATmega328P) | 1 | CH340 USB clone works fine |
| HC-SR04 ultrasonic sensor | 1 | Must be powered from 5V |
| Tactile push buttons | 5 | Connected between pin and GND |
| Existing desk controller | 1 | 3-pin: GND, UP, DOWN (active LOW) |

### Wiring

```
Arduino Nano          HC-SR04
------------          -------
D6  ───────────────── TRIG
D7  ───────────────── ECHO
5V  ───────────────── VCC
GND ───────────────── GND

Arduino Nano          Desk Motor Controller
------------          ---------------------
D8  ───────────────── UP    (active LOW)
D9  ───────────────── DOWN  (active LOW)
GND ───────────────── GND

Arduino Nano          Buttons (to GND)
------------          ----------------
D2  ───────────────── UP button
D3  ───────────────── DOWN button
D4  ───────────────── MEM1 button
D5  ───────────────── MEM2 button
D10 ───────────────── MEM3 button
```

Buttons use the internal pull-up resistors (`INPUT_PULLUP`), so each button simply connects its pin to GND when pressed. No external resistors needed.

> **Important:** The HC-SR04 must be powered from the Arduino's **5V** pin, not 3.3V. Insufficient voltage causes the sensor to only read very short distances (< 5 cm).

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

### Upload Notes (CH340 Clones)

Most CH340-based Nano clones use the **old bootloader** at 57600 baud. This is already configured in `platformio.ini`. If uploads fail:

- Make sure you have the CH340 driver installed
- Set `upload_port` in `platformio.ini` to your board's COM port
- Try a different USB cable (must be a data cable, not charge-only)

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
[Pins]
  Sensor:  TRIG=D6 ECHO=D7
  Motor:   UP=D8 DN=D9
  Buttons: UP=D2 DN=D3 M1=D4 M2=D5 M3=D10
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
