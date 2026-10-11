import time, serial, sys
PORT="/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00"
cmd=sys.argv[1].encode(); OUT=sys.argv[2]
s=serial.Serial(PORT,230400,timeout=0.2); time.sleep(0.4); s.reset_input_buffer()
s.write(b"\r\n"); time.sleep(0.3)
s.write(cmd+b"\r\n")
out=open(OUT,"wb"); idle=time.time()+300; last=time.time()
while time.time()<idle:
    try: n=s.in_waiting
    except OSError as e:   # device rebooted / USB dropped in the middle of the run
        out.write(f"\n[unit_tests_run] USB lost at {time.strftime('%H:%M:%S')}: {e}\n".encode()); break
    if n:
        d=s.read(n); out.write(d); out.flush(); last=time.time()
        if b"Failed tests" in d or b"Status:" in d or b"Consumed:" in d:
            t2=time.time()+4
            while time.time()<t2:
                m=s.in_waiting
                if m: out.write(s.read(m)); out.flush()
                else: time.sleep(0.05)
            break
    else:
        time.sleep(0.05)
        if time.time()-last>40: break
out.close(); s.close(); print("done ->",OUT)
