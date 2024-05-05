/*
 * Copyright (c) 2019, 2023, Oracle and/or its affiliates. All rights reserved.
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

#include <string.h>
#include <math.h>
#include <errno.h>
#include "cgroupSubsystem_linux.hpp"
#include "cgroupV1Subsystem_linux.hpp"
#include "cgroupV2Subsystem_linux.hpp"
#include "cgroupUtil_linux.hpp"
#include "logging/log.hpp"
#include "memory/allocation.hpp"
#include "os_linux.hpp"
#include "runtime/globals.hpp"
#include "runtime/os.hpp"
#include "utilities/globalDefinitions.hpp"

// controller names have to match the *_IDX indices
static const char* cg_controller_name[] = { "cpu", "cpuset", "cpuacct", "memory", "pids" };

CgroupSubsystem* CgroupSubsystemFactory::create() {
  CgroupV1Controller* memory = nullptr;
  CgroupV1Controller* cpuset = nullptr;
  CgroupV1CpuController* cpu = nullptr;
  CgroupV1Controller* cpuacct = nullptr;
  CgroupV1Controller* pids = nullptr;
  CgroupInfo cg_infos[CG_INFO_LENGTH];
  u1 cg_type_flags = INVALID_CGROUPS_GENERIC;
  const char* proc_cgroups = "/proc/cgroups";
  const char* proc_self_cgroup = "/proc/self/cgroup";
  const char* proc_self_mountinfo = "/proc/self/mountinfo";

  bool valid_cgroup = determine_type(cg_infos, proc_cgroups, proc_self_cgroup, proc_self_mountinfo, &cg_type_flags);

  if (!valid_cgroup) {
    // Could not detect cgroup type
    return nullptr;
  }
  assert(is_valid_cgroup(&cg_type_flags), "Expected valid cgroup type");

  if (is_cgroup_v2(&cg_type_flags)) {
    // Cgroups v2 case, we have all the info we need.
    // Construct the subsystem, free resources and return
    // Note: any index in cg_infos will do as the path is the same for
    //       all controllers.
    CgroupV2MemoryController* memory = new CgroupV2MemoryController(cg_infos[MEMORY_IDX]._root_mount_path, cg_infos[MEMORY_IDX]._mount_path);
    CgroupV2CpuController* cpu = new CgroupV2CpuController(cg_infos[CPU_IDX]._root_mount_path, cg_infos[CPU_IDX]._mount_path);
    memory->set_subsystem_path(cg_infos[MEMORY_IDX]._cgroup_path);
    log_debug(os, container)("Detected cgroups v2 unified hierarchy");
    cleanup(cg_infos);
    return new CgroupV2Subsystem(memory, cpu);
  }

  /*
   * Cgroup v1 case:
   *
   * Use info gathered previously from /proc/self/cgroup
   * and map host mount point to
   * local one via /proc/self/mountinfo content above
   *
   * Docker example:
   * 5:memory:/docker/6558aed8fc662b194323ceab5b964f69cf36b3e8af877a14b80256e93aecb044
   *
   * Host example:
   * 5:memory:/user.slice
   *
   * Construct a path to the process specific memory and cpuset
   * cgroup directory.
   *
   * For a container running under Docker from memory example above
   * the paths would be:
   *
   * /sys/fs/cgroup/memory
   *
   * For a Host from memory example above the path would be:
   *
   * /sys/fs/cgroup/memory/user.slice
   *
   */
  assert(is_cgroup_v1(&cg_type_flags), "Cgroup v1 expected");
  for (int i = 0; i < CG_INFO_LENGTH; i++) {
    CgroupInfo info = cg_infos[i];
    if (info._data_complete) { // pids controller might have incomplete data
      if (strcmp(info._name, "memory") == 0) {
        memory = new CgroupV1Controller(info._root_mount_path, info._mount_path);
        memory->set_subsystem_path(info._cgroup_path);
      } else if (strcmp(info._name, "cpuset") == 0) {
        cpuset = new CgroupV1Controller(info._root_mount_path, info._mount_path);
        cpuset->set_subsystem_path(info._cgroup_path);
      } else if (strcmp(info._name, "cpu") == 0) {
        cpu = new CgroupV1CpuController(info._root_mount_path, info._mount_path);
        cpu->set_subsystem_path(info._cgroup_path);
      } else if (strcmp(info._name, "cpuacct") == 0) {
        cpuacct = new CgroupV1Controller(info._root_mount_path, info._mount_path);
        cpuacct->set_subsystem_path(info._cgroup_path);
      } else if (strcmp(info._name, "pids") == 0) {
        pids = new CgroupV1Controller(info._root_mount_path, info._mount_path);
        pids->set_subsystem_path(info._cgroup_path);
      }
    } else {
      log_debug(os, container)("CgroupInfo for %s not complete", cg_controller_name[i]);
    }
  }
  cleanup(cg_infos);
  return new CgroupV1Subsystem(cpuset, cpu, cpuacct, pids, memory);
}

