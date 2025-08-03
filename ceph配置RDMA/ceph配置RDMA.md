配置RDMA，部署好网卡的RDMA驱动，加上如下参数，重启ceph.target服务，集群网络就是走RDMA  
```sh
ms_async_rdma_device_name = mlx5_bond_0     #ibstat 查看
ms_async_rdma_polling_us = 0
ms_bind_ipv4 = True
ms_bind_ipv6 = False
ms_cluster_type = async+rdma  #rdma
ms_public_type = async+posix  #posix
```  
  
这个mlx5_bond_0是配了bond，这个支持RDMA的bond, 做bond的口在一张卡上即可，其他没有要求    
  
