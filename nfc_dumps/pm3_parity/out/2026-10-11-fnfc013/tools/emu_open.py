import sys, time
S=sys.argv[1]
sys.path.insert(0, S)
from rpc import RPC, frame_to_png
card="/ext/nfc/SLIX2_2026-02-27_16,08,34.nfc"
r=RPC()
print("ping:", r.ping())
for _ in range(4): r.input('back','tap'); time.sleep(0.2)
time.sleep(0.3)
st,resp=r.app_start("/ext/apps/NFC/nfc.fap", card)
print("app_start:", st, resp)
time.sleep(2.0)
frame_to_png(r.screenshot(), S+"/emu_s1.png")
print("saved emu_s1.png")
r.close()
