#!/usr/bin/env python3
"""azahar_rpc.py — read/write the emulated 3DS RAM of a running Azahar via its UDP RPC server.

Azahar's built-in RPC server (core/rpc, enable_rpc_server=true, UDP :45987) exposes ReadMemory,
WriteMemory, ProcessList and SetGetProcess over a tiny packet protocol. This is the SoH3D 3DS
ORACLE backbone: with OoT3D running headless in Azahar we can read its actual actor/animation
state from emulated RAM (3DS *virtual* addresses) — ground truth for matching SoH3D to the 3DS
game (see issue #89, memory soh3d-azahar-oracle).

Wire format (all little-endian):
  request header (16B): version(u32=1) id(u32) type(u32) packet_size(u32=len(data))
  request data: arg1(u32) arg2(u32) [+ write bytes]
  reply: same 16B header echoed back, then packet_size bytes of reply data.
Types: ReadMemory=1 (arg1=vaddr,arg2=size; reply=bytes), WriteMemory=2, ProcessList=3
  (arg1=start,arg2=max; reply=count(u32)+count*ProcessInfo{pid u32, title_id u64, name[8]}),
  SetGetProcess=4 (arg1: 0=get->reply pid u32, 1=set with arg2=pid).

Usage:
  azahar_rpc.py procs                         # list emulated 3DS processes (find OoT3D)
  azahar_rpc.py read <hexaddr> <size>         # hexdump <size> bytes at a 3DS vaddr
  azahar_rpc.py read32 <hexaddr>              # read a single u32 (LE)
As a module: Rpc().processes(), .read(addr,size), .read32(addr), .select(pid).
"""
import socket, struct, sys

HOST, PORT = "127.0.0.1", 45987
VERSION = 1
READMEM, WRITEMEM, PROCLIST, SETGETPROC, SCREENSHOT, INPUT, TOUCH = 1, 2, 3, 4, 5, 6, 7

# 3DS PadState button bits (see Azahar src/core/hle/service/hid/hid.h).
BTN = {
    "a": 1 << 0, "b": 1 << 1, "select": 1 << 2, "start": 1 << 3,
    "right": 1 << 4, "left": 1 << 5, "up": 1 << 6, "down": 1 << 7,
    "r": 1 << 8, "l": 1 << 9, "x": 1 << 10, "y": 1 << 11,
}


def buttons_mask(names):
    """names: iterable or space/plus-separated string of button names -> bitmask."""
    if isinstance(names, str):
        names = names.replace("+", " ").split()
    m = 0
    for n in names:
        n = n.strip().lower()
        if n and n not in BTN:
            raise ValueError(f"unknown button '{n}' (have {', '.join(BTN)})")
        if n:
            m |= BTN[n]
    return m


class Rpc:
    def __init__(self, host=HOST, port=PORT, timeout=2.0):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout)
        self._id = 0

    def _req(self, ptype, arg1=0, arg2=0, extra=b""):
        self._id += 1
        data = struct.pack("<II", arg1, arg2) + extra
        hdr = struct.pack("<IIII", VERSION, self._id, ptype, len(data))
        self.sock.sendto(hdr + data, self.addr)
        pkt, _ = self.sock.recvfrom(65536)
        ver, rid, rtype, size = struct.unpack("<IIII", pkt[:16])
        return pkt[16:16 + size]

    def processes(self):
        out = self._req(PROCLIST, 0, 64)
        (count,) = struct.unpack("<I", out[:4])
        procs = []
        off = 4
        for _ in range(count):
            pid, tid = struct.unpack("<IQ", out[off:off + 12])
            name = out[off + 12:off + 20].split(b"\x00")[0].decode("latin1")
            procs.append((pid, tid, name))
            off += 20
        return procs

    def select(self, pid):
        self._req(SETGETPROC, 1, pid)

    def selected(self):
        out = self._req(SETGETPROC, 0, 0)
        return struct.unpack("<I", out[:4])[0] if len(out) >= 4 else None

    def read(self, addr, size):
        """Read up to 1024 bytes per request; chunks larger reads."""
        buf = b""
        while size > 0:
            n = min(size, 1024)
            out = self._req(READMEM, addr, n)
            if not out:
                break
            buf += out
            addr += len(out)
            size -= len(out)
            if len(out) < n:
                break
        return buf

    def read32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<I", b)[0] if len(b) >= 4 else None

    def read_f32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<f", b)[0] if len(b) >= 4 else None

    def set_input(self, buttons=0, circle=None):
        """Set the HELD pad state. buttons=bitmask (or name string). circle=(cx%,cy%) in -100..100
        or None to release the circle pad. Persists until the next set_input. Requires the Input mod."""
        if isinstance(buttons, str):
            buttons = buttons_mask(buttons)
        circ = 0
        if circle is not None:
            cx = max(-100, min(100, int(circle[0]))) & 0xFF
            cy = max(-100, min(100, int(circle[1]))) & 0xFF
            circ = (1 << 24) | (cx << 8) | cy
        self._req(INPUT, buttons, circ)

    def set_touch(self, active, x=0, y=0):
        """Hold/release a bottom-screen touch (pixels, x 0..319 y 0..239). Requires the Touch mod."""
        xy = ((int(x) & 0xFFFF) << 16) | (int(y) & 0xFFFF)
        self._req(TOUCH, 1 if active else 0, xy)

    def tap_touch(self, x, y, hold_s=0.15):
        import time
        self.set_touch(True, x, y)
        time.sleep(hold_s)
        self.set_touch(False)
        time.sleep(0.05)

    def tap(self, buttons, hold_s=0.12, circle=None):
        """Press buttons (and/or circle), hold briefly, release. A single discrete input."""
        import time
        self.set_input(buttons, circle)
        time.sleep(hold_s)
        self.set_input(0, None)
        time.sleep(0.05)

    def screenshot(self, ppm_path, res_scale=0, timeout=8.0):
        """Capture the next OoT3D frame to ppm_path (Azahar writes it host-side). Returns True on ok.
        Requires the SoH3D screenshot RPC mod (PacketType::Screenshot=5)."""
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            out = self._req(SCREENSHOT, res_scale, len(ppm_path.encode()),
                            ppm_path.encode())
        finally:
            self.sock.settimeout(old)
        return len(out) >= 4 and struct.unpack("<I", out[:4])[0] == 1


def _hexdump(addr, data):
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        hexs = " ".join(f"{b:02x}" for b in row)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        print(f"{addr + i:08x}  {hexs:<47}  {asc}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    r = Rpc()
    cmd = sys.argv[1]
    if cmd == "procs":
        for pid, tid, name in r.processes():
            print(f"pid={pid:<4} title_id=0x{tid:016x} name={name}")
    elif cmd == "read":
        addr = int(sys.argv[2], 16)
        size = int(sys.argv[3])
        _hexdump(addr, r.read(addr, size))
    elif cmd == "read32":
        addr = int(sys.argv[2], 16)
        v = r.read32(addr)
        print(f"{addr:08x}: {v:08x} ({v})" if v is not None else "(read failed)")
    elif cmd == "shot":
        out = sys.argv[2] if len(sys.argv) > 2 else "scratch/screenshots/azahar_shot.ppm"
        ok = r.screenshot(out)
        print(f"screenshot {'OK' if ok else 'FAILED'} -> {out}")
    else:
        print(f"unknown cmd {cmd}")


if __name__ == "__main__":
    main()
