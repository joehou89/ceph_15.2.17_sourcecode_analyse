// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab ft=cpp

#include <limits>
#include <sstream>

#include "rgw_rados.h"
#include "rgw_zone.h"
#include "rgw_bucket.h"
#include "rgw_reshard.h"
#include "rgw_sal.h"
#include "cls/rgw/cls_rgw_client.h"
#include "cls/lock/cls_lock_client.h"
#include "common/errno.h"
#include "common/ceph_json.h"

#include "common/dout.h"

#include "services/svc_zone.h"
#include "services/svc_sys_obj.h"
#include "services/svc_tier_rados.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rgw

const string reshard_oid_prefix = "reshard.";
const string reshard_lock_name = "reshard_process";
const string bucket_instance_lock_name = "bucket_instance_lock";

/* All primes up to 2000 used to attempt to make dynamic sharding use
 * a prime numbers of shards. Note: this list also includes 1 for when
 * 1 shard is the most appropriate, even though 1 is not prime.
 */
const std::initializer_list<uint16_t> RGWBucketReshard::reshard_primes = {
  1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61,
  67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137,
  139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211,
  223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283,
  293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379,
  383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461,
  463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563,
  569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643,
  647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739,
  743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829,
  839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937,
  941, 947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021,
  1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069, 1087, 1091, 1093,
  1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163, 1171, 1181,
  1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259,
  1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321,
  1327, 1361, 1367, 1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433,
  1439, 1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487, 1489, 1493,
  1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579,
  1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657,
  1663, 1667, 1669, 1693, 1697, 1699, 1709, 1721, 1723, 1733, 1741,
  1747, 1753, 1759, 1777, 1783, 1787, 1789, 1801, 1811, 1823, 1831,
  1847, 1861, 1867, 1871, 1873, 1877, 1879, 1889, 1901, 1907, 1913,
  1931, 1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999
};

class BucketReshardShard {
  rgw::sal::RGWRadosStore *store;
  const RGWBucketInfo& bucket_info;
  int num_shard;
  RGWRados::BucketShard bs;
  vector<rgw_cls_bi_entry> entries;
  map<RGWObjCategory, rgw_bucket_category_stats> stats;
  deque<librados::AioCompletion *>& aio_completions;
  uint64_t max_aio_completions;
  uint64_t reshard_shard_batch_size;

  int wait_next_completion() {
    librados::AioCompletion *c = aio_completions.front();
    aio_completions.pop_front();

    c->wait_for_complete();

    int ret = c->get_return_value();
    c->release();

    if (ret < 0) {
      derr << "ERROR: reshard rados operation failed: " << cpp_strerror(-ret) << dendl;
      return ret;
    }

    return 0;
  }

  int get_completion(librados::AioCompletion **c) {
    if (aio_completions.size() >= max_aio_completions) {
      int ret = wait_next_completion();
      if (ret < 0) {
        return ret;
      }
    }

    *c = librados::Rados::aio_create_completion(nullptr, nullptr);
    aio_completions.push_back(*c);

    return 0;
  }

public:
  BucketReshardShard(rgw::sal::RGWRadosStore *_store, const RGWBucketInfo& _bucket_info,
                     int _num_shard,
                     deque<librados::AioCompletion *>& _completions) :
    store(_store), bucket_info(_bucket_info), bs(store->getRados()),
    aio_completions(_completions)
  {
    num_shard = (bucket_info.num_shards > 0 ? _num_shard : -1);
    bs.init(bucket_info.bucket, num_shard, nullptr /* no RGWBucketInfo */);

    max_aio_completions =
      store->ctx()->_conf.get_val<uint64_t>("rgw_reshard_max_aio");
    reshard_shard_batch_size =
      store->ctx()->_conf.get_val<uint64_t>("rgw_reshard_batch_size");
  }

  int get_num_shard() {
    return num_shard;
  }

  int add_entry(rgw_cls_bi_entry& entry, bool account, RGWObjCategory category,
                const rgw_bucket_category_stats& entry_stats) {
    entries.push_back(entry);
    if (account) {
      rgw_bucket_category_stats& target = stats[category];
      target.num_entries += entry_stats.num_entries;
      target.total_size += entry_stats.total_size;
      target.total_size_rounded += entry_stats.total_size_rounded;
      target.actual_size += entry_stats.actual_size;
    }
    if (entries.size() >= reshard_shard_batch_size) {
      int ret = flush();
      if (ret < 0) {
        return ret;
      }
    }

    return 0;
  }

  int flush() {
    if (entries.size() == 0) {
      return 0;
    }

    librados::ObjectWriteOperation op;
    for (auto& entry : entries) {
      store->getRados()->bi_put(op, bs, entry);
    }
    cls_rgw_bucket_update_stats(op, false, stats);

    librados::AioCompletion *c;
    int ret = get_completion(&c);
    if (ret < 0) {
      return ret;
    }
    ret = bs.bucket_obj.aio_operate(c, &op);
    if (ret < 0) {
      derr << "ERROR: failed to store entries in target bucket shard (bs=" << bs.bucket << "/" << bs.shard_id << ") error=" << cpp_strerror(-ret) << dendl;
      return ret;
    }
    entries.clear();
    stats.clear();
    return 0;
  }

