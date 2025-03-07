// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef CEPH_MDS_SNAP_H
#define CEPH_MDS_SNAP_H

#include <string_view>

#include "mdstypes.h"
#include "common/snap_types.h"

#include "Capability.h"

/*
 * generic snap descriptor.
 */
struct SnapInfo {
  void encode(bufferlist &bl) const;
  void decode(bufferlist::const_iterator &bl);
  void dump(Formatter *f) const;
  static void generate_test_instances(std::list<SnapInfo*>& ls);

  std::string_view get_long_name() const;

  snapid_t snapid;
  inodeno_t ino;
  utime_t stamp;
  string name;

  mutable string long_name; ///< cached _$ino_$name
};
WRITE_CLASS_ENCODER(SnapInfo)

inline bool operator==(const SnapInfo &l, const SnapInfo &r)
{
  return l.snapid == r.snapid && l.ino == r.ino &&
	 l.stamp == r.stamp && l.name == r.name;
}

ostream& operator<<(ostream& out, const SnapInfo &sn);

/*
 * SnapRealm - a subtree that shares the same set of snapshots.
 */
struct SnapRealm;

struct snaplink_t {
  void encode(bufferlist &bl) const;
  void decode(bufferlist::const_iterator &bl);
  void dump(Formatter *f) const;
  static void generate_test_instances(std::list<snaplink_t*>& ls);

  inodeno_t ino;
  snapid_t first;
};
WRITE_CLASS_ENCODER(snaplink_t)

ostream& operator<<(ostream& out, const snaplink_t &l);

// carry data about a specific version of a SnapRealm
struct sr_t {
  void mark_parent_global() { flags |= PARENT_GLOBAL; }
  void clear_parent_global() { flags &= ~PARENT_GLOBAL; }
  bool is_parent_global() const { return flags & PARENT_GLOBAL; }

  void mark_subvolume() { flags |= SUBVOLUME; }
  void clear_subvolume() { flags &= ~SUBVOLUME; }
  bool is_subvolume() const { return flags & SUBVOLUME; }

  void encode(bufferlist &bl) const;
  void decode(bufferlist::const_iterator &bl);
  void dump(Formatter *f) const;
  static void generate_test_instances(std::list<sr_t*>& ls);

  snapid_t seq = 0;                     // basically, a version/seq # for changes to _this_ realm.
  snapid_t created = 0;                 // when this realm was created.
  snapid_t last_created = 0;            // last snap created in _this_ realm.
  snapid_t last_destroyed = 0;          // seq for last removal
  snapid_t current_parent_since = 1;
  map<snapid_t, SnapInfo> snaps;
  map<snapid_t, snaplink_t> past_parents;  // key is "last" (or NOSNAP)
  set<snapid_t> past_parent_snaps;

  __u32 flags = 0;
  enum {
    PARENT_GLOBAL	= 1 << 0,
    SUBVOLUME		= 1 << 1,
  };
};
WRITE_CLASS_ENCODER(sr_t)

class MDCache;
#endif
