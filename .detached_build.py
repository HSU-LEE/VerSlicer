import os
import sys

pid = os.fork()
if pid > 0:
    print("detached build child pid:", pid)
    sys.exit(0)

os.setsid()
log = open("/Users/hsu/VerSlicer/verslicer_build.log", "w", buffering=1)
os.dup2(log.fileno(), 1)
os.dup2(log.fileno(), 2)
os.chdir("/Users/hsu/VerSlicer/build/arm64")
rc = os.system("ninja -f build-Release.ninja verslicer")
exit_code = (rc >> 8) if os.WIFEXITED(rc) else rc
print("NINJA_EXIT=%d" % exit_code)
sys.exit(0)
