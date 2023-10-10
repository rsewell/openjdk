/*
 * Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
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
package org.openjdk.bench.javax.crypto.full;

import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Warmup;
import org.openjdk.jmh.annotations.Benchmark;

// import javax.crypto.BadPaddingException;
// import javax.crypto.Cipher;
// import javax.crypto.IllegalBlockSizeException;
// import javax.crypto.NoSuchPaddingException;

import java.math.BigInteger;
// import java.security.NoSuchAlgorithmException;
// import java.security.Provider;
// import java.security.SecureRandom;
// import java.security.Security;
// import java.util.Random;
import java.util.concurrent.TimeUnit;

import sun.security.util.math.intpoly.MontgomeryIntegerPolynomialP256;
import sun.security.util.math.intpoly.IntegerPolynomialP256;
import sun.security.util.math.MutableIntegerModuloP;
import sun.security.util.math.ImmutableIntegerModuloP;

@Fork(jvmArgsAppend = {"-XX:+AlwaysPreTouch", 
    "--add-exports", "java.base/sun.security.util.math.intpoly=ALL-UNNAMED", 
    "--add-exports", "java.base/sun.security.util.math=ALL-UNNAMED"}, value = 1)
@Warmup(iterations = 3, time = 3)
@Measurement(iterations = 8, time = 2)
@OutputTimeUnit(TimeUnit.SECONDS)
@State(Scope.Thread)
@BenchmarkMode(Mode.Throughput)
public class PolynomialP256Bench {

    final MontgomeryIntegerPolynomialP256 montField = MontgomeryIntegerPolynomialP256.ONE;
    final IntegerPolynomialP256 residueField = IntegerPolynomialP256.ONE;
    final BigInteger refx = new BigInteger("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296", 16);
    final ImmutableIntegerModuloP x = residueField.getElement(refx);
    final ImmutableIntegerModuloP X = montField.getElement(refx);

    @Param({"true", "false"})
    private boolean isMontBench;

    @Benchmark
    public MutableIntegerModuloP benchMultiply() {
        MutableIntegerModuloP test;
        if (isMontBench) {
            test = X.mutable();
        } else {
            test = x.mutable();
        }

        for (int i = 0; i< 10000; i++) {
            test = test.setProduct(test);
        }
        return test;
    }

    @Benchmark
    public MutableIntegerModuloP benchSquare() {
        MutableIntegerModuloP test;
        if (isMontBench) {
            test = X.mutable();
        } else {
            test = x.mutable();
        }

        for (int i = 0; i< 10000; i++) {
            test = test.setSquare();
        }
        return test;
    }

    @Benchmark
    public long[] benchAssign() {
        long[] a = new long[]{123,223,323,423,523};
        long[] b = new long[]{512345,612345,712345,812345,912345};
        for (int i = 0; i< 10000; i++) {
            MontgomeryIntegerPolynomialP256.ONE.assignTest(0,a,b);
            MontgomeryIntegerPolynomialP256.ONE.assignTest(1,a,b);
            MontgomeryIntegerPolynomialP256.ONE.assignTest(0,b,a);
            MontgomeryIntegerPolynomialP256.ONE.assignTest(1,b,a);
        }
        return a;
    }

    @Benchmark
    public long[] benchReduce() {
        long[] a = new long[]{123,223,323,423,523};
        long[] b = new long[]{512345,612345,712345,812345,912345};
        long[] c = new long[]{0x0000ffffffffffffL,0x0000ffffffffffffL,0x0000ffffffffffffL,0x0000ffffffffffffL, 0x00ffffffffffffffL};
        for (int i = 0; i< 10000; i++) {
            MontgomeryIntegerPolynomialP256.ONE.reduceTest(a);
            MontgomeryIntegerPolynomialP256.ONE.reduceTest(b);
            MontgomeryIntegerPolynomialP256.ONE.reduceTest(c);
        }
        return a;
    }
}
