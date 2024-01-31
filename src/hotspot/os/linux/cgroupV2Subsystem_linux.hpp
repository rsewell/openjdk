/*
 * Copyright (c) 2020, 2022, Red Hat Inc.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef CGROUP_V2_SUBSYSTEM_LINUX_HPP
#define CGROUP_V2_SUBSYSTEM_LINUX_HPP

#include "cgroupSubsystem_linux.hpp"

class CgroupV2Controller: public CgroupController {
  private:
    /* the mount path of the cgroup v2 hierarchy */
    char *_mount_path;
    /* The cgroup path for the controller */
    char *_cgroup_path;

    /* Constructed full path to the subsystem directory */
    size_t _paths_size;
    char **_paths;
    static char* construct_path(char* mount_path, char *cgroup_path);

  public:
    CgroupV2Controller(char * mount_path, char *cgroup_path);

    char *subsystem_path(size_t ix) { return ix < _paths_size ? _paths[ix] : nullptr; }
};

class CgroupV2Subsystem: public CgroupSubsystem {
  private:
    /* One unified controller */
    CgroupController* _unified = nullptr;
    /* Caching wrappers for cpu/memory metrics */
    CachingCgroupController* _memory = nullptr;
    CachingCgroupController* _cpu = nullptr;

    char *mem_limit_val(size_t dir_ix);
    char *mem_swp_limit_val(size_t dir_ix);
    char *mem_swp_current_val();
    char *mem_soft_limit_val();
    char *cpu_quota_val();
    char *pids_max_val();

    jlong dir_iterate(char *(CgroupV2Subsystem::*method_ptr)(size_t dir_ix), char *first_val = nullptr);
    jlong read_hierarchical_memsw_limit() const;
    jlong read_hierarchical_memory_limit() const;

  public:
    CgroupV2Subsystem(CgroupController * unified) {
      _unified = unified;
      _memory = new CachingCgroupController(unified);
      _cpu = new CachingCgroupController(unified);
    }

    jlong read_memory_limit_in_bytes();
    int cpu_quota();
    int cpu_period();
    int cpu_shares();
    jlong memory_and_swap_limit_in_bytes();
    jlong memory_soft_limit_in_bytes();
    jlong memory_usage_in_bytes();
    jlong memory_max_usage_in_bytes();
    jlong rss_usage_in_bytes();
    jlong cache_usage_in_bytes();

    char * cpu_cpuset_cpus();
    char * cpu_cpuset_memory_nodes();
    jlong pids_max();
    jlong pids_current();

    void print_version_specific_info(outputStream* st);

    const char * container_type() {
      return "cgroupv2";
    }
    CachingCgroupController * memory_controller() { return _memory; }
    CachingCgroupController * cpu_controller() { return _cpu; }
};

#endif // CGROUP_V2_SUBSYSTEM_LINUX_HPP