  int wait_all_aio() {
    int ret = 0;
    while (!aio_completions.empty()) {
      int r = wait_next_completion();
      if (r < 0) {
        ret = r;
      }
    }
    return ret;
  }
}; // class BucketReshardShard


class BucketReshardManager {
  rgw::sal::RGWRadosStore *store;
  const RGWBucketInfo& target_bucket_info;
  deque<librados::AioCompletion *> completions;
  int num_target_shards;
  vector<BucketReshardShard *> target_shards;

public:
  BucketReshardManager(rgw::sal::RGWRadosStore *_store,
		       const RGWBucketInfo& _target_bucket_info,
		       int _num_target_shards) :
    store(_store), target_bucket_info(_target_bucket_info),
    num_target_shards(_num_target_shards)
  {
    target_shards.resize(num_target_shards);
    for (int i = 0; i < num_target_shards; ++i) {
      target_shards[i] = new BucketReshardShard(store, target_bucket_info, i, completions);
    }
  }

  ~BucketReshardManager() {
    for (auto& shard : target_shards) {
      int ret = shard->wait_all_aio();
      if (ret < 0) {
        ldout(store->ctx(), 20) << __func__ <<
	  ": shard->wait_all_aio() returned ret=" << ret << dendl;
      }
    }
  }

  int add_entry(int shard_index,
                rgw_cls_bi_entry& entry, bool account, RGWObjCategory category,
                const rgw_bucket_category_stats& entry_stats) {
    int ret = target_shards[shard_index]->add_entry(entry, account, category,
						    entry_stats);
    if (ret < 0) {
      derr << "ERROR: target_shards.add_entry(" << entry.idx <<
	") returned error: " << cpp_strerror(-ret) << dendl;
      return ret;
    }

    return 0;
  }

  int finish() {
    int ret = 0;
    for (auto& shard : target_shards) {
      int r = shard->flush();
      if (r < 0) {
        derr << "ERROR: target_shards[" << shard->get_num_shard() << "].flush() returned error: " << cpp_strerror(-r) << dendl;
        ret = r;
      }
    }
    for (auto& shard : target_shards) {
      int r = shard->wait_all_aio();
      if (r < 0) {
        derr << "ERROR: target_shards[" << shard->get_num_shard() << "].wait_all_aio() returned error: " << cpp_strerror(-r) << dendl;
        ret = r;
      }
      delete shard;
    }
    target_shards.clear();
    return ret;
  }
}; // class BucketReshardManager

RGWBucketReshard::RGWBucketReshard(rgw::sal::RGWRadosStore *_store,
				   const RGWBucketInfo& _bucket_info,
				   const map<string, bufferlist>& _bucket_attrs,
				   RGWBucketReshardLock* _outer_reshard_lock) :
  store(_store), bucket_info(_bucket_info), bucket_attrs(_bucket_attrs),
  reshard_lock(store, bucket_info, true),
  outer_reshard_lock(_outer_reshard_lock)
{ }

int RGWBucketReshard::set_resharding_status(rgw::sal::RGWRadosStore* store,
					    const RGWBucketInfo& bucket_info,
					    const string& new_instance_id,
					    int32_t num_shards,
					    cls_rgw_reshard_status status)
{
  if (new_instance_id.empty()) {
    ldout(store->ctx(), 0) << __func__ << " missing new bucket instance id" << dendl;
    return -EINVAL;
  }

  cls_rgw_bucket_instance_entry instance_entry;
  instance_entry.set_status(new_instance_id, num_shards, status);

  int ret = store->getRados()->bucket_set_reshard(bucket_info, instance_entry);
  if (ret < 0) {
    ldout(store->ctx(), 0) << "RGWReshard::" << __func__ << " ERROR: error setting bucket resharding flag on bucket index: "
		  << cpp_strerror(-ret) << dendl;
    return ret;
  }
  return 0;
}

// reshard lock assumes lock is held
int RGWBucketReshard::clear_resharding(rgw::sal::RGWRadosStore* store,
				       const RGWBucketInfo& bucket_info)
{
  int ret = clear_index_shard_reshard_status(store, bucket_info);
  if (ret < 0) {
    ldout(store->ctx(), 0) << "RGWBucketReshard::" << __func__ <<
      " ERROR: error clearing reshard status from index shard " <<
      cpp_strerror(-ret) << dendl;
    return ret;
  }

  cls_rgw_bucket_instance_entry instance_entry;
  ret = store->getRados()->bucket_set_reshard(bucket_info, instance_entry);
  if (ret < 0) {
    ldout(store->ctx(), 0) << "RGWReshard::" << __func__ <<
      " ERROR: error setting bucket resharding flag on bucket index: " <<
      cpp_strerror(-ret) << dendl;
    return ret;
  }

  return 0;
}