void CgroupSubsystemFactory::set_controller_paths(CgroupInfo* cg_infos,
                                                  int controller,
                                                  const char* name,
                                                  char* mount_path,
                                                  char* root_path) {
  if (cg_infos[controller]._mount_path != nullptr) {
    // On some systems duplicate controllers get mounted in addition to
    // the main cgroup controllers most likely under /sys/fs/cgroup. In that
    // case pick the one under /sys/fs/cgroup and discard others.
    if (strstr(cg_infos[controller]._mount_path, "/sys/fs/cgroup") != cg_infos[controller]._mount_path) {
      log_debug(os, container)("Duplicate %s controllers detected. Picking %s, skipping %s.",
                               name, mount_path, cg_infos[controller]._mount_path);
      os::free(cg_infos[controller]._mount_path);
      os::free(cg_infos[controller]._root_mount_path);
      cg_infos[controller]._mount_path = os::strdup(mount_path);
      cg_infos[controller]._root_mount_path = os::strdup(root_path);
    } else {
      log_debug(os, container)("Duplicate %s controllers detected. Picking %s, skipping %s.",
                               name, cg_infos[controller]._mount_path, mount_path);
    }
  } else {
    cg_infos[controller]._mount_path = os::strdup(mount_path);
    cg_infos[controller]._root_mount_path = os::strdup(root_path);
  }
}

