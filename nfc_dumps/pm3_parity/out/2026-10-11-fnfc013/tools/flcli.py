import sys, time, serial
PORT="/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00"
def main():
    cmds=sys.argv[1:]
    s=serial.Serial(PORT,230400,timeout=2)
    time.sleep(0.2); s.write(b"\r\n"); time.sleep(0.3); s.reset_input_buffer()
    for c in cmds:
        s.write((c+"\r\n").encode()); time.sleep(1.0)
        data=b""; t=time.time()
        while time.time()-t<4:
            n=s.in_waiting
            if n: data+=s.read(n); t=time.time()
            else: time.sleep(0.05)
        sys.stdout.write(data.decode(errors="replace"))
    s.close()
main()