int RGWBucketReshard::clear_index_shard_reshard_status(rgw::sal::RGWRadosStore* store,
						       const RGWBucketInfo& bucket_info)
{
  uint32_t num_shards = bucket_info.num_shards;

  if (num_shards < std::numeric_limits<uint32_t>::max()) {
    int ret = set_resharding_status(store, bucket_info,
				    bucket_info.bucket.bucket_id,
				    (num_shards < 1 ? 1 : num_shards),
				    cls_rgw_reshard_status::NOT_RESHARDING);
    if (ret < 0) {
      ldout(store->ctx(), 0) << "RGWBucketReshard::" << __func__ <<
	" ERROR: error clearing reshard status from index shard " <<
	cpp_strerror(-ret) << dendl;
      return ret;
    }
  }

  return 0;
}

static int create_new_bucket_instance(rgw::sal::RGWRadosStore *store,
				      int new_num_shards,
				      const RGWBucketInfo& bucket_info,
				      map<string, bufferlist>& attrs,
				      RGWBucketInfo& new_bucket_info)
{
  new_bucket_info = bucket_info;

  store->getRados()->create_bucket_id(&new_bucket_info.bucket.bucket_id);

  new_bucket_info.num_shards = new_num_shards;
  new_bucket_info.objv_tracker.clear();

  new_bucket_info.new_bucket_instance_id.clear();
  new_bucket_info.reshard_status = cls_rgw_reshard_status::NOT_RESHARDING;

  int ret = store->getRados()->put_bucket_instance_info(new_bucket_info, true, real_time(), &attrs);
  if (ret < 0) {
    cerr << "ERROR: failed to store new bucket instance info: " << cpp_strerror(-ret) << std::endl;
    return ret;
  }

  ret = store->svc()->bi->init_index(new_bucket_info);
  if (ret < 0) {
    cerr << "ERROR: failed to init new bucket indexes: " << cpp_strerror(-ret) << std::endl;
    return ret;
  }

  return 0;
}

int RGWBucketReshard::create_new_bucket_instance(int new_num_shards,
                                                 RGWBucketInfo& new_bucket_info)
{
  return ::create_new_bucket_instance(store, new_num_shards,
				      bucket_info, bucket_attrs, new_bucket_info);
}

int RGWBucketReshard::cancel()
{
  int ret = reshard_lock.lock();
  if (ret < 0) {
    return ret;
  }

  ret = clear_resharding();

  reshard_lock.unlock();
  return ret;
}

class BucketInfoReshardUpdate
{
  rgw::sal::RGWRadosStore *store;
  RGWBucketInfo& bucket_info;
  std::map<string, bufferlist> bucket_attrs;

  bool in_progress{false};

  int set_status(cls_rgw_reshard_status s) {
    bucket_info.reshard_status = s;
    int ret = store->getRados()->put_bucket_instance_info(bucket_info, false, real_time(), &bucket_attrs);
    if (ret < 0) {
      ldout(store->ctx(), 0) << "ERROR: failed to write bucket info, ret=" << ret << dendl;
      return ret;
    }
    return 0;
  }

public:
  BucketInfoReshardUpdate(rgw::sal::RGWRadosStore *_store,
			  RGWBucketInfo& _bucket_info,
                          map<string, bufferlist>& _bucket_attrs,
			  const string& new_bucket_id) :
    store(_store),
    bucket_info(_bucket_info),
    bucket_attrs(_bucket_attrs)
  {
    bucket_info.new_bucket_instance_id = new_bucket_id;
  }

  ~BucketInfoReshardUpdate() {
    if (in_progress) {
      // resharding must not have ended correctly, clean up
      int ret =
	RGWBucketReshard::clear_index_shard_reshard_status(store, bucket_info);
      if (ret < 0) {
	lderr(store->ctx()) << "Error: " << __func__ <<
	  " clear_index_shard_status returned " << ret << dendl;
      }
      bucket_info.new_bucket_instance_id.clear();

      // clears new_bucket_instance as well
      set_status(cls_rgw_reshard_status::NOT_RESHARDING);
    }
  }

  int start() {
    int ret = set_status(cls_rgw_reshard_status::IN_PROGRESS);
    if (ret < 0) {
      return ret;
    }
    in_progress = true;
    return 0;
  }

  int complete() {
    int ret = set_status(cls_rgw_reshard_status::DONE);
    if (ret < 0) {
      return ret;
    }
    in_progress = false;
    return 0;
  }
};


