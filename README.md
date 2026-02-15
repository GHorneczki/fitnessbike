🚴 DIY Smart Bike Trainer (ESP32 + FTMS + Zwift)

Description: 

A fully controllable DIY Smart Indoor Bike Trainer based on ESP32 (ESP32-C3) implementing the Bluetooth FTMS (Fitness Machine Service) protocol.

The trainer:

✅ Connects to Zwift

✅ Receives real-time grade (incline) data

✅ Controls a stepper motor for physical resistance adjustment

✅ Sends cadence, speed and power over FTMS

✅ Uses a reed/hall sensor for cadence detection

✅ Non-blocking stepper control (no cadence interruption)

📦 Features

Bluetooth FTMS implementation (Indoor Bike Data + Control Point)

Zwift grade simulation (opcode 0x11)

ERG mode support (opcode 0x05)

Reed sensor cadence detection (interrupt driven)

Non-blocking stepper motor control

Adjustable max incline (0–20%)

Stable cadence smoothing

Protection against power spikes

🧠 How It Works
1️⃣ Cadence Measurement

A reed or hall sensor is connected to:

GPIO 10


Each pedal revolution triggers an interrupt.

Cadence is calculated from pulse interval:

RPM = 60000 / interval_ms


Smoothed using exponential filtering to prevent spikes.

2️⃣ Power Calculation

Currently simulated using:

Power = 0.015 × RPM² + 10


This can be replaced with:

Torque sensor data

Load cell

Real power measurement hardware

3️⃣ FTMS Communication

Implements:

Service: 0x1826 (Fitness Machine Service)

Indoor Bike Data: 0x2AD2

Control Point: 0x2AD9

Status: 0x2AD6

Feature: 0x2ACC

Supports:

Opcode	Function
0x00	Request Control
0x01	Reset
0x05	Set Target Power (ERG mode)
0x07	Set Resistance
0x11	Simulation Parameters (grade)

Zwift sends grade in 0.01% resolution.

Example:

0x11 00 00 CE FF 28 33

4️⃣ Stepper Motor Control

Stepper pins:

STEP  → GPIO 3
DIR   → GPIO 2
ZERO  → GPIO 4


The system:

Homes on startup

Moves gradually toward target grade

Enforces minimum step interval (~1400 µs)

Uses non-blocking stepping

Prevents motor buzzing

Grade range:

0% – 20%

🔧 Hardware Requirements

ESP32-C3 (tested on SuperMini)

Stepper motor

Stepper driver (A4988 / DRV8825)

Reed or hall sensor

Mechanical incline mechanism

Optional: limit switch for zeroing

📡 Compatible Apps
App	Status
Zwift	✅ Fully working
MyWhoosh	⚠ Partially tested
⚙ Installation
1️⃣ PlatformIO

Install dependencies:

lib_deps =
    h2zero/NimBLE-Arduino @ ^1.4.3

2️⃣ Flash to ESP32
pio run --target upload

🛠 Configuration

You can adjust:

Maximum incline
grade = constrain(grade, 0.0f, 20.0f);

Minimum step interval
const unsigned long MIN_STEP_INTERVAL_US = 1400;

Cadence smoothing factor
smoothed = smoothed * 0.8f + raw * 0.2f;

⚠ Known Limitations

Power is simulated

No real torque sensing

No advanced ERG control logic yet

No ANT+ support

No calibration routine

🚀 Future Improvements

Real power measurement

Closed-loop ERG control

Acceleration ramp for stepper

OTA firmware update

Web configuration panel

Real physics-based resistance model

📸 Example Output Log
[FTMS] TX: 15.0 km/h  90.0 rpm  150 W
→ Grade 4.36 %

📜 License

GNU Public Licence

🙌 Why This Project?

Commercial smart trainers are expensive.

This project proves that with:

ESP32

Open Bluetooth FTMS

Basic mechanics

you can build a fully controllable smart trainer for a fraction of the cost.
