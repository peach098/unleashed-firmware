"""Capture Flipper `log info` output until <stopfile> exists (or <max seconds>), then Ctrl-C the log.
usage: logcap_until.py <outfile> <stopfile> <max seconds>"""
import os, sys, time, serial
PORT = "/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00"
outf, stopf, dur = sys.argv[1], sys.argv[2], float(sys.argv[3])
s = serial.Serial(PORT, 230400, timeout=1)
time.sleep(0.2); s.write(b"\r\n"); time.sleep(0.3); s.reset_input_buffer()
s.write(b"log info\r\n"); time.sleep(0.4)
t0 = time.time()
with open(outf, "wb") as f:
    while time.time() - t0 < dur and not os.path.exists(stopf):
        n = s.in_waiting
        if n: f.write(s.read(n)); f.flush()
        else: time.sleep(0.02)
    time.sleep(0.5); n = s.in_waiting
    if n: f.write(s.read(n))
s.write(b"\x03"); time.sleep(0.3)
s.close()
print("log capture done ->", outf)