RGWBucketReshardLock::RGWBucketReshardLock(rgw::sal::RGWRadosStore* _store,
					   const std::string& reshard_lock_oid,
					   bool _ephemeral) :
  store(_store),
  lock_oid(reshard_lock_oid),
  ephemeral(_ephemeral),
  internal_lock(reshard_lock_name)
{
  const int lock_dur_secs = store->ctx()->_conf.get_val<uint64_t>(
    "rgw_reshard_bucket_lock_duration");
  duration = std::chrono::seconds(lock_dur_secs);

#define COOKIE_LEN 16
  char cookie_buf[COOKIE_LEN + 1];
  gen_rand_alphanumeric(store->ctx(), cookie_buf, sizeof(cookie_buf) - 1);
  cookie_buf[COOKIE_LEN] = '\0';

  internal_lock.set_cookie(cookie_buf);
  internal_lock.set_duration(duration);
}

int RGWBucketReshardLock::lock() {
  internal_lock.set_must_renew(false);

  int ret;
  if (ephemeral) {
    ret = internal_lock.lock_exclusive_ephemeral(&store->getRados()->reshard_pool_ctx,
						 lock_oid);
  } else {
    ret = internal_lock.lock_exclusive(&store->getRados()->reshard_pool_ctx, lock_oid);
  }

  if (ret == -EBUSY) {
    ldout(store->ctx(), 0) << "INFO: RGWReshardLock::" << __func__ <<
      " found lock on " << lock_oid <<
      " to be held by another RGW process; skipping for now" << dendl;
    return ret;
  } else if (ret < 0) {
    lderr(store->ctx()) << "ERROR: RGWReshardLock::" << __func__ <<
      " failed to acquire lock on " << lock_oid << ": " <<
      cpp_strerror(-ret) << dendl;
    return ret;
  }

  reset_time(Clock::now());

  return 0;
}

void RGWBucketReshardLock::unlock() {
  int ret = internal_lock.unlock(&store->getRados()->reshard_pool_ctx, lock_oid);
  if (ret < 0) {
    ldout(store->ctx(), 0) << "WARNING: RGWBucketReshardLock::" << __func__ <<
      " failed to drop lock on " << lock_oid << " ret=" << ret << dendl;
  }
}

int RGWBucketReshardLock::renew(const Clock::time_point& now) {
  internal_lock.set_must_renew(true);
  int ret;
  if (ephemeral) {
    ret = internal_lock.lock_exclusive_ephemeral(&store->getRados()->reshard_pool_ctx,
						 lock_oid);
  } else {
    ret = internal_lock.lock_exclusive(&store->getRados()->reshard_pool_ctx, lock_oid);
  }
  if (ret < 0) { /* expired or already locked by another processor */
    std::stringstream error_s;
    if (-ENOENT == ret) {
      error_s << "ENOENT (lock expired or never initially locked)";
    } else {
      error_s << ret << " (" << cpp_strerror(-ret) << ")";
    }
    ldout(store->ctx(), 5) << __func__ << "(): failed to renew lock on " <<
      lock_oid << " with error " << error_s.str() << dendl;
    return ret;
  }
  internal_lock.set_must_renew(false);

  reset_time(now);
  ldout(store->ctx(), 20) << __func__ << "(): successfully renewed lock on " <<
    lock_oid << dendl;

  return 0;
}


