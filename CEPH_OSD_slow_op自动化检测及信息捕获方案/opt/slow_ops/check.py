#!/usr/bin/env python3
from datetime import datetime
import glob
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
import re
import sys
import time

LOG_DIR = "/var/log/ceph"
PATTERN = "ceph-osd.*.log"
time_prefix = datetime.now().strftime("%Y-%m-%d %H:%M")
current_time = int(time.time())

def process_log_file(log_file):
    filename = os.path.basename(log_file)
    pattern = re.compile(r"ceph-osd\.(\d+)\.log$")
    match = pattern.match(filename)
    if not match:
        return None

    osd_id = match.group(1)

    try:
        with open(log_file, 'r') as f:
            for line in f:
                if line.startswith(time_prefix) and ('slow requests' in line or 'slow ops' in line):
                    return osd_id
    except Exception as e:
        print(f"{time_prefix} Error reading {log_file}: {e}")
    return None

def run_shell_command(cmd):
    try:
        subprocess.run(cmd, shell=True, check=True)
    except Exception as e:
        print(f"{time_prefix} {e}")

def scan_logs_this_minute():
    log_files = glob.glob(os.path.join(LOG_DIR, PATTERN))
    if not log_files:
        return

    osd_ids=[]

    with ThreadPoolExecutor() as executor:
        futures = [
            executor.submit(process_log_file, log_file)
            for log_file in log_files
        ]
        for future in as_completed(futures):
            result = future.result()
            if result is not None:
                osd_ids.append(result)

    if len(osd_ids) > 0:
        run_shell_command("bash perfinfo.sh &")
        with open('/opt/slow_ops/stamp', 'w') as f:
            f.write(str(current_time+3600))
    for osd_id in osd_ids:
    	run_shell_command(f"bash osdinfo.sh {osd_id}")


if __name__ == "__main__":
    try:
        with open('/opt/slow_ops/stamp', 'r') as f:
            timestamp = int(f.read().strip())
    except Exception as e:
        print(f"{time_prefix} Failed to read timestamp: {e}")
        sys.exit(0)
    if current_time < timestamp:
        print(f"{time_prefix} sleep")
        sys.exit(0)
    scan_logs_this_minute()

