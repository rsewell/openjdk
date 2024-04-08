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

package org.openjdk.bench.java.lang.lazy;

import org.openjdk.jmh.annotations.*;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.function.IntFunction;
import java.util.stream.Stream;

/**
 * Benchmark measuring lazy list performance
 */
@BenchmarkMode(Mode.AverageTime)
@OutputTimeUnit(TimeUnit.NANOSECONDS)
@State(Scope.Thread)
@Warmup(iterations = 5, time = 1)
@Measurement(iterations = 5, time = 1)
@Fork(value = 2, jvmArgsAppend = "--enable-preview")
/* 2024-04-02
Benchmark                                 Mode  Cnt  Score   Error  Units
MonotonicListBenchmark.instanceArrayList  avgt   10  1.033 ? 0.042  ns/op
MonotonicListBenchmark.instanceLazyList   avgt   10  1.077 ? 0.042  ns/op
MonotonicListBenchmark.instanceWrapped    avgt   10  1.325 ? 0.047  ns/op <- Stored
MonotonicListBenchmark.staticArrayList    avgt   10  0.922 ? 0.058  ns/op
MonotonicListBenchmark.staticLazyList     avgt   10  0.568 ? 0.046  ns/op

2024-04-08
Benchmark                            Mode  Cnt  Score   Error  Units
LazyListBenchmark.instanceArrayList  avgt   10  1.130 ? 0.021  ns/op
LazyListBenchmark.instanceDelegated  avgt   10  1.402 ? 0.012  ns/op
LazyListBenchmark.instanceLazyList   avgt   10  1.227 ? 0.042  ns/op
LazyListBenchmark.instanceStored     avgt   10  1.512 ? 0.014  ns/op

LazyListBenchmark.staticArrayList    avgt   10  0.979 ? 0.007  ns/op
LazyListBenchmark.staticDelegated    avgt   10  0.557 ? 0.001  ns/op
LazyListBenchmark.staticLazyList     avgt   10  0.573 ? 0.079  ns/op
LazyListBenchmark.staticStored       avgt   10  0.556 ? 0.001  ns/op


 */
public class LazyListBenchmark {

    private static final IntFunction<Integer> FUNCTION = i -> i;
    private static final int SIZE = 100;

    private static final List<Lazy<Integer>> STORED = Stream.generate(Lazy::<Integer>of)
            .limit(SIZE)
            .toList();
    private static final List<Lazy<Integer>> DELEGATED_LIST = Lazy.ofWrappedList(SIZE);

    static {
        initLazy(STORED);
        initLazy(DELEGATED_LIST);
    }

    //private static final List<Monotonic<Integer>> MONOTONIC_LIST = initMono(Monotonic.ofList(SIZE));
    private static final List<Integer> ARRAY_LIST = initList(new ArrayList<>(SIZE));
    private static final List<Integer> LAZY_LIST = Lazy.ofList(SIZE, FUNCTION);

    //private final List<Monotonic<Integer>> referenceList = initMono(Monotonic.ofList(SIZE));
    private final List<Integer> arrayList = initList(new ArrayList<>(SIZE));
    private final List<Integer> lazyList = Lazy.ofList(SIZE, FUNCTION);
    private final List<Lazy<Integer>> storedList;
    private final List<Lazy<Integer>> delegatedList;


    public LazyListBenchmark() {
        this.storedList = Stream.generate(Lazy::<Integer>of)
                .limit(SIZE)
                .toList();
        initLazy(storedList);
        delegatedList = Lazy.ofWrappedList(SIZE);
        initLazy(delegatedList);
    }

    @Setup
    public void setup() {
    }

    @Benchmark
    public int instanceArrayList() {
        return arrayList.get(8);
    }

    @Benchmark
    public int instanceLazyList() {
        return lazyList.get(8);
    }

    @Benchmark
    public int instanceStored() {
        return storedList.get(8).orThrow();
    }

    @Benchmark
    public int instanceDelegated() {
        return delegatedList.get(8).orThrow();
    }

    @Benchmark
    public int staticArrayList() {
        return ARRAY_LIST.get(8);
    }

    @Benchmark
    public int staticLazyList() {
        return LAZY_LIST.get(8);
    }

    @Benchmark
    public int staticStored() {
        return STORED.get(8).orThrow();
    }

    @Benchmark
    public int staticDelegated() {
        return DELEGATED_LIST.get(8).orThrow();
    }

    private static void initLazy(List<Lazy<Integer>> list) {
        int index = 8;
        list.get(index).setOrThrow(FUNCTION.apply(index));
    }

    private static List<Integer> initList(List<Integer> list) {
        for (int i = 0; i < 9; i++) {
            list.add(FUNCTION.apply(i));
        }
        return list;
    }

}