int RGWBucketReshard::do_reshard(int num_shards,
				 RGWBucketInfo& new_bucket_info,
				 int max_entries,
				 bool verbose,
				 ostream *out,
				 Formatter *formatter)
{
  rgw_bucket& bucket = bucket_info.bucket;

  int ret = 0;

  if (out) {
    (*out) << "tenant: " << bucket_info.bucket.tenant << std::endl;
    (*out) << "bucket name: " << bucket_info.bucket.name << std::endl;
    (*out) << "old bucket instance id: " << bucket_info.bucket.bucket_id <<
      std::endl;
    (*out) << "new bucket instance id: " << new_bucket_info.bucket.bucket_id <<
      std::endl;
  }

  /* update bucket info -- in progress*/
  list<rgw_cls_bi_entry> entries;

  if (max_entries < 0) {
    ldout(store->ctx(), 0) << __func__ <<
      ": can't reshard, negative max_entries" << dendl;
    return -EINVAL;
  }

  // NB: destructor cleans up sharding state if reshard does not
  // complete successfully
  BucketInfoReshardUpdate bucket_info_updater(store, bucket_info, bucket_attrs, new_bucket_info.bucket.bucket_id);

  ret = bucket_info_updater.start();
  if (ret < 0) {
    ldout(store->ctx(), 0) << __func__ << ": failed to update bucket info ret=" << ret << dendl;
    return ret;
  }

  int num_target_shards = (new_bucket_info.num_shards > 0 ? new_bucket_info.num_shards : 1);

  BucketReshardManager target_shards_mgr(store, new_bucket_info, num_target_shards);

  bool verbose_json_out = verbose && (formatter != nullptr) && (out != nullptr);

  if (verbose_json_out) {
    formatter->open_array_section("entries");
  }

  uint64_t total_entries = 0;

  if (!verbose_json_out && out) {
    (*out) << "total entries:";
  }

  const int num_source_shards =
    (bucket_info.num_shards > 0 ? bucket_info.num_shards : 1);
  string marker;
  for (int i = 0; i < num_source_shards; ++i) {
    bool is_truncated = true;
    marker.clear();
    const std::string null_object_filter; // empty string since we're not filtering by object
    while (is_truncated) {
      entries.clear();
      ret = store->getRados()->bi_list(bucket, i, null_object_filter, marker, max_entries, &entries, &is_truncated);
      if (ret < 0 && ret != -ENOENT) {
	derr << "ERROR: bi_list(): " << cpp_strerror(-ret) << dendl;
	return ret;
      }

      for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
	rgw_cls_bi_entry& entry = *iter;
	if (verbose_json_out) {
	  formatter->open_object_section("entry");

	  encode_json("shard_id", i, formatter);
	  encode_json("num_entry", total_entries, formatter);
	  encode_json("entry", entry, formatter);
	}
	total_entries++;

	marker = entry.idx;

	int target_shard_id;
	cls_rgw_obj_key cls_key;
	RGWObjCategory category;
	rgw_bucket_category_stats stats;
	bool account = entry.get_info(&cls_key, &category, &stats);
	rgw_obj_key key(cls_key);
	rgw_obj obj(new_bucket_info.bucket, key);
	RGWMPObj mp;
	if (key.ns == RGW_OBJ_NS_MULTIPART && mp.from_meta(key.name)) {
	  // place the multipart .meta object on the same shard as its head object
	  obj.index_hash_source = mp.get_key();
	}
	int ret = store->getRados()->get_target_shard_id(new_bucket_info, obj.get_hash_object(), &target_shard_id);
	if (ret < 0) {
	  lderr(store->ctx()) << "ERROR: get_target_shard_id() returned ret=" << ret << dendl;
	  return ret;
	}

	int shard_index = (target_shard_id > 0 ? target_shard_id : 0);

	ret = target_shards_mgr.add_entry(shard_index, entry, account,
					  category, stats);
	if (ret < 0) {
	  return ret;
	}

	Clock::time_point now = Clock::now();
	if (reshard_lock.should_renew(now)) {
	  // assume outer locks have timespans at least the size of ours, so
	  // can call inside conditional
	  if (outer_reshard_lock) {
	    ret = outer_reshard_lock->renew(now);
	    if (ret < 0) {
	      return ret;
	    }
	  }
	  ret = reshard_lock.renew(now);
	  if (ret < 0) {
	    lderr(store->ctx()) << "Error renewing bucket lock: " << ret << dendl;
	    return ret;
	  }
	}
	if (verbose_json_out) {
	  formatter->close_section();
	  formatter->flush(*out);
	} else if (out && !(total_entries % 1000)) {
	  (*out) << " " << total_entries;
	}
      } // entries loop
    }
  }

  if (verbose_json_out) {
    formatter->close_section();
    formatter->flush(*out);
  } else if (out) {
    (*out) << " " << total_entries << std::endl;
  }

  ret = target_shards_mgr.finish();
  if (ret < 0) {
    lderr(store->ctx()) << "ERROR: failed to reshard" << dendl;
    return -EIO;
  }

  ret = store->ctl()->bucket->link_bucket(new_bucket_info.owner, new_bucket_info.bucket, bucket_info.creation_time, null_yield);
  if (ret < 0) {
    lderr(store->ctx()) << "failed to link new bucket instance (bucket_id=" << new_bucket_info.bucket.bucket_id << ": " << cpp_strerror(-ret) << ")" << dendl;
    return ret;
  }

  ret = bucket_info_updater.complete();
  if (ret < 0) {
    ldout(store->ctx(), 0) << __func__ << ": failed to update bucket info ret=" << ret << dendl;
    /* don't error out, reshard process succeeded */
  }

  return 0;
  // NB: some error clean-up is done by ~BucketInfoReshardUpdate
} // RGWBucketReshard::do_reshard

int RGWBucketReshard::get_status(list<cls_rgw_bucket_instance_entry> *status)
{
  return store->svc()->bi_rados->get_reshard_status(bucket_info, status);
}