bool CgroupSubsystemFactory::determine_type(CgroupInfo* cg_infos,
                                            const char* proc_cgroups,
                                            const char* proc_self_cgroup,
                                            const char* proc_self_mountinfo,
                                            u1* flags) {
  FILE *mntinfo = nullptr;
  FILE *cgroups = nullptr;
  FILE *cgroup = nullptr;
  char buf[MAXPATHLEN+1];
  char *p;
  bool is_cgroupsV2;
  // true iff all required controllers, memory, cpu, cpuset, cpuacct are enabled
  // at the kernel level.
  // pids might not be enabled on older Linux distros (SLES 12.1, RHEL 7.1)
  bool all_required_controllers_enabled;

  /*
   * Read /proc/cgroups so as to be able to distinguish cgroups v2 vs cgroups v1.
   *
   * For cgroups v1 hierarchy (hybrid or legacy), cpu, cpuacct, cpuset, memory controllers
   * must have non-zero for the hierarchy ID field and relevant controllers mounted.
   * Conversely, for cgroups v2 (unified hierarchy), cpu, cpuacct, cpuset, memory
   * controllers must have hierarchy ID 0 and the unified controller mounted.
   */
  cgroups = os::fopen(proc_cgroups, "r");
  if (cgroups == nullptr) {
    log_debug(os, container)("Can't open %s, %s", proc_cgroups, os::strerror(errno));
    *flags = INVALID_CGROUPS_GENERIC;
    return false;
  }

  while ((p = fgets(buf, MAXPATHLEN, cgroups)) != nullptr) {
    char name[MAXPATHLEN+1];
    int  hierarchy_id;
    int  enabled;

    // Format of /proc/cgroups documented via man 7 cgroups
    if (sscanf(p, "%s %d %*d %d", name, &hierarchy_id, &enabled) != 3) {
      continue;
    }
    if (strcmp(name, "memory") == 0) {
      cg_infos[MEMORY_IDX]._name = os::strdup(name);
      cg_infos[MEMORY_IDX]._hierarchy_id = hierarchy_id;
      cg_infos[MEMORY_IDX]._enabled = (enabled == 1);
    } else if (strcmp(name, "cpuset") == 0) {
      cg_infos[CPUSET_IDX]._name = os::strdup(name);
      cg_infos[CPUSET_IDX]._hierarchy_id = hierarchy_id;
      cg_infos[CPUSET_IDX]._enabled = (enabled == 1);
    } else if (strcmp(name, "cpu") == 0) {
      cg_infos[CPU_IDX]._name = os::strdup(name);
      cg_infos[CPU_IDX]._hierarchy_id = hierarchy_id;
      cg_infos[CPU_IDX]._enabled = (enabled == 1);
    } else if (strcmp(name, "cpuacct") == 0) {
      cg_infos[CPUACCT_IDX]._name = os::strdup(name);
      cg_infos[CPUACCT_IDX]._hierarchy_id = hierarchy_id;
      cg_infos[CPUACCT_IDX]._enabled = (enabled == 1);
    } else if (strcmp(name, "pids") == 0) {
      log_debug(os, container)("Detected optional pids controller entry in %s", proc_cgroups);
      cg_infos[PIDS_IDX]._name = os::strdup(name);
      cg_infos[PIDS_IDX]._hierarchy_id = hierarchy_id;
      cg_infos[PIDS_IDX]._enabled = (enabled == 1);
    }
  }
  fclose(cgroups);

  is_cgroupsV2 = true;
  all_required_controllers_enabled = true;
  for (int i = 0; i < CG_INFO_LENGTH; i++) {
    // pids controller is optional. All other controllers are required
    if (i != PIDS_IDX) {
      is_cgroupsV2 = is_cgroupsV2 && cg_infos[i]._hierarchy_id == 0;
      all_required_controllers_enabled = all_required_controllers_enabled && cg_infos[i]._enabled;
    }
    if (log_is_enabled(Debug, os, container) && !cg_infos[i]._enabled) {
      log_debug(os, container)("controller %s is not enabled\n", cg_controller_name[i]);
    }
  }

  if (!all_required_controllers_enabled) {
    // one or more required controllers disabled, disable container support
    log_debug(os, container)("One or more required controllers disabled at kernel level.");
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_GENERIC;
    return false;
  }

  /*
   * Read /proc/self/cgroup and determine:
   *  - the cgroup path for cgroups v2 or
   *  - on a cgroups v1 system, collect info for mapping
   *    the host mount point to the local one via /proc/self/mountinfo below.
   */
  cgroup = os::fopen(proc_self_cgroup, "r");
  if (cgroup == nullptr) {
    log_debug(os, container)("Can't open %s, %s",
                             proc_self_cgroup, os::strerror(errno));
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_GENERIC;
    return false;
  }

  while ((p = fgets(buf, MAXPATHLEN, cgroup)) != nullptr) {
    char *controllers;
    char *token;
    char *hierarchy_id_str;
    int  hierarchy_id;
    char *cgroup_path;

    hierarchy_id_str = strsep(&p, ":");
    hierarchy_id = atoi(hierarchy_id_str);
    /* Get controllers and base */
    controllers = strsep(&p, ":");
    cgroup_path = strsep(&p, "\n");

    if (controllers == nullptr) {
      continue;
    }

    while (!is_cgroupsV2 && (token = strsep(&controllers, ",")) != nullptr) {
      if (strcmp(token, "memory") == 0) {
        assert(hierarchy_id == cg_infos[MEMORY_IDX]._hierarchy_id, "/proc/cgroups and /proc/self/cgroup hierarchy mismatch for memory");
        cg_infos[MEMORY_IDX]._cgroup_path = os::strdup(cgroup_path);
      } else if (strcmp(token, "cpuset") == 0) {
        assert(hierarchy_id == cg_infos[CPUSET_IDX]._hierarchy_id, "/proc/cgroups and /proc/self/cgroup hierarchy mismatch for cpuset");
        cg_infos[CPUSET_IDX]._cgroup_path = os::strdup(cgroup_path);
      } else if (strcmp(token, "cpu") == 0) {
        assert(hierarchy_id == cg_infos[CPU_IDX]._hierarchy_id, "/proc/cgroups and /proc/self/cgroup hierarchy mismatch for cpu");
        cg_infos[CPU_IDX]._cgroup_path = os::strdup(cgroup_path);
      } else if (strcmp(token, "cpuacct") == 0) {
        assert(hierarchy_id == cg_infos[CPUACCT_IDX]._hierarchy_id, "/proc/cgroups and /proc/self/cgroup hierarchy mismatch for cpuacc");
        cg_infos[CPUACCT_IDX]._cgroup_path = os::strdup(cgroup_path);
      } else if (strcmp(token, "pids") == 0) {
        assert(hierarchy_id == cg_infos[PIDS_IDX]._hierarchy_id, "/proc/cgroups (%d) and /proc/self/cgroup (%d) hierarchy mismatch for pids",
                                                                 cg_infos[PIDS_IDX]._hierarchy_id, hierarchy_id);
        cg_infos[PIDS_IDX]._cgroup_path = os::strdup(cgroup_path);
      }
    }
    if (is_cgroupsV2) {
      // On some systems we have mixed cgroups v1 and cgroups v2 controllers (e.g. freezer on cg1 and
      // all relevant controllers on cg2). Only set the cgroup path when we see a hierarchy id of 0.
      if (hierarchy_id != 0) {
        continue;
      }
      for (int i = 0; i < CG_INFO_LENGTH; i++) {
        assert(cg_infos[i]._cgroup_path == nullptr, "cgroup path must only be set once");
        cg_infos[i]._cgroup_path = os::strdup(cgroup_path);
      }
    }
  }
  fclose(cgroup);

  // Find various mount points by reading /proc/self/mountinfo
  // mountinfo format is documented at https://www.kernel.org/doc/Documentation/filesystems/proc.txt
  mntinfo = os::fopen(proc_self_mountinfo, "r");
  if (mntinfo == nullptr) {
      log_debug(os, container)("Can't open %s, %s",
                               proc_self_mountinfo, os::strerror(errno));
      cleanup(cg_infos);
      *flags = INVALID_CGROUPS_GENERIC;
      return false;
  }

  bool cgroupv2_mount_point_found = false;
  bool any_cgroup_mounts_found = false;
  while ((p = fgets(buf, MAXPATHLEN, mntinfo)) != nullptr) {
    char tmp_fs_type[MAXPATHLEN+1];
    char tmproot[MAXPATHLEN+1];
    char tmpmount[MAXPATHLEN+1];
    char tmpcgroups[MAXPATHLEN+1];
    char *cptr = tmpcgroups;
    char *token;

    // Cgroup v2 relevant info. We only look for the _mount_path iff is_cgroupsV2 so
    // as to avoid memory stomping of the _mount_path pointer later on in the cgroup v1
    // block in the hybrid case.
    if (is_cgroupsV2 && sscanf(p, "%*d %*d %*d:%*d %s %s %*[^-]- %s %*s %*s", tmproot, tmpmount, tmp_fs_type) == 3) {
      // we likely have an early match return (e.g. cgroup fs match), be sure we have cgroup2 as fstype
      if (strcmp("cgroup2", tmp_fs_type) == 0) {
        cgroupv2_mount_point_found = true;
        any_cgroup_mounts_found = true;
        for (int i = 0; i < CG_INFO_LENGTH; i++) {
          set_controller_paths(cg_infos, i, "(cg2, unified)", tmpmount, tmproot);
        }
      }
    }

    /* Cgroup v1 relevant info
     *
     * Find the cgroup mount point for memory, cpuset, cpu, cpuacct, pids
     *
     * Example for docker:
     * 219 214 0:29 /docker/7208cebd00fa5f2e342b1094f7bed87fa25661471a4637118e65f1c995be8a34 /sys/fs/cgroup/memory ro,nosuid,nodev,noexec,relatime - cgroup cgroup rw,memory
     *
     * Example for host:
     * 34 28 0:29 / /sys/fs/cgroup/memory rw,nosuid,nodev,noexec,relatime shared:16 - cgroup cgroup rw,memory
     *
     * 44 31 0:39 / /sys/fs/cgroup/pids rw,nosuid,nodev,noexec,relatime shared:23 - cgroup cgroup rw,pids
     */
    if (sscanf(p, "%*d %*d %*d:%*d %s %s %*[^-]- %s %*s %s", tmproot, tmpmount, tmp_fs_type, tmpcgroups) == 4) {
      if (strcmp("cgroup", tmp_fs_type) != 0) {
        // Skip cgroup2 fs lines on hybrid or unified hierarchy.
        continue;
      }
      while ((token = strsep(&cptr, ",")) != nullptr) {
        if (strcmp(token, "memory") == 0) {
          any_cgroup_mounts_found = true;
          set_controller_paths(cg_infos, MEMORY_IDX, token, tmpmount, tmproot);
          cg_infos[MEMORY_IDX]._data_complete = true;
        } else if (strcmp(token, "cpuset") == 0) {
          any_cgroup_mounts_found = true;
          set_controller_paths(cg_infos, CPUSET_IDX, token, tmpmount, tmproot);
          cg_infos[CPUSET_IDX]._data_complete = true;
        } else if (strcmp(token, "cpu") == 0) {
          any_cgroup_mounts_found = true;
          set_controller_paths(cg_infos, CPU_IDX, token, tmpmount, tmproot);
          cg_infos[CPU_IDX]._data_complete = true;
        } else if (strcmp(token, "cpuacct") == 0) {
          any_cgroup_mounts_found = true;
          set_controller_paths(cg_infos, CPUACCT_IDX, token, tmpmount, tmproot);
          cg_infos[CPUACCT_IDX]._data_complete = true;
        } else if (strcmp(token, "pids") == 0) {
          any_cgroup_mounts_found = true;
          set_controller_paths(cg_infos, PIDS_IDX, token, tmpmount, tmproot);
          cg_infos[PIDS_IDX]._data_complete = true;
        }
      }
    }
  }
  fclose(mntinfo);

  // Neither cgroup2 nor cgroup filesystems mounted via /proc/self/mountinfo
  // No point in continuing.
  if (!any_cgroup_mounts_found) {
    log_trace(os, container)("No relevant cgroup controllers mounted.");
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_NO_MOUNT;
    return false;
  }

  if (is_cgroupsV2) {
    if (!cgroupv2_mount_point_found) {
      log_trace(os, container)("Mount point for cgroupv2 not found in /proc/self/mountinfo");
      cleanup(cg_infos);
      *flags = INVALID_CGROUPS_V2;
      return false;
    }
    // Cgroups v2 case, we have all the info we need.
    *flags = CGROUPS_V2;
    return true;
  }

  // What follows is cgroups v1
  log_debug(os, container)("Detected cgroups hybrid or legacy hierarchy, using cgroups v1 controllers");

  if (!cg_infos[MEMORY_IDX]._data_complete) {
    log_debug(os, container)("Required cgroup v1 memory subsystem not found");
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_V1;
    return false;
  }
  if (!cg_infos[CPUSET_IDX]._data_complete) {
    log_debug(os, container)("Required cgroup v1 cpuset subsystem not found");
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_V1;
    return false;
  }
  if (!cg_infos[CPU_IDX]._data_complete) {
    log_debug(os, container)("Required cgroup v1 cpu subsystem not found");
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_V1;
    return false;
  }
  if (!cg_infos[CPUACCT_IDX]._data_complete) {
    log_debug(os, container)("Required cgroup v1 cpuacct subsystem not found");
    cleanup(cg_infos);
    *flags = INVALID_CGROUPS_V1;
    return false;
  }
  if (log_is_enabled(Debug, os, container) && !cg_infos[PIDS_IDX]._data_complete) {
    log_debug(os, container)("Optional cgroup v1 pids subsystem not found");
    // keep the other controller info, pids is optional
  }
  // Cgroups v1 case, we have all the info we need.
  *flags = CGROUPS_V1;
  return true;
};

