# SDP810 Reader – inhale detector for ESP32-C3

**English** | [Türkçe](README.tr.md)

This project turns an **ESP32-C3** board and a Sensirion **SDP810** pressure sensor into an
inhale detector. When you draw air through a tube connected to the sensor, the light on the
board fades to red. Blowing into the tube does not turn it on.

The light stands in for a heating coil, for example in an electronically activated hookah.
A program for your PC shows the pressure live as a graph and saves it to a file.

![Dashboard showing seven detected inhales](docs/dashboard.png)

*Seven gentle pulls recorded by the board. Shaded areas show when the light was on.*

> **This is a demo.** Use the light to see when a coil would heat. Do not connect a heater
> until you have read [Safety](#safety).

## What you need

- An **ESP32-C3 board with a built-in RGB light**, for example the ESP32-C3-DevKitM-1
- A **Sensirion SDP810-500Pa** sensor
- **4 jumper wires**
- A **USB cable that carries data**. Some cables only charge, and the PC cannot see the board
  through them.
- A **Windows PC** with an internet connection. macOS and Linux work too: see
  [For developers](#for-developers).
- A **tube** that fits the sensor's ports
- Optional but recommended: two resistors of 4.7 kΩ to 10 kΩ (see step 1)

You do not need to install anything beforehand. The installer sets everything up.

## Step 1: Connect the sensor

Unplug the board from USB first. Then connect the four wires:

| Sensor pin | Board pin |
|------------|-----------|
| VDD        | **3V3** (3.3 V, not 5 V) |
| GND        | **GND** |
| SDA        | **4** (GPIO4) |
| SCL        | **9** (GPIO9) |

For a more reliable connection, add one resistor from SDA to 3V3 and another from SCL to 3V3.

Connect the tube to the sensor port marked **"+"**. This is the side you draw air through.
Leave the **"−"** port open, but keep it away from wind and breath. Air blowing into the "−"
port looks to the sensor exactly like an inhale.

## Step 2: Download and install

1. On this GitHub page, click the green **Code** button, then **Download ZIP**.
2. Right-click the downloaded ZIP file and choose **Extract All**. The installer does not work
   from inside the ZIP.
3. Plug the board into the PC with the USB cable.
4. Open the extracted folder and double-click **`install.bat`**.

If Windows shows **"Windows protected your PC"**, click **More info**, then **Run anyway**.
This appears for files downloaded from the internet.

The installer shows its progress in a black window, in four steps:

| Step | What happens | What you do |
|------|--------------|-------------|
| 1/4 | Looks for Python, a program the PC tool needs | If it asks to install Python, press **Y**. This downloads about 27 MB from python.org and needs no administrator rights |
| 2/4 | Installs the PC tool's components | Wait. The first time takes a minute or two |
| 3/4 | Finds the board | Nothing, unless it asks for a COM port (see [Troubleshooting](#troubleshooting)) |
| 4/4 | Copies the firmware onto the board | Wait until it says **Done** |

At the end it asks **"Start the live plotter now?"**. Press **Y** to open the graph.

## Step 3: Check that it works

1. **Green flash:** press the **RESET** button on the board. The light flashes green once
   when the board starts.
2. **Inhale:** draw air gently through the tube. After about a quarter of a second, the light
   fades up to red. When you stop, it fades out.
3. **Blow:** blow into the tube. The light stays off.

In the plotter window, the top graph shows the pressure. Drawing air makes the line go down,
and blowing makes it go up. Blue shading shows when the light was on.

## Using the plotter

To open the plotter again later, double-click **`run.bat`** in the `tools` folder.

While the plotter window is selected, you can press these keys:

| Key | What it does |
|-----|--------------|
| Space | Pause or resume the graph (recording continues) |
| `1` `2` `3` `4` | Mark what you are doing now: inhale, blow, wind, other |
| `0` | Remove the mark |
| `z` / `x` | Set the current pressure as zero / undo that |
| `c` | Clear the graph and the numbers |
| `s` | Save a picture of the window |

Everything is saved in the `tools\logs` folder, in files you can open in Excel. The marks you
make with `1` to `4` are saved too. They help to adjust the detector later.

Close the plotter before you run `install.bat` again. Only one program at a time can talk to
the board.

## Troubleshooting

| Problem | What to try |
|---------|-------------|
| The installer asks you to type a COM port | Check the USB cable carries data, or try another cable. Install the [CP210x driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers), unplug the board, plug it in again and rerun `install.bat`. To find the port yourself, open **Device Manager** and look under **Ports (COM & LPT)** |
| "Flashing failed" | Close the plotter and any other program using the board, such as Arduino IDE. If that does not help, hold the **BOOT** button, press and release **RESET**, release **BOOT**, and run the installer again |
| "Python 3.9 or newer is required" | Install Python 3.12 from [python.org](https://www.python.org/downloads/). In the first screen of its installer, tick **Add python.exe to PATH**. Then run `install.bat` again |
| The graph stays flat while you inhale | Check the four sensor wires, especially SDA on pin 4 and SCL on pin 9. Unplug and replug the USB cable after fixing them |
| No green flash at reset | The board may have its light on a different pin. Open an issue on GitHub with your board's name |
| The light turns on by itself | Air movement reaches the sensor. Move it away from fans and windows, and shield the "−" port |
| Inhales are not detected | Watch the graph while you draw air. The line must go down. If it goes up, the tube is on the wrong port: move it to the "+" port |

## Safety

Read this before you connect anything that heats.

- **Never connect a coil directly to the board.** The coil output (pin **10**) can supply only
  a few milliamps. A coil needs a switching transistor (a logic-level MOSFET), its own power
  supply and a 10 kΩ resistor from the MOSFET's gate to GND. The resistor keeps the coil off
  while the board starts.
- **Add a thermal fuse** in series with the coil. It cuts the power if the coil overheats,
  even if the board stops working.
- **Test for false triggers first.** The detector reacts to very small pressures, so air
  movement in a room can turn it on. Leave it alone for a while and walk past it. With a
  real coil, every false trigger means heating.
- **Protect the sensor** from smoke and condensation with a water-repellent membrane filter
  in the tube.

The firmware limits each activation to 10 seconds and switches off if the sensor stops
responding. These limits are in software, so they do not protect you if the board itself
fails.

## Limitations

- Wind on the "+" port, or air reaching the "−" port, can look like an inhale.
- Very loud bass near the sensor, around 110 dB or more, could cause missed inhales. This
  comes from a simulation and has not been tested on hardware.
- The settings come from a small number of test recordings, not from a complete hookah.

---

## For developers

### Board pins

| ESP32-C3 | Function |
|----------|----------|
| GPIO4 / GPIO9 | SDP810 SDA / SCL, I2C at 100 kHz, address 0x25 |
| GPIO8 | WS2812 RGB LED: red while the output is on, green flash at boot |
| GPIO10 | Coil output, 1 kHz PWM, 10-bit |

The pins are defined at the top of [main/main.c](main/main.c). The sensor measures
`pressure("+" port) − pressure("−" port)`, so an inhale on the "+" port reads negative,
which matches the default `inhale_sign = -1`.

### macOS / Linux setup

```bash
cd tools
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python -m esptool --chip esp32c3 --port /dev/ttyUSB0 --baud 460800 \
    write_flash 0x0 ../firmware/sdp810_reader_esp32c3.bin
./run.sh                                         # live plotter
./run.sh -p /dev/ttyUSB0 -w 60 -a 0.5            # port, 60 s window, 0.5 s moving average
./run.sh -f ../examples/board_recording_pulls.csv  # open a saved log
```

On macOS the port looks like `/dev/cu.usbserial-XXXX`.

### How the detector works

The detector is [main/puff_detector.c](main/puff_detector.c), plain C with no ESP-IDF
dependency, so the host tests and the replay tool run the same code as the firmware. Each
sample goes through these steps:

1. **Low-pass filter** with a 30 ms time constant
2. **Blow check:** more than 1 Pa in the blowing direction locks out for 0.3 s, and turns the
   output off at once if it was on
3. **Arm** above 0.8 Pa, then **confirm:** the signal must stay up for 250 ms and reach 1 Pa
4. **On** until the signal stays below 0.5 Pa for 150 ms
5. **Limits:** 10 s maximum on time, output off after 3 failed sensor reads or a 200 ms gap
   in samples

The output then fades in over 0.3 s and out over 0.5 s ([main/output_ramp.h](main/output_ramp.h)).
A sensor failure cuts it at once.

The defaults are in `puff_default_config()`. The wind filters (rise-speed limit and wobble
limit) are in the code but **off by default**, because hard pulls overlap the wind recorded
so far. Set `max_slew_pa_s`, `max_active_slew_pa_s` and `max_roughness` to finite values to
enable them.

### Tuning with recordings

Record with the plotter and label what you do with keys `1` to `4`. Then replay the log
through the detector with different settings, without reflashing:

```bash
tools/replay.sh test                                         # detector tests
tools/replay.sh examples/labelled_inhale_blow_wind.csv       # current defaults
tools/replay.sh examples/labelled_inhale_blow_wind.csv on=3 reverse=2
```

Options: `sign arm on off reverse rough slew aslew confirm release lockout`. The replay
prints every activation and rejection, and a table of activations per label. `replay.sh`
needs a C compiler (macOS or Linux, or WSL on Windows).

In `examples/labelled_inhale_blow_wind.csv` the `state` and `reason` columns come from an
older firmware version. Use the replay tool to see what the current detector does with it.

### UART output

115200 baud, one line per sample:

```text
D,<device_ms>,<pressure_pa>,<temperature_c>,<state>,<reason>,<output_percent>
```

`state`: 0 idle, 1 confirming, 2 active, 3 locked out.
`reason`: 0 none, 1 released, 2 reverse, 3 dip, 4 rough, 5 weak, 6 max_time, 7 data_gap,
8 slew. ESP-IDF log lines are mixed in and do not start with `D,`.

### Building from source

Requires [ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32c3/get-started/).

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

The LED driver (`espressif/led_strip`) is downloaded on the first build. To refresh the
prebuilt firmware used by `install.bat`:

```bash
cd build
python -m esptool --chip esp32c3 merge_bin -o ../firmware/sdp810_reader_esp32c3.bin @flash_args
```

### Project layout

```text
main/
  main.c              sensor loop, LED, coil PWM
  sdp8xx.c/.h         SDP8xx I2C driver
  puff_detector.c/.h  inhale detector (plain C)
  output_ramp.h       fade in and fade out
firmware/             prebuilt firmware for install.bat
tools/
  sdp_plotter.py      live dashboard and CSV logger
  puff_replay.c       runs logs through the detector
  test_puff_detector.c
  run.sh / run.bat / replay.sh / find_port.py
examples/             sample recordings
install.bat           Windows installer
```
