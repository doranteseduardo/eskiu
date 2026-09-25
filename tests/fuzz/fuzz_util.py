"""
fuzz_util.py: bounded process execution shared by the fuzzers.

Every compiler and program invocation runs under a wall-clock timeout and a
resident-memory cap, so a runaway input cannot take the machine down. macOS
ignores RLIMIT_AS (`ulimit -v`), and a plain rlimit would also break ASan
binaries, which reserve terabytes of virtual shadow memory, so the cap is a
watchdog: it samples the resident set size of the child and its children
(eskiuc runs clang to link) and kills the whole process group past the limit.
A timeout or a memory blowup is reported to the caller, which treats it as a
finding (a hang or blowup on a small input is a bug).
"""

import os, signal, subprocess, time

MEM_LIMIT_MB = int(os.environ.get("ESKIU_FUZZ_MEM_MB", "4000"))
DEFAULT_JOBS = min(4, os.cpu_count() or 2)
POLL = 0.2

def _tree_rss_mb(pid):
    pids = [str(pid)]
    try:
        kids = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True,
                              timeout=5).stdout.split()
        pids += kids
        out = subprocess.run(["ps", "-o", "rss=", "-p", ",".join(pids)],
                             capture_output=True, text=True, timeout=5).stdout.split()
        return sum(int(x) for x in out) // 1024
    except (subprocess.SubprocessError, ValueError, OSError):
        return 0

def _kill_group(p):
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except OSError:
        p.kill()

def run_limited(cmd, timeout, env=None, input=None, mem_mb=None):
    """Run cmd; return (status, returncode, stdout, stderr).

    status is "OK", "TIMEOUT" or "OOM" (returncode is None for the last two)."""
    mem_mb = mem_mb or MEM_LIMIT_MB
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         stdin=subprocess.PIPE if input is not None else subprocess.DEVNULL,
                         env=env, start_new_session=True)
    status = "OK"
    start = time.monotonic()
    first = True
    while True:
        try:
            # most runs finish within the first slice, so the RSS probe (two forks)
            # only runs for the few long ones
            out, err = p.communicate(input=input if first else None, timeout=POLL)
            break
        except subprocess.TimeoutExpired:
            first = False
            if time.monotonic() - start > timeout:
                status = "TIMEOUT"
            elif _tree_rss_mb(p.pid) > mem_mb:
                status = "OOM"
            if status != "OK":
                _kill_group(p)
                out, err = p.communicate()
                break
    rc = p.returncode if status == "OK" else None
    return (status, rc, out.decode("utf-8", "replace"), err.decode("utf-8", "replace"))


def default_eskiuc_esk():
    # The self-hosted compiler to test against: $ESKIUC_ESK, else the eskiuc-esk next to
    # $ESKIUC (so pointing ESKIUC at another build dir never pairs it with a stale
    # self-host binary), else the repo's build/eskiuc-esk.
    if os.environ.get("ESKIUC_ESK"):
        return os.environ["ESKIUC_ESK"]
    if os.environ.get("ESKIUC"):
        return os.path.join(os.path.dirname(os.environ["ESKIUC"]), "eskiuc-esk")
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return os.path.join(root, "build", "eskiuc-esk")
