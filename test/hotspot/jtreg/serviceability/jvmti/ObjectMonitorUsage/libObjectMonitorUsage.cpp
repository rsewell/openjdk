/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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
 */

#include <stdio.h>
#include <string.h>
#include "jvmti.h"
#include "jvmti_common.hpp"

extern "C" {

#define PASSED 0
#define STATUS_FAILED 2

static jvmtiEnv *jvmti = nullptr;
static jrawMonitorID event_lock = nullptr;
static jint result = PASSED;
static int check_idx = 0;
static int waits_to_enter = 0;
static int waits_to_be_notified = 0;
static jobject tested_monitor = nullptr;

static bool is_tested_monitor(JNIEnv *jni, jobject monitor) {
  if (tested_monitor == nullptr) {
    return false; // tested_monitor was not set yet
  }
  return jni->IsSameObject(monitor, tested_monitor) == JNI_TRUE;
}

static void log_event(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread,
                      const char* title, int counter) {
  char* tname = get_thread_name(jvmti, jni, thread);
  LOG(">>> %s event: %s counter: %d\n", title, tname, counter);
  deallocate(jvmti, jni, (void*)tname);
}

JNIEXPORT void JNICALL
MonitorContendedEnter(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread, jobject monitor) {
  RawMonitorLocker rml(jvmti, jni, event_lock);
  if (is_tested_monitor(jni, monitor)) {
    waits_to_enter++;
    log_event(jvmti, jni, thread, "MonitorContendedEnter", waits_to_enter);
  }
}

JNIEXPORT void JNICALL
MonitorContendedEntered(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread, jobject monitor) {
  RawMonitorLocker rml(jvmti, jni, event_lock);
  if (is_tested_monitor(jni, monitor)) {
    waits_to_enter--;
    log_event(jvmti, jni, thread, "MonitorContendedEntered", waits_to_enter);
  }
}

JNIEXPORT void JNICALL
MonitorWait(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread, jobject monitor, jlong timeout) {
  RawMonitorLocker rml(jvmti, jni, event_lock);
  if (is_tested_monitor(jni, monitor)) {
    waits_to_be_notified++;
    log_event(jvmti, jni, thread, "MonitorWait", waits_to_be_notified);
  }
}

JNIEXPORT void JNICALL
MonitorWaited(jvmtiEnv *jvmti, JNIEnv *jni, jthread thread, jobject monitor, jboolean timed_out) {
  RawMonitorLocker rml(jvmti, jni, event_lock);
  if (is_tested_monitor(jni, monitor)) {
    waits_to_be_notified--;
    log_event(jvmti, jni, thread, "MonitorWaited", waits_to_be_notified);
  }
}

jint Agent_Initialize(JavaVM *jvm, char *options, void *reserved) {
  jint res;
  jvmtiError err;
  jvmtiCapabilities caps;
  jvmtiEventCallbacks callbacks;

  res = jvm->GetEnv((void **) &jvmti, JVMTI_VERSION_1_1);
  if (res != JNI_OK || jvmti == nullptr) {
    LOG("Wrong result of a valid call to GetEnv !\n");
    return JNI_ERR;
  }
  err = jvmti->GetPotentialCapabilities(&caps);
  check_jvmti_error(err, "Agent_Initialize: error in JVMTI GetPotentialCapabilities");

  err = jvmti->AddCapabilities(&caps);
  check_jvmti_error(err, "Agent_Initialize: error in JVMTI AddCapabilities");

  err = jvmti->GetCapabilities(&caps);
  check_jvmti_error(err, "Agent_Initialize: error in JVMTI GetCapabilities");

  if (!caps.can_get_monitor_info) {
    LOG("Warning: GetObjectMonitorUsage is not implemented\n");
  }
  if (!caps.can_generate_monitor_events) {
    LOG("Warning: Monitor events are not implemented\n");
    return JNI_ERR;
  }
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.MonitorContendedEnter   = &MonitorContendedEnter;
  callbacks.MonitorContendedEntered = &MonitorContendedEntered;
  callbacks.MonitorWait = &MonitorWait;
  callbacks.MonitorWaited = &MonitorWaited;

  err = jvmti->SetEventCallbacks(&callbacks, sizeof(jvmtiEventCallbacks));
  check_jvmti_error(err, "Agent_Initialize: error in JVMTI SetEventCallbacks");

  event_lock = create_raw_monitor(jvmti, "Events Monitor");

  return JNI_OK;
}

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
  return Agent_Initialize(jvm, options, reserved);
}

JNIEXPORT jint JNICALL
Agent_OnAttach(JavaVM *jvm, char *options, void *reserved) {
  return Agent_Initialize(jvm, options, reserved);
}

