#!/bin/bash

osd_id=$1
d=$(date "+%Y-%m-%d-%H-%M")

mkdir -p osd-${osd_id}/$d

capture_osd_info()
{
        ceph daemon osd."${osd_id}" config show > osd-${osd_id}/$d/osd_config_info
        ls /var/lib/ceph/osd/ceph-"${osd_id}" -l > osd-${osd_id}/$d/osd_disk_info

        ceph_version=`ceph -v | grep "ceph" | awk -F ' ' '{print $3}'`
        if [[ "$ceph_version" == 15.* || "$ceph_version" == 14.* ]]; then
          ceph daemon osd."${osd_id}" dump_mempools > osd-${osd_id}/$d/osd_dump_mempools
          ceph daemon osd."${osd_id}" perf dump bluefs > osd-${osd_id}/$d/osd_dump_bluefs
        fi
        ceph daemon osd."${osd_id}" perf dump > osd-${osd_id}/$d/osd_perf_dump
        ceph daemon osd."${osd_id}" dump_ops_in_flight > osd-${osd_id}/$d/osd_dump_ops_in_flight
        ceph daemon osd."${osd_id}" dump_historic_ops > osd-${osd_id}/$d/osd_dump_historic_ops
        ceph daemon osd."${osd_id}" dump_blocked_ops > osd-${osd_id}/$d/osd_dump_blocked_ops
        ceph daemon osd."${osd_id}" dump_op_pq_state > osd-${osd_id}/$d/osd_dump_op_pq_state
}

capture_osd_info