void CgroupSubsystemFactory::cleanup(CgroupInfo* cg_infos) {
  assert(cg_infos != nullptr, "Invariant");
  for (int i = 0; i < CG_INFO_LENGTH; i++) {
    os::free(cg_infos[i]._name);
    os::free(cg_infos[i]._cgroup_path);
    os::free(cg_infos[i]._root_mount_path);
    os::free(cg_infos[i]._mount_path);
  }
}

/* active_processor_count
 *
 * Calculate an appropriate number of active processors for the
 * VM to use based on these three inputs.
 *
 * cpu affinity
 * cgroup cpu quota & cpu period
 * cgroup cpu shares
 *
 * Algorithm:
 *
 * Determine the number of available CPUs from sched_getaffinity
 *
 * If user specified a quota (quota != -1), calculate the number of
 * required CPUs by dividing quota by period.
 *
 * All results of division are rounded up to the next whole number.
 *
 * If quotas have not been specified, return the
 * number of active processors in the system.
 *
 * If quotas have been specified, the resulting number
 * returned will never exceed the number of active processors.
 *
 * return:
 *    number of CPUs
 */
int CgroupSubsystem::active_processor_count() {
  int quota_count = 0;
  int cpu_count;
  int result;

  // We use a cache with a timeout to avoid performing expensive
  // computations in the event this function is called frequently.
  // [See 8227006].
  CachingCgroupController<CgroupCpuController*>* contrl = cpu_controller();
  CachedMetric* cpu_limit = contrl->metrics_cache();
  if (!cpu_limit->should_check_metric()) {
    int val = (int)cpu_limit->value();
    log_trace(os, container)("CgroupSubsystem::active_processor_count (cached): %d", val);
    return val;
  }

  cpu_count = os::Linux::active_processor_count();
  result = CgroupUtil::processor_count(contrl->controller(), cpu_count);
  // Update cached metric to avoid re-reading container settings too often
  cpu_limit->set_value(result, OSCONTAINER_CACHE_TIMEOUT);

  return result;
}

