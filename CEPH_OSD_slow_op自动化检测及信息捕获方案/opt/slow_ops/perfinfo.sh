#!/bin/bash

sample_wait=30
d=$(date "+%Y-%m-%d-%H-%M")

mkdir -p system/$d

capture_system_info()
{
	lsblk > system/$d/disk_info
	lscpu > system/$d/cpuinfo
	cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > system/$d/cpufreq
	cat /proc/buddyinfo > system/$d/buddyinfo
	free -h > system/$d/free
	cat /proc/meminfo > system/$d/meminfo

        iostat -x -p 2 > system/$d/iostat &
        top -b -n 10 -H -p $(pgrep -d',' -f 'ceph-osd') -c > system/$d/osd_top &


        top -b -c> system/$d/top &
        sar -n DEV 2 > system/$d/eth_device_info &
        cat /sys/block/sd*/queue/max_sectors_kb > system/$d/hdd_disk_queue_info
        cat /sys/block/sd*/queue/max_hw_sectors_kb >> system/$d/hdd_disk_queue_info
        cat /sys/block/sd*/queue/nr_requests >> system/$d/hdd_disk_queue_info

        cat /sys/block/nvme*/queue/max_sectors_kb > system/$d/nvme_disk_queue_info
        cat /sys/block/nvme*/queue/max_hw_sectors_kb >> system/$d/nvme_disk_queue_info
        cat /sys/block/nvme*/queue/nr_requests >> system/$d/nvme_disk_queue_info

        numastat > system/$d/numa_info
        numactl -H  >> system/$d/numa_info

        ceph osd tree > system/$d/osd_tree_all
        ceph osd df > system/$d/osd_df_all
        ceph df > system/$d/ceph_df
        ceph pg dump > system/$d/pg_info
        cp /etc/ceph/ceph.conf system/$d/ceph.conf
}

finish_env_resource_info()
{
	ps aux | grep iostat | grep -v grep | awk '{print $2}' | xargs   kill -9
	ps aux | grep top | grep -v grep | awk '{print $2}' | xargs   kill -9
	ps aux | grep "sar -n" | grep -v grep | awk '{print $2}' | xargs    kill -9
}


capture_system_info
sleep $sample_wait
finish_env_resource_info

