# 问题描述：  
ceph分布式存储系统中，同一个osd可以部署在多个存储池上吗？为什么    
这是一个群里有一位朋友问到的问题，他的原问题是ceph两个池，不复用osd，一个池满了不可写会导致另一个不满的池不能写吗？    
答案是不会  
  
# 问题分析:    
那么同一个osd可以部署到两个池上吗    
答案是可以，之前一直没有这么干过，所以潜意识中认为不能，早先的理解是osd--> 唯一创建对应的存储池pool，pool上pg唯一，即pg和osd是1对多的绝对关系，而不允许是同一个pg映射到多个pool的osd上。  
这个理解是混乱的，说明对ceph数据分布的原理还不清晰   
  
## 1. osd和创建的存储池没有直接的关系，我们之前的基于ceph做自研全闪产品，osd是属于硬盘池的东西，在这之上创建存储池，存储池的创建要根据crushmap的拓扑结构来设计，比如同一个osd是否会跨不同的存储池  
正常来说，比如要建立一个块存储副本池，单节点有两块osd，其拓扑结构如下，这是不包含机架的角色：    
![](https://github.com/joehou89/ceph_15.2.17_sourcecode_analyse/blob/main/crushmap%E6%8B%93%E6%89%91%E7%BB%93%E6%9E%84.png)    
这里的都是物理资源，crushmap里针对每一个pool，还有一个rule规则，其主要内容如下：  
```sh
id   #序号
type #表示副本类型还是ec类型
step take root  #root类型
...
```    
如果创建pool的rule规则，buckets所属的root是相同的，则同一个osd会部署上不同的存储池上，如果root是不同的，则每一个pool的osd是不同的，即物理隔离；    
我理解ceph之所以允许这样做的目的还是存储系统的通用化，即复用存储硬件资源，提供更多的存储服务，随之带来的问题是运维成本的增加，以及存储服务的性能是否满足业务需求等问题；  
正常来说，还是要保证每一个osd属于唯一的pool，确保物理隔离，这也是常规用法。    
  
## 2.如果同一个osd部署到两个存储池pool上，那么可能存在如下pg到osd的关系：  
```sh  
pool1 80.0e    pg  {11,2,5}
pool2 81.0e    pg  {2,5,11}
```  
可以看到同一个osd.11上包含两个pool池的pg，而不是唯一的，QA 哪里来保存不同pool的pgmap？这张pgmap在osd实例上是否也会看到？    
    
## 3.ceph创池流程  
流程函数如下:  
```sh
客户端命令执行ceph osd pool create pool pgnum pgpnum
|void Monitor::handle_command(MonOpRequestRef op)
  |osdmon()->dispatch(op); #osd相关
    |PaxosService::dispatch(MonOpRequestRef op)
      |bool OSDMonitor::preprocess_query(MonOpRequestRef op)
      |bool OSDMonitor::prepare_update(MonOpRequestRef op)
        |bool OSDMonitor::prepare_command(MonOpRequestRef op)
          |bool OSDMonitor::prepare_command_impl(MonOpRequestRef op, const cmdmap_t& cmdmap)
            |create pool 逻辑
              |int OSDMonitor::prepare_new_pool(MonOpRequestRef op)
                |int OSDMonitor::prepare_new_pool(string& name,
				        |int crush_rule,
				        |const string &crush_rule_name,
                |                 unsigned pg_num, unsigned pgp_num,
				        |unsigned pg_num_min,
                |                 const uint64_t repl_size,
				        |const uint64_t target_size_bytes,
				        |const float target_size_ratio,
				        |const string &erasure_code_profile,
                |                 const unsigned pool_type,
                |                 const uint64_t expected_num_objects,
                |                 FastReadType fast_read,
				        |const string& pg_autoscale_mode,
				        |ostream *ss)
                  |prepare_pool_crush_rule
                  |_get_pending_crush
                  |prepare_pool_size
                  |check_pg_num
                  |check_crush_rule
                  |prepare_pool_stripe_width

```    
整个流程做了两件事:  
(1)创池pool动作，其中包含创pool所需的参数，这些参数是通过入参传进来，针对副本池或ec池进行区分;  
(2)通过paxos::propose_pending 接口提交事务到paxos协议，通过paxos协议将更新后的osdmap同步给其他mon节点;  
    
具体的osd实例如何感知到osdmap是否发生了变化？  
mon主动推送:此时mon集群已经同步完了最新的osdmap，此时mon集群会周期发送消息给osd;    
osd周期拉取  
当osd获取到最新的osdmap后，会触发创建pg流程  






















