"""Start NFC emulation of an .nfc file on the Flipper SD via RPC (as 01.3 emu_open.py, file as argument).
usage: emu_open_file.py <01.3 tools dir> </ext/nfc/... .nfc>"""
import sys, time
S = sys.argv[1]; sys.path.insert(0, S); sys.path.insert(0, S + "/pb")
from rpc import RPC
r = RPC()
print("ping:", r.ping())
for _ in range(4): r.input('back', 'tap'); time.sleep(0.2)
time.sleep(0.3)
st, resp = r.app_start("/ext/apps/NFC/nfc.fap", sys.argv[2])
print("app_start:", st, resp)
time.sleep(2.0)
r.close()
