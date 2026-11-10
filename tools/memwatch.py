#!/usr/bin/env python3
# The second net under tools/capped: logs every compiler, test or benchmark of this tree over 4 GB, and SIGKILLs the
# largest of them when the box has less than 6 GB available. Run it in the background for a session, outside the
# sandbox (it reads /proc):  tools/memwatch.py <log>
import os, signal, time, sys
LOG = sys.argv[1]
NAMES = ("clang", "ld.lld", "ld", "ninja", "ctest", "cmake")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"  # this tree, its worktrees and build dirs
def meminfo():
    for line in open("/proc/meminfo"):
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) * 1024
def ours():
    out = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit(): continue
        try:
            cmd = open(f"/proc/{pid}/cmdline", "rb").read().split(b"\0")
            cmd = [c.decode(errors="replace") for c in cmd if c]
            if not cmd: continue
            exe = os.path.basename(cmd[0])
            if not (exe.startswith(NAMES) or cmd[0].startswith(ROOT)): continue
            rss = 0
            for line in open(f"/proc/{pid}/status"):
                if line.startswith("VmRSS:"): rss = int(line.split()[1]) * 1024
            target = cmd[cmd.index("-o") + 1] if "-o" in cmd else " ".join(cmd)[:160]
            out.append((rss, int(pid), exe, target))
        except (OSError, ValueError, IndexError):
            pass
    return out
seen = {}
def log(msg):
    with open(LOG, "a") as f: f.write(time.strftime("%H:%M:%S ") + msg + "\n")
log("memwatch started")
while True:
    avail = meminfo()
    procs = ours()
    for rss, pid, exe, target in procs:
        step = rss >> 31  # 2 GB steps
        if rss > 4 << 30 and seen.get(pid, -1) < step:
            seen[pid] = step
            log(f"{rss/2**30:5.1f} GB pid {pid} {exe} {target}  (avail {avail/2**30:.1f} GB)")
    if avail < 6 << 30 and procs:
        rss, pid, exe, target = max(procs)
        try:
            os.kill(pid, signal.SIGKILL)
            log(f"KILLED pid {pid} {exe} {target} at {rss/2**30:.1f} GB, avail {avail/2**30:.1f} GB")
        except OSError:
            pass
        time.sleep(2)
    time.sleep(1)
