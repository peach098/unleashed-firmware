import sys, time, serial
sys.path.insert(0, sys.argv[0].rsplit('/',1)[0] + '/pb')
import flipper_pb2, gui_pb2, application_pb2, system_pb2, storage_pb2

PORT="/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00"

class RPC:
    def __init__(self):
        self.s = serial.Serial(PORT, 230400, timeout=0.3)
        time.sleep(0.2); self.s.reset_input_buffer()
        self.s.write(b"\r\n"); time.sleep(0.2)
        # read prompt
        self._drain(0.5)
        self.s.write(b"start_rpc_session\r"); time.sleep(0.6)
        self._drain(0.5)   # discard echo/newlines
        self.cmd_id = 0
    def _drain(self, t=0.3):
        end=time.time()+t
        while time.time()<end:
            n=self.s.in_waiting
            if n: self.s.read(n); end=time.time()+0.1
            else: time.sleep(0.02)
    def _wv(self, val):
        out=bytearray()
        while True:
            b=val & 0x7f; val >>= 7
            if val: out.append(b|0x80)
            else: out.append(b); break
        return bytes(out)
    def _rv(self):
        shift=0; res=0
        while True:
            c=self.s.read(1)
            if not c: raise TimeoutError("varint read timeout")
            b=c[0]; res |= (b&0x7f)<<shift
            if not (b&0x80): break
            shift+=7
        return res
    def send(self, main, has_next=False):
        self.cmd_id += 1
        main.command_id = self.cmd_id
        main.has_next = has_next
        data = main.SerializeToString()
        self.s.write(self._wv(len(data)) + data); self.s.flush()
    def recv(self, timeout=5.0):
        self.s.timeout=timeout
        ln=self._rv()
        buf=b""
        while len(buf)<ln:
            chunk=self.s.read(ln-len(buf))
            if not chunk: raise TimeoutError("payload read timeout")
            buf+=chunk
        m=flipper_pb2.Main(); m.ParseFromString(buf)
        return m
    # --- helpers ---
    def ping(self):
        m=flipper_pb2.Main(); m.system_ping_request.CopyFrom(system_pb2.PingRequest())
        self.send(m); r=self.recv(); return r.WhichOneof("content")
    def screenshot(self):
        m=flipper_pb2.Main(); m.gui_start_screen_stream_request.CopyFrom(gui_pb2.StartScreenStreamRequest())
        self.send(m)
        # read messages until we get a screen_frame
        frame=None
        for _ in range(10):
            r=self.recv()
            if r.WhichOneof("content")=="gui_screen_frame":
                frame=r.gui_screen_frame.data; break
        # stop stream
        m2=flipper_pb2.Main(); m2.gui_stop_screen_stream_request.CopyFrom(gui_pb2.StopScreenStreamRequest())
        self.send(m2); self._drain(0.3)
        return frame
    def render(self, data):
        if not data: return "(no frame)"
        W,H=128,64; lines=[]
        for y in range(0,H,2):
            row=[]
            for x in range(0,W,2):
                def px(xx,yy):
                    idx=(yy//8)*W+xx
                    return (data[idx]>>(yy%8))&1 if idx<len(data) else 0
                c=px(x,y)+px(x+1,y)+px(x,y+1)+px(x+1,y+1)
                row.append(' ' if c==0 else ('.' if c<=1 else ('o' if c<=2 else '#')))
            lines.append(''.join(row))
        return '\n'.join(lines)
    def input(self, key, typ):
        kmap={'up':0,'down':1,'right':2,'left':3,'ok':4,'back':5}
        tmap={'press':0,'release':1,'short':2,'long':3}
        for t in ('press','short','release') if typ=='tap' else (typ,):
            m=flipper_pb2.Main()
            ev=gui_pb2.SendInputEventRequest(); ev.key=kmap[key]; ev.type=tmap[t]
            m.gui_send_input_event_request.CopyFrom(ev)
            self.send(m); 
            try: self.recv(timeout=2)
            except Exception: pass
            time.sleep(0.05)
    def app_start(self, name, args):
        m=flipper_pb2.Main()
        req=application_pb2.StartRequest(); req.name=name; req.args=args
        m.app_start_request.CopyFrom(req)
        self.send(m)
        r=self.recv(timeout=5)
        return r.command_status, r.WhichOneof("content")
    def stop_session(self):
        m=flipper_pb2.Main(); m.stop_session.CopyFrom(flipper_pb2.StopSession())
        self.send(m)
        try: self.recv(timeout=2)
        except Exception: pass
        self._drain(0.3)
    def app_exit(self):
        m=flipper_pb2.Main(); m.app_exit_request.CopyFrom(application_pb2.AppExitRequest())
        self.send(m)
        try: self.recv(timeout=2)
        except Exception: pass
    def close(self):
        try:
            self._drain(0.2); self.s.close()
        except Exception: pass

if __name__=="__main__":
    r=RPC()
    print("ping ->", r.ping())
    f=r.screenshot()
    print("frame bytes:", len(f) if f else None)
    print(r.render(f))
    r.close()

def frame_to_png(data, path, scale=5):
    from PIL import Image
    W,H=128,64
    img=Image.new("L",(W,H),0)
    px=img.load()
    for y in range(H):
        for x in range(W):
            idx=(y//8)*W+x
            on=(data[idx]>>(y%8))&1 if idx<len(data) else 0
            px[x,y]=255 if on else 0
    img=img.resize((W*scale,H*scale), Image.NEAREST)
    img.save(path)
