import sys, time, serial
PORT="/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00"
outf=sys.argv[1]; dur=float(sys.argv[2])
s=serial.Serial(PORT,230400,timeout=1)
time.sleep(0.2); s.write(b"\r\n"); time.sleep(0.3); s.reset_input_buffer()
s.write((sys.argv[3] if len(sys.argv)>3 else "log info").encode()+b"\r\n"); time.sleep(0.4)
t0=time.time()
with open(outf,"wb") as f:
    while time.time()-t0 < dur:
        n=s.in_waiting
        if n: f.write(s.read(n)); f.flush()
        else: time.sleep(0.02)
s.write(b"\x03"); time.sleep(0.2)   # Ctrl-C to stop log
try: s.close()
except: pass
print("log capture done ->", outf)