int RGWBucketReshard::execute(int num_shards, int max_op_entries,
                              bool verbose, ostream *out, Formatter *formatter,
			      RGWReshard* reshard_log)
{
  int ret = reshard_lock.lock();
  if (ret < 0) {
    return ret;
  }

  RGWBucketInfo new_bucket_info;
  ret = create_new_bucket_instance(num_shards, new_bucket_info);
  if (ret < 0) {
    // shard state is uncertain, but this will attempt to remove them anyway
    goto error_out;
  }

  if (reshard_log) {
    ret = reshard_log->update(bucket_info, new_bucket_info);
    if (ret < 0) {
      goto error_out;
    }
  }

  // set resharding status of current bucket_info & shards with
  // information about planned resharding
  ret = set_resharding_status(new_bucket_info.bucket.bucket_id,
			      num_shards, cls_rgw_reshard_status::IN_PROGRESS);
  if (ret < 0) {
    goto error_out;
  }

  ret = do_reshard(num_shards,
		   new_bucket_info,
		   max_op_entries,
                   verbose, out, formatter);
  if (ret < 0) {
    goto error_out;
  }

  // at this point we've done the main work; we'll make a best-effort
  // to clean-up but will not indicate any errors encountered

  reshard_lock.unlock();

  // resharding successful, so remove old bucket index shards; use
  // best effort and don't report out an error; the lock isn't needed
  // at this point since all we're using a best effor to to remove old
  // shard objects
  ret = store->svc()->bi->clean_index(bucket_info);
  if (ret < 0) {
    lderr(store->ctx()) << "Error: " << __func__ <<
      " failed to clean up old shards; " <<
      "RGWRados::clean_bucket_index returned " << ret << dendl;
  }

  ret = store->ctl()->bucket->remove_bucket_instance_info(bucket_info.bucket,
                                                       bucket_info, null_yield);
  if (ret < 0) {
    lderr(store->ctx()) << "Error: " << __func__ <<
      " failed to clean old bucket info object \"" <<
      bucket_info.bucket.get_key() <<
      "\"created after successful resharding with error " << ret << dendl;
  }

  ldout(store->ctx(), 1) << __func__ <<
    " INFO: reshard of bucket \"" << bucket_info.bucket.name << "\" from \"" <<
    bucket_info.bucket.get_key() << "\" to \"" <<
    new_bucket_info.bucket.get_key() << "\" completed successfully" << dendl;

  return 0;

error_out:

  reshard_lock.unlock();

  // since the real problem is the issue that led to this error code
  // path, we won't touch ret and instead use another variable to
  // temporarily error codes
  int ret2 = store->svc()->bi->clean_index(new_bucket_info);
  if (ret2 < 0) {
    lderr(store->ctx()) << "Error: " << __func__ <<
      " failed to clean up shards from failed incomplete resharding; " <<
      "RGWRados::clean_bucket_index returned " << ret2 << dendl;
  }

  ret2 = store->ctl()->bucket->remove_bucket_instance_info(new_bucket_info.bucket,
                                                        new_bucket_info,
							null_yield);
  if (ret2 < 0) {
    lderr(store->ctx()) << "Error: " << __func__ <<
      " failed to clean bucket info object \"" <<
      new_bucket_info.bucket.get_key() <<
      "\"created during incomplete resharding with error " << ret2 << dendl;
  }

  return ret;
} // execute


RGWReshard::RGWReshard(rgw::sal::RGWRadosStore* _store, bool _verbose, ostream *_out,
                       Formatter *_formatter) :
  store(_store), instance_lock(bucket_instance_lock_name),
  verbose(_verbose), out(_out), formatter(_formatter)
{
  num_logshards = store->ctx()->_conf.get_val<uint64_t>("rgw_reshard_num_logs");
}

string RGWReshard::get_logshard_key(const string& tenant,
				    const string& bucket_name)
{
  return tenant + ":" + bucket_name;
}

#define MAX_RESHARD_LOGSHARDS_PRIME 7877

void RGWReshard::get_bucket_logshard_oid(const string& tenant, const string& bucket_name, string *oid)
{
  string key = get_logshard_key(tenant, bucket_name);

  uint32_t sid = ceph_str_hash_linux(key.c_str(), key.size());
  uint32_t sid2 = sid ^ ((sid & 0xFF) << 24);
  sid = sid2 % MAX_RESHARD_LOGSHARDS_PRIME % num_logshards;

  get_logshard_oid(int(sid), oid);
}

int RGWReshard::add(cls_rgw_reshard_entry& entry)
{
  if (!store->svc()->zone->can_reshard()) {
    ldout(store->ctx(), 20) << __func__ << " Resharding is disabled"  << dendl;
    return 0;
  }

  string logshard_oid;

  get_bucket_logshard_oid(entry.tenant, entry.bucket_name, &logshard_oid);

  librados::ObjectWriteOperation op;
  cls_rgw_reshard_add(op, entry);

  int ret = rgw_rados_operate(store->getRados()->reshard_pool_ctx, logshard_oid, &op, null_yield);
  if (ret < 0) {
    lderr(store->ctx()) << "ERROR: failed to add entry to reshard log, oid=" << logshard_oid << " tenant=" << entry.tenant << " bucket=" << entry.bucket_name << dendl;
    return ret;
  }
  return 0;
}

int RGWReshard::update(const RGWBucketInfo& bucket_info, const RGWBucketInfo& new_bucket_info)
{
  cls_rgw_reshard_entry entry;
  entry.bucket_name = bucket_info.bucket.name;
  entry.bucket_id = bucket_info.bucket.bucket_id;
  entry.tenant = bucket_info.owner.tenant;

  int ret = get(entry);
  if (ret < 0) {
    return ret;
  }

  entry.new_instance_id = new_bucket_info.bucket.name + ":"  + new_bucket_info.bucket.bucket_id;

  ret = add(entry);
  if (ret < 0) {
    ldout(store->ctx(), 0) << __func__ << ":Error in updating entry bucket " << entry.bucket_name << ": " <<
      cpp_strerror(-ret) << dendl;
  }

  return ret;
}


