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

/* @test
 * @summary Basic test for Monotonic.
 * @run junit Basic
 */

import org.junit.jupiter.api.Test;

import java.lang.invoke.MethodHandle;
import java.util.NoSuchElementException;
import java.util.function.Supplier;

import static org.junit.jupiter.api.Assertions.*;

final class Basic {

    @Test
    void testIntegerOld() {
        Monotonic<Integer> m = Monotonic.of(Integer.class);
        assertFalse(m.isBound());
        assertThrows(NoSuchElementException.class, m::get);

        m.bind(42);
        assertTrue(m.isBound());
        assertEquals(42, m.get());
        assertThrows(IllegalStateException.class, () -> m.bind(13));
        assertTrue(m.isBound());
        assertEquals(42, m.get());

        MethodHandle handle = m.getter();
        assertEquals(Object.class, handle.type().returnType());
        assertEquals(1, handle.type().parameterCount());
        assertEquals(Monotonic.class, handle.type().parameterType(0));
        try {
            Integer i = (Integer) handle.invoke(m);
            assertEquals(42, i);
        } catch (Throwable t) {
            fail(t);
        }
    }

    @Test
    void testIntOld() {
        Monotonic<Integer> m = Monotonic.of(int.class);
        assertFalse(m.isBound());
        assertThrows(NoSuchElementException.class, m::get);

        m.bind(42);
        assertTrue(m.isBound());
        assertEquals(42, m.get());
        assertThrows(IllegalStateException.class, () -> m.bind(13));
        assertTrue(m.isBound());
        assertEquals(42, m.get());
        Supplier<Integer> throwingSupplier = () -> {
            throw new UnsupportedOperationException();
        };
        assertDoesNotThrow(() -> m.computeIfUnbound(throwingSupplier));

        MethodHandle handle = m.getter();
        assertEquals(int.class, handle.type().returnType());
        assertEquals(1, handle.type().parameterCount());

        assertEquals(Monotonic.class, handle.type().parameterType(0));
        try {
            Integer i = (int) handle.invoke(m);
            assertEquals(42, i);
        } catch (Throwable t) {
            fail(t);
        }
    }

    @Test
    void testString() {
        testMonotonic(String.class, "A", "B", monotonic -> (String) (Object) monotonic.getter().invokeExact(monotonic));
    }

    @Test
    void testInteger() {
        testMonotonic(Integer.class, 42, 13, monotonic -> (Integer) (Object) monotonic.getter().invokeExact(monotonic));
    }

    @Test
    void testInt() {
        testMonotonic(int.class, 42, 13, monotonic -> (int) monotonic.getter().invokeExact(monotonic));
    }

    @Test
    void testLong() {
        testMonotonic(long.class, 42L, 13L, monotonic -> (long) monotonic.getter().invokeExact(monotonic));
    }

    interface MethodHandleInvoker<T> {
        T apply(Monotonic<T> monotonic) throws Throwable;
    }

    <T> void testMonotonic(Class<T> type,
                           T first,
                           T second,
                           MethodHandleInvoker<T> invoker) {

        // unbound
        Monotonic<T> m = Monotonic.of(type);
        assertFalse(m.isBound());
        assertThrows(NoSuchElementException.class, m::get);

        // bind()
        m.bind(first);
        assertTrue(m.isBound());
        assertEquals(first, m.get());
        assertThrows(IllegalStateException.class, () -> m.bind(second));
        assertTrue(m.isBound());
        assertEquals(first, m.get());

        // getter()
        MethodHandle handle = m.getter();
        if (type.isPrimitive()) {
            assertEquals(type, handle.type().returnType());
        } else {
            assertEquals(Object.class, handle.type().returnType());
        }
        assertEquals(1, handle.type().parameterCount());

        assertEquals(Monotonic.class, handle.type().parameterType(0));
        try {
            T t = invoker.apply(m);
            assertEquals(first, t);
        } catch (Throwable t) {
            fail(t);
        }

        // computeIfUnbound()
        Supplier<T> throwingSupplier = () -> {
            throw new UnsupportedOperationException();
        };
        assertDoesNotThrow(() -> m.computeIfUnbound(throwingSupplier));

        var m2 = Monotonic.of(type);
        m2.computeIfUnbound(() -> first);
        assertEquals(first, m2.get());

        // asMemoized()
        Supplier<T> oneTimeSupplier = new Supplier<T>() {
            int cnt = 0;
            @Override
            public T get() {
                if (cnt++ != 0) {
                    throw new IllegalStateException();
                }
                return first;
            }
        };
        var m3 = Monotonic.of(type);
        Supplier<T> memoized = m3.asMemoized(oneTimeSupplier);
        assertEquals(first, memoized.get());
        // Make sure the original supplier is not invoked
        assertEquals(first, memoized.get());
    }

}
