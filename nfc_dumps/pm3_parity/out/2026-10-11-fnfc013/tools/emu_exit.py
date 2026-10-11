import sys,time
sys.path.insert(0, sys.argv[1]); sys.path.insert(0, sys.argv[1]+"/pb")
import flipper_pb2  # noqa
from rpc import RPC
r=RPC()
r.app_exit(); time.sleep(0.5)
for _ in range(4): r.input('back','tap'); time.sleep(0.2)
r.stop_session(); r.close()
print("emulation exited, at desktop")