/* memory_limit_in_bytes
 *
 * Return the limit of available memory for this process.
 *
 * return:
 *    memory limit in bytes or
 *    -1 for unlimited
 *    OSCONTAINER_ERROR for not supported
 */
jlong CgroupSubsystem::memory_limit_in_bytes() {
  CachingCgroupController<CgroupMemoryController*>* contrl = memory_controller();
  CachedMetric* memory_limit = contrl->metrics_cache();
  if (!memory_limit->should_check_metric()) {
    return memory_limit->value();
  }
  jlong phys_mem = os::Linux::physical_memory();
  log_trace(os, container)("total physical memory: " JLONG_FORMAT, phys_mem);
  jlong mem_limit = contrl->controller()->read_memory_limit_in_bytes(phys_mem);
  // Update cached metric to avoid re-reading container settings too often
  memory_limit->set_value(mem_limit, OSCONTAINER_CACHE_TIMEOUT);
  return mem_limit;
}

// CgroupSubsystem implementations

jlong CgroupSubsystem::memory_and_swap_limit_in_bytes() {
  julong phys_mem = os::Linux::physical_memory();
  julong host_swap = os::Linux::host_swap();
  return memory_controller()->controller()->memory_and_swap_limit_in_bytes(phys_mem, host_swap);
}

