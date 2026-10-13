"""FNFC-01.12: start SLIX2 card AC emulation via RPC like 01.3 emu_open.py, but without the screenshot
(screen stream) and with an explicit RPC stop_session, so no RPC session is left behind.
usage: emu_open_quiet.py <01.3 tools dir>"""
import sys, time
S = sys.argv[1]; sys.path.insert(0, S); sys.path.insert(0, S + "/pb")
from rpc import RPC
r = RPC()
print("ping:", r.ping())
for _ in range(4): r.input('back', 'tap'); time.sleep(0.2)
time.sleep(0.3)
print("app_start:", *r.app_start("/ext/apps/NFC/nfc.fap", "/ext/nfc/SLIX2_2026-02-27_16,08,34.nfc"))
time.sleep(1.0)
r.stop_session(); r.close()
print("rpc session stopped")
