"""Prints the serial port of the first USB-serial adapter found, for install.bat."""
import sys

import serial.tools.list_ports

KEYWORDS = ("cp210", "silicon labs", "ch34", "ch910", "usb serial", "usb-serial", "uart",
            "usbserial", "usbmodem", "ttyusb", "ttyacm")

ports = list(serial.tools.list_ports.comports())
matches = [p for p in ports
           if any(k in f"{p.device} {p.description} {p.manufacturer or ''}".lower() for k in KEYWORDS)]
for p in ports:
    print(f"  found {p.device}: {p.description}", file=sys.stderr)
if not matches:
    sys.exit(1)
print(matches[0].device)
