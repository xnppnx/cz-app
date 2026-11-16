import glob
import os
import select
import struct
import time

primary = "/dev/input/by-path/platform-3f804000.i2c-event"
paths = []
if os.path.exists(primary):
    paths.append(primary)
paths += sorted(glob.glob("/dev/input/event*"))

seen = set()
unique = []
for p in paths:
    real = os.path.realpath(p)
    if real in seen:
        continue
    seen.add(real)
    unique.append(p)

fds = []
for p in unique:
    try:
        fd = os.open(p, os.O_RDONLY | os.O_NONBLOCK)
        fds.append((p, fd))
    except OSError:
        pass

print("WATCHING", len(fds), "devices", flush=True)
for p, _ in fds:
    print("  ", p, "->", os.path.realpath(p), flush=True)
print("PRESS KEYS on device now: N A S D Enter Del Space 1 2  (40s)", flush=True)

end = time.time() + 40
while time.time() < end:
    readable, _, _ = select.select([f for _, f in fds], [], [], 0.2)
    for fd in readable:
        try:
            data = os.read(fd, 24 * 64)
        except OSError:
            continue
        for i in range(0, len(data) - 23, 24):
            sec, usec, typ, code, val = struct.unpack("<qqHHi", data[i:i + 24])
            if typ == 1 and val in (0, 1):
                name = [p for p, f in fds if f == fd][0]
                print(os.path.basename(name), "code=", code, "val=", val, flush=True)

for _, fd in fds:
    os.close(fd)
print("DONE", flush=True)
