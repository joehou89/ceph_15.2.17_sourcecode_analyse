现网集群出现slow op故障快速恢复和捕获关键信息方案  
1.通过监控(后台脚本)首先捕获第一次出现slow op的OSD 以及同pg的其他两个OSD  
  
2.a.捕获异常OSD以及对应pg的从OSD的perf dump、 dump_historic_ops、dump_ops_in_flight、磁盘信息  
```sh
   d=$(date "+%Y-%m-%d_%H-%M-%S")  
   cd /root/nvme9_trace  
   blktrace -d /dev/nvme9n1 -w 15
   blkparse -i nvme9n1 -d nvme9n1.blktrace_${d}.bin > /dev/null
   btt -i nvme9n1.blktrace_${d}.bin > nvme9n1_${d}.txt
   ceph daemon osd.109 perf dump > 109_perf_dump_${d}.txt
   ceph daemon osd.109 dump_ops_in_flight > 109_dump_ops_in_flight_${d}.txt
   ceph daemon osd.109 dump_historic_ops > 109_dump_historic_ops_${d}.txt
```  
这一段是针对某一个具体的osd，可以封装成一个脚本，通过获取到第一次出现slow op的OSD id 作为该脚本入参  
  
3.捕获异常OSD以及对应pg的从OSD所在节点的集群信息、节点系统资源信息(编写脚本已提供)  ---- > 执行该脚本即可  
4.重启出现slow op的osd即可  
  
注意:   
3 4两个脚本针对出现slow op的三个OSD 都要捕获对应的信息，以防止漏掉，举例: pg.1 {OSD.1 OSD.2 OSD.3 } 出现slow op的是OSD.1 ，但是问题出在从OSD.2上  
3 4两步的脚本可以合在一起，要通过免密ssh进行节点间通信，举例，OSD.1 出现slow op， 通知其他两个节点osd捕获对应信息  
    
补充：无法通过OSD找到对应的主PG，这是不能做到的，至少通过现有的ceph命令，因为相同主osd对应找到的主pg可能是多个，除非是依次遍历， 这就没必要了。    
比较可行的方案是通过如上命令，找到回复慢的从OSD，再依次获取对应节点信息即可。      
    
  
