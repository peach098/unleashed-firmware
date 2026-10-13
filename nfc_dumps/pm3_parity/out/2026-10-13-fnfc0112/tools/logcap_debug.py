"""FNFC-01.12: attach `log debug` for <seconds> and save the output (collects the deferred probe v5 records).
usage: logcap_debug.py <outfile> <seconds>"""
import sys, time, serial
PORT = "/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00"
s = serial.Serial(PORT, 230400, timeout=1)
time.sleep(0.2); s.write(b"\r\n"); time.sleep(0.3); s.reset_input_buffer()
s.write(b"log debug\r\n"); t0 = time.time()
with open(sys.argv[1], "wb") as f:
    while time.time() - t0 < float(sys.argv[2]):
        n = s.in_waiting
        if n: f.write(s.read(n)); f.flush()
        else: time.sleep(0.02)
s.write(b"\x03"); time.sleep(0.3); s.close()