jlong CgroupSubsystem::memory_and_swap_usage_in_bytes() {
  julong phys_mem = os::Linux::physical_memory();
  julong host_swap = os::Linux::host_swap();
  return memory_controller()->controller()->memory_and_swap_usage_in_bytes(phys_mem, host_swap);
}

jlong CgroupSubsystem::memory_soft_limit_in_bytes() {
  julong phys_mem = os::Linux::physical_memory();
  return memory_controller()->controller()->memory_soft_limit_in_bytes(phys_mem);
}

jlong CgroupSubsystem::memory_usage_in_bytes() {
  return memory_controller()->controller()->memory_usage_in_bytes();
}

jlong CgroupSubsystem::memory_max_usage_in_bytes() {
  return memory_controller()->controller()->memory_max_usage_in_bytes();
}

jlong CgroupSubsystem::rss_usage_in_bytes() {
  return memory_controller()->controller()->rss_usage_in_bytes();
}

jlong CgroupSubsystem::cache_usage_in_bytes() {
  return memory_controller()->controller()->cache_usage_in_bytes();
}

int CgroupSubsystem::cpu_quota() {
  return cpu_controller()->controller()->cpu_quota();
}

int CgroupSubsystem::cpu_period() {
  return cpu_controller()->controller()->cpu_period();
}