int RGWReshard::list(int logshard_num, string& marker, uint32_t max, std::list<cls_rgw_reshard_entry>& entries, bool *is_truncated)
{
  string logshard_oid;

  get_logshard_oid(logshard_num, &logshard_oid);

  int ret = cls_rgw_reshard_list(store->getRados()->reshard_pool_ctx, logshard_oid, marker, max, entries, is_truncated);

  if (ret == -ENOENT) {
    // these shard objects aren't created until we actually write something to
    // them, so treat ENOENT as a successful empty listing
    *is_truncated = false;
    ret = 0;
  } else if (ret == -EACCES) {
    lderr(store->ctx()) << "ERROR: access denied to pool " << store->svc()->zone->get_zone_params().reshard_pool
                      << ". Fix the pool access permissions of your client" << dendl;
  } else if (ret < 0) {
    lderr(store->ctx()) << "ERROR: failed to list reshard log entries, oid="
        << logshard_oid << " marker=" << marker << " " << cpp_strerror(ret) << dendl;
  }

  return ret;
}

int RGWReshard::get(cls_rgw_reshard_entry& entry)
{
  string logshard_oid;

  get_bucket_logshard_oid(entry.tenant, entry.bucket_name, &logshard_oid);

  int ret = cls_rgw_reshard_get(store->getRados()->reshard_pool_ctx, logshard_oid, entry);
  if (ret < 0) {
    if (ret != -ENOENT) {
      lderr(store->ctx()) << "ERROR: failed to get entry from reshard log, oid=" << logshard_oid << " tenant=" << entry.tenant <<
	" bucket=" << entry.bucket_name << dendl;
    }
    return ret;
  }

  return 0;
}

int RGWReshard::remove(cls_rgw_reshard_entry& entry)
{
  string logshard_oid;

  get_bucket_logshard_oid(entry.tenant, entry.bucket_name, &logshard_oid);

  librados::ObjectWriteOperation op;
  cls_rgw_reshard_remove(op, entry);

  int ret = rgw_rados_operate(store->getRados()->reshard_pool_ctx, logshard_oid, &op, null_yield);
  if (ret < 0) {
    lderr(store->ctx()) << "ERROR: failed to remove entry from reshard log, oid=" << logshard_oid << " tenant=" << entry.tenant << " bucket=" << entry.bucket_name << dendl;
    return ret;
  }

  return ret;
}

int RGWReshard::clear_bucket_resharding(const string& bucket_instance_oid, cls_rgw_reshard_entry& entry)
{
  int ret = cls_rgw_clear_bucket_resharding(store->getRados()->reshard_pool_ctx, bucket_instance_oid);
  if (ret < 0) {
    lderr(store->ctx()) << "ERROR: failed to clear bucket resharding, bucket_instance_oid=" << bucket_instance_oid << dendl;
    return ret;
  }

  return 0;
}

int RGWReshardWait::wait(optional_yield y)
{
  std::unique_lock lock(mutex);

  if (going_down) {
    return -ECANCELED;
  }

#ifdef HAVE_BOOST_CONTEXT
  if (y) {
    auto& context = y.get_io_context();
    auto& yield = y.get_yield_context();

    Waiter waiter(context);
    waiters.push_back(waiter);
    lock.unlock();

    waiter.timer.expires_after(duration);

    boost::system::error_code ec;
    waiter.timer.async_wait(yield[ec]);

    lock.lock();
    waiters.erase(waiters.iterator_to(waiter));
    return -ec.value();
  }
#endif

  cond.wait_for(lock, duration);

  if (going_down) {
    return -ECANCELED;
  }

  return 0;
}

void RGWReshardWait::stop()
{
  std::scoped_lock lock(mutex);
  going_down = true;
  cond.notify_all();
  for (auto& waiter : waiters) {
    // unblock any waiters with ECANCELED
    waiter.timer.cancel();
  }
}