static void print_monitor_info(JNIEnv *jni, jvmtiMonitorUsage &inf) {
  jvmtiError err;
  jvmtiThreadInfo tinf;

  LOG(">>> [%d]\n", check_idx);
  if (inf.owner == nullptr) {
    LOG(">>>          owner:               none (0x0)\n");
  } else {
    err = jvmti->GetThreadInfo(inf.owner, &tinf);
    check_jvmti_status(jni, err, "error in JVMTI GetThreadInfo");
    LOG(">>>          owner:               %s (0x%p)\n",
        tinf.name, inf.owner);
    deallocate(jvmti, jni, tinf.name);
  }
  LOG(">>>          entry_count:         %d\n", inf.entry_count);
  LOG(">>>          waiter_count:        %d\n", inf.waiter_count);
  LOG(">>>          notify_waiter_count: %d\n", inf.notify_waiter_count);

  if (inf.waiter_count > 0) {
    LOG(">>>  waiters:\n");
    for (int j = 0; j < inf.waiter_count; j++) {
      err = jvmti->GetThreadInfo(inf.waiters[j], &tinf);
      check_jvmti_status(jni, err, "error in JVMTI GetThreadInfo");
      LOG(">>>                %2d: %s (0x%p)\n",
          j, tinf.name, inf.waiters[j]);
      deallocate(jvmti, jni, tinf.name);
    }
  }
  if (inf.notify_waiter_count > 0) {
    LOG(">>>  notify_waiters:\n");
    for (int j = 0; j < inf.notify_waiter_count; j++) {
      err = jvmti->GetThreadInfo(inf.notify_waiters[j], &tinf);
      check_jvmti_status(jni, err, "error in JVMTI GetThreadInfo");
      LOG(">>>                %2d: %s (0x%p)\n",
          j, tinf.name, inf.notify_waiters[j]);
      deallocate(jvmti, jni, tinf.name);
    }
  }
}

JNIEXPORT void JNICALL
Java_ObjectMonitorUsage_check(JNIEnv *jni, jclass cls, jobject obj, jthread owner,
        jint entryCount, jint waiterCount, jint notifyWaiterCount) {
  jvmtiError err;
  jvmtiMonitorUsage inf;

  check_idx++;

  err = jvmti->GetObjectMonitorUsage(obj, &inf);
  check_jvmti_status(jni, err, "error in JVMTI GetObjectMonitorUsage");

  print_monitor_info(jni, inf);

  if (!jni->IsSameObject(owner, inf.owner)) {
    LOG("FAILED: (%d) unexpected owner: 0x%p\n", check_idx, inf.owner);
    result = STATUS_FAILED;
  }
  if (inf.entry_count != entryCount) {
    LOG("FAILED: (%d) entry_count expected: %d, actually: %d\n",
        check_idx, entryCount, inf.entry_count);
    result = STATUS_FAILED;
  }
  if (inf.waiter_count != waiterCount) {
    LOG("FAILED: (%d) waiter_count expected: %d, actually: %d\n",
        check_idx, waiterCount, inf.waiter_count);
    result = STATUS_FAILED;
  }
  if (inf.notify_waiter_count != notifyWaiterCount) {
    LOG("FAILED: (%d) notify_waiter_count expected: %d, actually: %d\n",
        check_idx, notifyWaiterCount, inf.notify_waiter_count);
    result = STATUS_FAILED;
  }
}

JNIEXPORT void JNICALL
Java_ObjectMonitorUsage_setTestedMonitor(JNIEnv *jni, jclass cls, jobject monitor) {
  jvmtiError err;
  jvmtiEventMode event_mode = (monitor != nullptr) ? JVMTI_ENABLE : JVMTI_DISABLE;

  RawMonitorLocker rml(jvmti, jni, event_lock);

  if (tested_monitor != nullptr) {
    jni->DeleteGlobalRef(tested_monitor);
  }
  tested_monitor = (monitor != nullptr) ? jni->NewGlobalRef(monitor) : nullptr;
  waits_to_enter = 0;
  waits_to_be_notified = 0;

  err = jvmti->SetEventNotificationMode(event_mode, JVMTI_EVENT_MONITOR_CONTENDED_ENTER, nullptr);
  check_jvmti_status(jni, err, "setTestedMonitor: error in JVMTI SetEventNotificationMode #1");

  err = jvmti->SetEventNotificationMode(event_mode, JVMTI_EVENT_MONITOR_CONTENDED_ENTERED, nullptr);
  check_jvmti_status(jni, err, "setTestedMonitor: error in JVMTI SetEventNotificationMode #2");

  err = jvmti->SetEventNotificationMode(event_mode, JVMTI_EVENT_MONITOR_WAIT, nullptr);
  check_jvmti_status(jni, err, "setTestedMonitor: error in JVMTI SetEventNotificationMode #3");

  err = jvmti->SetEventNotificationMode(event_mode, JVMTI_EVENT_MONITOR_WAITED, nullptr);
  check_jvmti_status(jni, err, "setTestedMonitor: error in JVMTI SetEventNotificationMode #4");
}

JNIEXPORT jint JNICALL
Java_ObjectMonitorUsage_waitsToEnter(JNIEnv *jni, jclass cls) {
  RawMonitorLocker rml(jvmti, jni, event_lock);
  return waits_to_enter;
}

JNIEXPORT jint JNICALL
Java_ObjectMonitorUsage_waitsToBeNotified(JNIEnv *jni, jclass cls) {
  RawMonitorLocker rml(jvmti, jni, event_lock);
  return waits_to_be_notified;
}

JNIEXPORT jint JNICALL
Java_ObjectMonitorUsage_getRes(JNIEnv *jni, jclass cls) {
  return result;
}

} // extern "C"
