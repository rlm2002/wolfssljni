/* VerifyCallback.java
 *
 * Copyright (C) 2006-2025 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

import com.wolfssl.WolfSSLVerifyCallback;

public class VerifyCallback implements WolfSSLVerifyCallback {

    public int verifyCallback(int preverify_ok, long x509StorePtr) {
        System.out.println("Hello from Java verify callback!");
        if (preverify_ok != 1) {
            System.out.println("peer cert verification failed");
            return 0;
        } else {
            /* wolfSSL verification passed successfully, continue */
            return 1;
        }
    }
}

