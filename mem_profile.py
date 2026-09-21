import psutil
import subprocess
import time

p = subprocess.Popen(['.\\build\\bin\\scale_benchmark.exe'])
peak = 0
try:
    proc = psutil.Process(p.pid)
    while p.poll() is None:
        mem = proc.memory_info().rss
        if mem > peak:
            peak = mem
        time.sleep(0.01)
except psutil.NoSuchProcess:
    pass

print(f'Peak Memory: {peak / (1024*1024):.2f} MB')