int RGWReshard::process_single_logshard(int logshard_num)
{
  string marker;
  bool truncated = true;

  CephContext *cct = store->ctx();
  constexpr uint32_t max_entries = 1000;

  string logshard_oid;
  get_logshard_oid(logshard_num, &logshard_oid);

  RGWBucketReshardLock logshard_lock(store, logshard_oid, false);

  int ret = logshard_lock.lock();
  if (ret < 0) { 
    ldout(store->ctx(), 5) << __func__ << "(): failed to acquire lock on " <<
      logshard_oid << ", ret = " << ret <<dendl;
    return ret;
  }
  
  do {
    std::list<cls_rgw_reshard_entry> entries;
    ret = list(logshard_num, marker, max_entries, entries, &truncated);
    if (ret < 0) {
      ldout(cct, 10) << "cannot list all reshards in logshard oid=" <<
	logshard_oid << dendl;
      continue;
    }

    for(auto& entry: entries) { // logshard entries
      if(entry.new_instance_id.empty()) {

	ldout(store->ctx(), 20) << __func__ << " resharding " <<
	  entry.bucket_name  << dendl;

	rgw_bucket bucket;
	RGWBucketInfo bucket_info;
	map<string, bufferlist> attrs;

	ret = store->getRados()->get_bucket_info(store->svc(),
						 entry.tenant, entry.bucket_name,
						 bucket_info, nullptr,
						 null_yield, &attrs);
	if (ret < 0 || bucket_info.bucket.bucket_id != entry.bucket_id) {
	  if (ret < 0) {
	    ldout(cct, 0) <<  __func__ <<
	      ": Error in get_bucket_info for bucket " << entry.bucket_name <<
	      ": " << cpp_strerror(-ret) << dendl;
	    if (ret != -ENOENT) {
	      // any error other than ENOENT will abort
	      return ret;
	    }
	  } else {
	    ldout(cct,0) << __func__ <<
	      ": Bucket: " << entry.bucket_name <<
	      " already resharded by someone, skipping " << dendl;
	  }

	  // we've encountered a reshard queue entry for an apparently
	  // non-existent bucket; let's try to recover by cleaning up
	  ldout(cct, 0) <<  __func__ <<
	    ": removing reshard queue entry for a resharded or non-existent bucket" <<
	    entry.bucket_name << dendl;

	  ret = remove(entry);
	  if (ret < 0) {
	    ldout(cct, 0) << __func__ <<
	      ": Error removing non-existent bucket " <<
	      entry.bucket_name << " from resharding queue: " <<
	      cpp_strerror(-ret) << dendl;
	    return ret;
	  }

	  // we cleaned up, move on to the next entry
	  goto finished_entry;
	}

	RGWBucketReshard br(store, bucket_info, attrs, nullptr);
	ret = br.execute(entry.new_num_shards, max_entries, false, nullptr,
			 nullptr, this);
	if (ret < 0) {
	  ldout(store->ctx(), 0) <<  __func__ <<
	    ": Error during resharding bucket " << entry.bucket_name << ":" <<
	    cpp_strerror(-ret)<< dendl;
	  return ret;
	}

	ldout(store->ctx(), 20) << __func__ <<
	  " removing reshard queue entry for bucket " << entry.bucket_name <<
	  dendl;

      	ret = remove(entry);
	if (ret < 0) {
	  ldout(cct, 0) << __func__ << ": Error removing bucket " <<
	    entry.bucket_name << " from resharding queue: " <<
	    cpp_strerror(-ret) << dendl;
	  return ret;
	}
      } // if new instance id is empty

    finished_entry:

      Clock::time_point now = Clock::now();
      if (logshard_lock.should_renew(now)) {
	ret = logshard_lock.renew(now);
	if (ret < 0) {
	  return ret;
	}
      }

      entry.get_key(&marker);
    } // entry for loop
  } while (truncated);

  logshard_lock.unlock();
  return 0;
}


void  RGWReshard::get_logshard_oid(int shard_num, string *logshard)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%010u", (unsigned)shard_num);

  string objname(reshard_oid_prefix);
  *logshard =  objname + buf;
}

int RGWReshard::process_all_logshards()
{
  if (!store->svc()->zone->can_reshard()) {
    ldout(store->ctx(), 20) << __func__ << " Resharding is disabled"  << dendl;
    return 0;
  }
  int ret = 0;

  for (int i = 0; i < num_logshards; i++) {
    string logshard;
    get_logshard_oid(i, &logshard);

    ldout(store->ctx(), 20) << "processing logshard = " << logshard << dendl;

    ret = process_single_logshard(i);

    ldout(store->ctx(), 20) << "finish processing logshard = " << logshard << " , ret = " << ret << dendl;
  }

  return 0;
}

bool RGWReshard::going_down()
{
  return down_flag;
}

void RGWReshard::start_processor()
{
  worker = new ReshardWorker(store->ctx(), this);
  worker->create("rgw_reshard");
}

void RGWReshard::stop_processor()
{
  down_flag = true;
  if (worker) {
    worker->stop();
    worker->join();
  }
  delete worker;
  worker = nullptr;
}

void *RGWReshard::ReshardWorker::entry() {
  do {
    utime_t start = ceph_clock_now();
    reshard->process_all_logshards();

    if (reshard->going_down())
      break;

    utime_t end = ceph_clock_now();
    end -= start;
    int secs = cct->_conf.get_val<uint64_t>("rgw_reshard_thread_interval");

    if (secs <= end.sec())
      continue; // next round

    secs -= end.sec();

    std::unique_lock locker{lock};
    cond.wait_for(locker, std::chrono::seconds(secs));
  } while (!reshard->going_down());

  return NULL;
}

void RGWReshard::ReshardWorker::stop()
{
  std::lock_guard l{lock};
  cond.notify_all();
}
