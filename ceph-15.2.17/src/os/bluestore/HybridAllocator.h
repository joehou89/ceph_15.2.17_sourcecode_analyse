// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab


/*
TODO:
真正部署起来一个ceph15.2.17 版本的存储集群
通过ceph daemon osd.0 config show | grep alloc
查看对应的磁盘分配器类型: bluestore_alloctor: hybrid
url链接: 
https://smdsbz.github.io/2021/11/09/ceph-bluestore.html
https://zhuanlan.zhihu.com/p/643938193


*/


#pragma once

#include <mutex>

#include "AvlAllocator.h"
#include "BitmapAllocator.h"


/*
hybridAllocator分配器是包了一个bitmap分配器，但是hybrid本身继承于avlallocator
所以从类对象的设计来看，是一个avl类分配器里塞了一个bitmap分配器，是avl分配器的变种 
本身hybridallocator提供的API不多，就是如下这几个接口
*/

class HybridAllocator : public AvlAllocator {
  BitmapAllocator* bmap_alloc = nullptr;  //bit磁盘空间管理实例
public:
  //TODO: 构造函数，其中还要调用基类构造函数
  HybridAllocator(CephContext* cct, int64_t device_size, int64_t _block_size,
                  uint64_t max_mem,
	          const std::string& name) :
      AvlAllocator(cct, device_size, _block_size, max_mem, name) {
  }
  //提供分配接口用于磁盘空间的分配，override重写了基类的纯虚函数
  int64_t allocate(
    uint64_t want,
    uint64_t unit,
    uint64_t max_alloc_size,
    int64_t  hint,
    PExtentVector *extents) override;
  //释放磁盘空间
  void release(const interval_set<uint64_t>& release_set) override;
  uint64_t get_free() override;
  double get_fragmentation() override;

  void dump() override;
  void dump(std::function<void(uint64_t offset, uint64_t length)> notify) override;
  void init_rm_free(uint64_t offset, uint64_t length) override;
  void shutdown() override;

protected:
  // intended primarily for UT
  BitmapAllocator* get_bmap() {
    return bmap_alloc;
  }
  const BitmapAllocator* get_bmap() const {
    return bmap_alloc;
  }
private:

  void _spillover_range(uint64_t start, uint64_t end) override;

  // called when extent to be released/marked free
  void _add_to_tree(uint64_t start, uint64_t size) override;
};