int CgroupSubsystem::cpu_shares() {
  return cpu_controller()->controller()->cpu_shares();
}

/*
 * Set directory to subsystem specific files based
 * on the contents of the mountinfo and cgroup files.
 */
void CgroupController::set_subsystem_path(const char *cgroup_path) {
  os::free(_cgroup_path);
  _cgroup_path = os::strdup(cgroup_path);
  trim_path(0);
}

void CgroupController::set_path(const char *cgroup_path) {
  __attribute__((unused)) bool _cgroup_path; // Do not use the member variable.
  stringStream ss;
  if (_root == nullptr || cgroup_path == nullptr) {
    return;
  }
  if (strcmp(_root, "/") == 0) {
    ss.print_raw(_mount_point);
    if (strcmp(cgroup_path, "/") != 0) {
      ss.print_raw(cgroup_path);
    }
    os::free(_path);
    _path = os::strdup(ss.base());
    return;
  }
  if (strcmp(_root, cgroup_path) == 0) {
    os::free(_path);
    _path = os::strdup(_mount_point);
    return;
  }
  if (strlen(cgroup_path) == strlen(_root)) {
    return;
  }
  if (strncmp(cgroup_path, _root, strlen(_root)) != 0 || cgroup_path[strlen(_root)] != '/') {
    return;
  }
  ss.print_raw(_mount_point);
  const char* cg_path_sub = cgroup_path + strlen(_root);
  ss.print_raw(cg_path_sub);
  os::free(_path);
  _path = os::strdup(ss.base());
}

/* trim_path
 *
 * Remove specific dir_count number of trailing _cgroup_path directories
 *
 * return:
 *    whether dir_count was < number of _cgroup_path directories
 *    false is returned if the result would be cgroup root directory
 */
bool CgroupController::trim_path(size_t dir_count) {
  char *cgroup_path = os::strdup(_cgroup_path);
  assert(cgroup_path[0] == '/', "_cgroup_path should start with a slash ('/')");
  while (dir_count--) {
    char *s = strrchr(cgroup_path, '/');
    assert(s, "function should have already returned");
    *s = 0;
    if (s == cgroup_path) {
      os::free(cgroup_path);
      return false;
    }
  }
  set_path(cgroup_path);
  os::free(cgroup_path);
  return true;
}

void CgroupSubsystem::initialize_hierarchy() {
  CgroupController *memory = memory_controller()->controller();

  size_t best_level = 0;
  jlong memory_limit_min = max_jlong;
  jlong memory_swap_limit_min = max_jlong;

  for (size_t dir_count = 0; memory->trim_path(dir_count); ++dir_count) {
    jlong memory_limit = read_memory_limit_in_bytes();
    if (memory_limit != -1 && memory_limit != OSCONTAINER_ERROR && memory_limit < memory_limit_min) {
      memory_limit_min = memory_limit;
      best_level = dir_count;
    }
    jlong memory_swap_limit = memory_and_swap_limit_in_bytes();
    if (memory_swap_limit != -1 && memory_swap_limit != OSCONTAINER_ERROR && memory_swap_limit < memory_swap_limit_min) {
      memory_swap_limit_min = memory_swap_limit;
      best_level = dir_count;
    }
    // Never use a directory without controller files (disabled by "../cgroup.subtree_control").
    if (memory_limit == OSCONTAINER_ERROR && memory_swap_limit == OSCONTAINER_ERROR && best_level == dir_count) {
      ++best_level;
    }
  }

  memory->trim_path(best_level);
}
