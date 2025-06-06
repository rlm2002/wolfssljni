/* com_wolfssl_WolfSSL.c
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

#include <stdio.h>
#include <stdint.h>
#ifdef WOLFSSL_USER_SETTINGS
    #include <wolfssl/wolfcrypt/settings.h>
#else
    #include <wolfssl/options.h>
#endif
#include <wolfssl/ssl.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfcrypt/logging.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/random.h>
#ifdef HAVE_FIPS
    #include <wolfssl/wolfcrypt/fips_test.h>
#endif
#ifndef USE_WINDOWS_API
    #include <sys/errno.h>
#endif

#include "com_wolfssl_globals.h"
#include "com_wolfssl_WolfSSL.h"

#ifdef _MSC_VER
    /* 4996 warning to use MS extensions e.g., strncat_s instead of XSTRNCAT */
    #pragma warning(disable: 4996)
#endif

/* global JavaVM reference for JNIEnv lookup */
JavaVM*  g_vm;

/* global object refs for logging callbacks */
static jobject g_loggingCbIfaceObj;

/* global method IDs we can cache for performance */
jmethodID g_sslIORecvMethodId = NULL;
jmethodID g_sslIORecvMethodId_BB = NULL;
jmethodID g_sslIOSendMethodId = NULL;
jmethodID g_sslIOSendMethodId_BB = NULL;
jmethodID g_isArrayIORecvCallbackSet = NULL;
jmethodID g_isArrayIOSendCallbackSet = NULL;
jmethodID g_isByteBufferIORecvCallbackSet = NULL;
jmethodID g_isByteBufferIOSendCallbackSet = NULL;
jmethodID g_bufferPositionMethodId = NULL;
jmethodID g_bufferLimitMethodId = NULL;
jmethodID g_bufferHasArrayMethodId = NULL;
jmethodID g_bufferArrayMethodId = NULL;
jmethodID g_bufferSetPositionMethodId = NULL;
jmethodID g_verifyCallbackMethodId = NULL;

#ifdef HAVE_FIPS
/* global object ref for FIPS error callback */
static jobject g_fipsCbIfaceObj;
#endif

/* custom native fn prototypes */
void NativeLoggingCallback(const int logLevel, const char *const logMessage);

/* Called when native library is loaded.
 * We also cache global jmethodIDs here for performance. */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    jclass sslClass = NULL;
    jclass byteBufferClass = NULL;
    jclass verifyClass = NULL;
    (void)reserved;

    /* store JavaVM */
    g_vm = vm;

    /* get JNIEnv from JavaVM */
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        printf("Unable to get JNIEnv from JavaVM in JNI_OnLoad()\n");
        return JNI_ERR;
    }

    /* Cache the method ID for IO send and recv callbacks */
    sslClass = (*env)->FindClass(env, "com/wolfssl/WolfSSLSession");
    if (sslClass == NULL) {
        return JNI_ERR;
    }

    g_sslIORecvMethodId = (*env)->GetMethodID(env, sslClass,
        "internalIOSSLRecvCallback",
        "(Lcom/wolfssl/WolfSSLSession;[BI)I");
    if (g_sslIORecvMethodId == NULL) {
        return JNI_ERR;
    }

    g_sslIORecvMethodId_BB = (*env)->GetMethodID(env, sslClass,
        "internalIOSSLRecvCallback",
        "(Lcom/wolfssl/WolfSSLSession;Ljava/nio/ByteBuffer;I)I");
    if (g_sslIORecvMethodId_BB == NULL) {
        return JNI_ERR;
    }

    g_sslIOSendMethodId = (*env)->GetMethodID(env, sslClass,
        "internalIOSSLSendCallback",
        "(Lcom/wolfssl/WolfSSLSession;[BI)I");
    if (g_sslIOSendMethodId == NULL) {
        return JNI_ERR;
    }

    g_sslIOSendMethodId_BB = (*env)->GetMethodID(env, sslClass,
        "internalIOSSLSendCallback",
        "(Lcom/wolfssl/WolfSSLSession;Ljava/nio/ByteBuffer;I)I");
    if (g_sslIOSendMethodId_BB == NULL) {
        return JNI_ERR;
    }

    g_isArrayIORecvCallbackSet = (*env)->GetMethodID(env, sslClass,
        "isArrayIORecvCallbackSet", "()Z");
    if (g_isArrayIORecvCallbackSet == NULL) {
        return JNI_ERR;
    }

    g_isArrayIOSendCallbackSet = (*env)->GetMethodID(env, sslClass,
        "isArrayIOSendCallbackSet", "()Z");
    if (g_isArrayIOSendCallbackSet == NULL) {
        return JNI_ERR;
    }

    g_isByteBufferIORecvCallbackSet = (*env)->GetMethodID(env, sslClass,
        "isByteBufferIORecvCallbackSet", "()Z");
    if (g_isByteBufferIORecvCallbackSet == NULL) {
        return JNI_ERR;
    }

    g_isByteBufferIOSendCallbackSet = (*env)->GetMethodID(env, sslClass,
        "isByteBufferIOSendCallbackSet", "()Z");
    if (g_isByteBufferIOSendCallbackSet == NULL) {
        return JNI_ERR;
    }

    /* Cache ByteBuffer method IDs */
    byteBufferClass = (*env)->FindClass(env, "java/nio/ByteBuffer");
    if (byteBufferClass == NULL) {
        return JNI_ERR;
    }

    g_bufferPositionMethodId = (*env)->GetMethodID(env, byteBufferClass,
        "position", "()I");
    if (g_bufferPositionMethodId == NULL) {
        return JNI_ERR;
    }

    g_bufferLimitMethodId = (*env)->GetMethodID(env, byteBufferClass,
        "limit", "()I");
    if (g_bufferLimitMethodId == NULL) {
        return JNI_ERR;
    }

    g_bufferHasArrayMethodId = (*env)->GetMethodID(env, byteBufferClass,
        "hasArray", "()Z");
    if (g_bufferHasArrayMethodId == NULL) {
        return JNI_ERR;
    }

    g_bufferArrayMethodId = (*env)->GetMethodID(env, byteBufferClass,
        "array", "()[B");
    if (g_bufferArrayMethodId == NULL) {
        return JNI_ERR;
    }

    g_bufferSetPositionMethodId = (*env)->GetMethodID(env, byteBufferClass,
        "position", "(I)Ljava/nio/Buffer;");
    if (g_bufferSetPositionMethodId == NULL) {
        return JNI_ERR;
    }

    /* Cache verify callback method ID */
    verifyClass = (*env)->FindClass(env, "com/wolfssl/WolfSSLVerifyCallback");
    if (verifyClass == NULL) {
        return JNI_ERR;
    }

    g_verifyCallbackMethodId = (*env)->GetMethodID(env, verifyClass,
        "verifyCallback", "(IJ)I");
    if (g_verifyCallbackMethodId == NULL) {
        return JNI_ERR;
    }

    /* Clean up local reference to class, not needed */
    (*env)->DeleteLocalRef(env, sslClass);
    (*env)->DeleteLocalRef(env, byteBufferClass);
    (*env)->DeleteLocalRef(env, verifyClass);

    return JNI_VERSION_1_6;
}

/* Called when native library is unloaded.
 * We clear cached method IDs here. */
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved)
{
    JNIEnv* env;

    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return;
    }

    /* Clear cached method ID */
    g_sslIORecvMethodId = NULL;
    g_sslIORecvMethodId_BB = NULL;
    g_sslIOSendMethodId = NULL;
    g_sslIOSendMethodId_BB = NULL;
    g_isArrayIORecvCallbackSet = NULL;
    g_isArrayIOSendCallbackSet = NULL;
    g_isByteBufferIORecvCallbackSet = NULL;
    g_isByteBufferIOSendCallbackSet = NULL;
    g_bufferPositionMethodId = NULL;
    g_bufferLimitMethodId = NULL;
    g_bufferHasArrayMethodId = NULL;
    g_bufferArrayMethodId = NULL;
    g_bufferSetPositionMethodId = NULL;
    g_verifyCallbackMethodId = NULL;
}

/**
 * Throw WolfSSLJNIException
 */
void throwWolfSSLJNIException(JNIEnv* jenv, const char* msg)
{
    jclass excClass = (*jenv)->FindClass(jenv, "com/wolfssl/WolfSSLJNIException");
    if (excClass == NULL) {
        /* Unable to find exception class, give up trying to throw */
        return;
    }
    (*jenv)->ThrowNew(jenv, excClass, msg);
}

/**
 * Throw WolfSSLException
 */
void throwWolfSSLException(JNIEnv* jenv, const char* msg)
{
    jclass excClass = (*jenv)->FindClass(jenv, "com/wolfssl/WolfSSLException");
    if (excClass == NULL) {
        /* Unable to find exception class, give up trying to throw */
        return;
    }
    (*jenv)->ThrowNew(jenv, excClass, msg);
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_init
  (JNIEnv* jenv, jobject jcl)
{
    int ret = 0;

    (void)jenv;
    (void)jcl;

#ifdef WC_RNG_SEED_CB
    ret = wc_SetSeed_Cb(wc_GenerateSeed);
    if (ret != 0) {
        printf("wc_SetSeed_Cb() failed");
    }
#endif

#if defined(HAVE_FIPS) && defined(HAVE_FIPS_VERSION) && \
    (HAVE_FIPS_VERSION >= 6)

    ret = wc_RunAllCast_fips();
    if (ret != 0) {
        printf("FIPS CASTs failed to run");
    }

#elif defined(HAVE_FIPS) && defined(HAVE_FIPS_VERSION) && \
    (HAVE_FIPS_VERSION == 5)

    /* run FIPS 140-3 conditional algorithm self tests early to prevent
     * multi threaded issues later on */
#if !defined(NO_AES) && !defined(NO_AES_CBC)
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_AES_CBC);
        if (ret != 0) {
            printf("AES-CBC CAST failed");
        }
    }
#endif
#ifdef HAVE_AESGCM
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_AES_GCM);
        if (ret != 0) {
            printf("AES-GCM CAST failed");
        }
    }
#endif
#ifndef NO_SHA
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_HMAC_SHA1);
        if (ret != 0) {
            printf("HMAC-SHA1 CAST failed");
        }
    }
#endif
    /* the only non-optional CAST */
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_HMAC_SHA2_256);
        if (ret != 0) {
            printf("HMAC-SHA2-256 CAST failed");
        }
    }
#ifdef WOLFSSL_SHA512
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_HMAC_SHA2_512);
        if (ret != 0) {
            printf("HMAC-SHA2-512 CAST failed");
        }
    }
#endif
#ifdef WOLFSSL_SHA3
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_HMAC_SHA3_256);
        if (ret != 0) {
            printf("HMAC-SHA3-256 CAST failed");
        }
    }
#endif
#ifdef HAVE_HASHDRBG
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_DRBG);
        if (ret != 0) {
            printf("Hash_DRBG CAST failed");
        }
    }
#endif
#ifndef NO_RSA
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_RSA_SIGN_PKCS1v15);
        if (ret != 0) {
            printf("RSA sign CAST failed");
        }
    }
#endif
#if defined(HAVE_ECC_CDH) && defined(HAVE_ECC_CDH_CAST)
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_ECC_CDH);
        if (ret != 0) {
            printf("ECC CDH CAST failed");
        }
    }
#endif
#ifdef HAVE_ECC_DHE
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_ECC_PRIMITIVE_Z);
        if (ret != 0) {
            printf("ECC Primitive Z CAST failed");
        }
    }
#endif
#ifdef HAVE_ECC
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_ECDSA);
        if (ret != 0) {
            printf("ECDSA CAST failed");
        }
    }
#endif
#ifndef NO_DH
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_DH_PRIMITIVE_Z);
        if (ret != 0) {
            printf("DH Primitive Z CAST failed");
        }
    }
#endif
#ifdef WOLFSSL_HAVE_PRF
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_KDF_TLS12);
        if (ret != 0) {
            printf("KDF TLSv1.2 CAST failed");
        }
    }
#endif
#if defined(WOLFSSL_HAVE_PRF) && defined(WOLFSSL_TLS13)
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_KDF_TLS13);
        if (ret != 0) {
            printf("KDF TLSv1.3 CAST failed");
        }
    }
#endif
#ifdef WOLFSSL_WOLFSSH
    if (ret == 0) {
        ret = wc_RunCast_fips(FIPS_CAST_KDF_SSH);
        if (ret != 0) {
            printf("KDF SSHv2.0 CAST failed");
        }
    }
#endif
#endif /* HAVE_FIPS && HAVE_FIPS_VERSION == 5 */

    if (ret == 0) {
        return (jint)wolfSSL_Init();
    } else {
        return (jint)WOLFSSL_FAILURE;
    }
}

/* used in unit tests */
JNIEXPORT void JNICALL Java_com_wolfssl_WolfSSL_nativeFree
  (JNIEnv* jenv, jobject jcl, jlong jptr)
{
    void* ptr = NULL;

    (void)jenv;
    (void)jcl;

    ptr = (void*)(uintptr_t)jptr;
    if(ptr != NULL) {
        XFREE(ptr, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    }
}

/* Functions to get native NID enum values. These must be dynamically
 * obtained since the native wolfSSL values can change depending on
 * wolfSSL configuration. */
JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1surname
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_surname;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1serialNumber
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_serialNumber;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1pkcs9_1unstructuredName
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_pkcs9_unstructuredName;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1pkcs9_1contentType
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_pkcs9_contentType;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1pkcs9_1challengePassword
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_pkcs9_challengePassword;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1givenName
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_givenName;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1initials
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_initials;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1key_1usage
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_key_usage;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1subject_1alt_1name
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_subject_alt_name;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1basic_1constraints
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_basic_constraints;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1ext_1key_1usage
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_ext_key_usage;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getNID_1dnQualifier
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return NID_dnQualifier;
}

/* functions to return BulkCipherAlgorithm enum values from ./wolfssl/ssl.h  */
JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumNULL
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_cipher_null;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumRC4
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_rc4;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumRC2
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_rc2;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumDES
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_des;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnum3DES
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_triple_des;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumDES40
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_des40;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumAES
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_aes;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumAESGCM
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_aes_gcm;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumAESCCM
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_aes_ccm;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumCHACHA
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_chacha;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getBulkCipherAlgorithmEnumCAMELLIA
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfssl_camellia;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacEnumMD5
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return WC_HASH_TYPE_MD5;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacEnumSHA1
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return WC_HASH_TYPE_SHA;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacEnumSHA256
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return WC_HASH_TYPE_SHA256;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacEnumSHA384
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return WC_HASH_TYPE_SHA384;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacEnumSHA512
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return WC_HASH_TYPE_SHA512;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getKeyTypeEnumDSA
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return DSAk;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getKeyTypeEnumRSA
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return RSAk;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getKeyTypeEnumECDSA
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return ECDSAk;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getKeyTypeEnumED25519
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return ED25519k;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1CLIENT_1EARLY_1TRAFFIC_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return CLIENT_EARLY_TRAFFIC_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1CLIENT_1HANDSHAKE_1TRAFFIC_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return CLIENT_HANDSHAKE_TRAFFIC_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1SERVER_1HANDSHAKE_1TRAFFIC_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return SERVER_HANDSHAKE_TRAFFIC_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1CLIENT_1TRAFFIC_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return CLIENT_TRAFFIC_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1SERVER_1TRAFFIC_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return SERVER_TRAFFIC_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1EARLY_1EXPORTER_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return EARLY_EXPORTER_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getTls13SecretEnum_1EXPORTER_1SECRET
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_SECRET_CALLBACK) && defined(WOLFSSL_TLS13)
    return EXPORTER_SECRET;
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_TLSv1Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_TLSv11Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_OLD_TLS)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_TLSv12Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(WOLFSSL_NO_TLS12)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_TLSv13Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_TLS13
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_DTLSv13Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && defined(WOLFSSL_DTLS13)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_ShaEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_SHA)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_Sha224Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_SHA256) && defined(WOLFSSL_SHA224)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_Sha256Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_SHA256)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_Sha384Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_SHA512) && defined(WOLFSSL_SHA384)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_Sha512Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_SHA512)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_EccEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_ECC
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_RsaEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifndef NO_RSA
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_RsaPssEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_RSA) && defined(WC_RSA_PSS)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_Curve25519Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_CURVE25519)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_Curve448Enabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_CURVE448)
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_FileSystemEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef NO_FILESYSTEM
    return JNI_FALSE;
#else
    return JNI_TRUE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_certReqEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_CERT_REQ
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_trustPeerCertEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_TRUST_PEER_CERT
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_sessionTicketEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_SESSION_TICKET
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_secretCallbackEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_SECRET_CALLBACK
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL Java_com_wolfssl_WolfSSL_encryptThenMacEnabled
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_ENCRYPT_THEN_MAC
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv3_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    return (jlong)(uintptr_t)wolfSSLv3_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv3_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_ALLOW_SSLV3) && !defined(NO_OLD_TLS)
    return (jlong)(uintptr_t)wolfSSLv3_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
    return (jlong)(uintptr_t)wolfTLSv1_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
    return (jlong)(uintptr_t)wolfTLSv1_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if !defined(NO_OLD_TLS) && defined(WOLFSSL_ALLOW_TLSV10)
    return (jlong)(uintptr_t)wolfTLSv1_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_11_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifndef NO_OLD_TLS
    return (jlong)(uintptr_t)wolfTLSv1_1_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_11_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifndef NO_OLD_TLS
    return (jlong)(uintptr_t)wolfTLSv1_1_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_11_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifndef NO_OLD_TLS
    return (jlong)(uintptr_t)wolfTLSv1_1_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_12_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return (jlong)(uintptr_t)wolfTLSv1_2_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_12_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return (jlong)(uintptr_t)wolfTLSv1_2_server_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_12_1ClientMethod(
    JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return (jlong)(uintptr_t)wolfTLSv1_2_client_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_13_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_TLS13
    return (jlong)(uintptr_t)wolfTLSv1_3_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_13_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_TLS13
    return (jlong)(uintptr_t)wolfTLSv1_3_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_TLSv1_13_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_TLS13
    return (jlong)(uintptr_t)wolfTLSv1_3_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && !defined(NO_OLD_TLS)
    return (jlong)(uintptr_t)wolfDTLSv1_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && !defined(NO_OLD_TLS)
    return (jlong)(uintptr_t)wolfDTLSv1_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && !defined(NO_OLD_TLS)
    return (jlong)(uintptr_t)wolfDTLSv1_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_12_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && !defined(WOLFSSL_NO_TLS12)
    return (jlong)(uintptr_t)wolfDTLSv1_2_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_12_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && !defined(WOLFSSL_NO_TLS12)
    return (jlong)(uintptr_t)wolfDTLSv1_2_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_12_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && !defined(WOLFSSL_NO_TLS12)
    return (jlong)(uintptr_t)wolfDTLSv1_2_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_13_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && defined(WOLFSSL_DTLS13)
    return (jlong)(uintptr_t)wolfDTLSv1_3_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_13_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && defined(WOLFSSL_DTLS13)
    return (jlong)(uintptr_t)wolfDTLSv1_3_server_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_DTLSv1_13_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(WOLFSSL_DTLS) && defined(WOLFSSL_DTLS13)
    return (jlong)(uintptr_t)wolfDTLSv1_3_client_method();
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv23_1Method
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return (jlong)(uintptr_t)wolfSSLv23_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv23_1ServerMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return (jlong)(uintptr_t)wolfSSLv23_server_method();
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_SSLv23_1ClientMethod
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return (jlong)(uintptr_t)wolfSSLv23_client_method();
}

JNIEXPORT jstring JNICALL Java_com_wolfssl_WolfSSL_getErrorString
  (JNIEnv* jenv, jclass jcl, jlong errNumber)
{
    char buffer[WOLFSSL_MAX_ERROR_SZ];
    jstring retString;

    (void)jcl;

    wolfSSL_ERR_error_string((unsigned long)errNumber, buffer);
    retString = (*jenv)->NewStringUTF(jenv, buffer);

    return retString;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_cleanup
  (JNIEnv* jenv, jclass jcl)
{
    int ret = WOLFSSL_SUCCESS;
    (void)jenv;
    (void)jcl;

    /* Call wolfSSL_Cleanup() first since it may use the logging callback,
     * before we free that next. */
    ret = wolfSSL_Cleanup();

    /* release global logging callback object if registered */
    if (g_loggingCbIfaceObj != NULL) {
        (*jenv)->DeleteGlobalRef(jenv, g_loggingCbIfaceObj);
        g_loggingCbIfaceObj = NULL;
    }

#ifdef HAVE_FIPS
    /* release existing FIPS callback object if set */
    if (g_fipsCbIfaceObj != NULL) {
        (*jenv)->DeleteGlobalRef(jenv, g_fipsCbIfaceObj);
        g_fipsCbIfaceObj = NULL;
    }
#endif

    return ret;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_debuggingON
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return wolfSSL_Debugging_ON();
}

JNIEXPORT void JNICALL Java_com_wolfssl_WolfSSL_debuggingOFF
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    wolfSSL_Debugging_OFF();

    return;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_setLoggingCb
  (JNIEnv* jenv, jclass jcl, jobject callback)
{
    int ret = 0;

    (void)jcl;

    if (jenv == NULL) {
        return BAD_FUNC_ARG;
    }

    /* release existing logging callback object if registered */
    if (g_loggingCbIfaceObj != NULL) {
        (*jenv)->DeleteGlobalRef(jenv, g_loggingCbIfaceObj);
        g_loggingCbIfaceObj = NULL;
    }

    if (callback != NULL) {
        /* store Java logging callback Interface object */
        g_loggingCbIfaceObj = (*jenv)->NewGlobalRef(jenv, callback);
        if (g_loggingCbIfaceObj == NULL) {
            printf("error storing global logging callback interface\n");
            return SSL_FAILURE;
        }

        ret = wolfSSL_SetLoggingCb(NativeLoggingCallback);
    }
    else {
        /* reset back to null */
        ret = wolfSSL_SetLoggingCb(NULL);
    }

    return ret;
}

/**
 * Native wolfSSL logging callback.
 *
 * We skip throwing exceptions in this function and just move on without
 * printing the log. Otherwise, our non-important exception here could cause
 * bad things to happen at the Java level - ie, causing the
 * certificate verify callback to fail unnecessarily.
 */
void NativeLoggingCallback(const int logLevel, const char *const logMessage)
{
    JNIEnv*   jenv = NULL;
    jint      vmret  = 0;
    jclass    logClass;
    jmethodID logMethod;
    jstring   logMsg;
    int       needsDetach = 0;  /* Should we explicitly detach? */
    jobjectRefType refcheck;

    /* get JNIEnv from JavaVM */
    vmret = (int)((*g_vm)->GetEnv(g_vm, (void**) &jenv, JNI_VERSION_1_6));
    if (vmret == JNI_EDETACHED) {
        /* If the JVM is shutting down, we may reach this point. One cause
         * of this can be if wolfSSL_Cleanup() is called from the atexit()
         * handler that native wolfSSL registers. wolfSSL_Cleanup() then does
         * some logging (WOLFSSL_ENTER) which reaches this code. Just return
         * since trying to re-attach was not working for these cases.*/
        return;

    } else if (vmret != JNI_OK) {
        printf("Unable to get JNIEnv from JavaVM in NativeLoggingCallback\n");
        return;
    }

    /* if g_loggingCbIfaceObj has been released (part of wolfSSL_Cleanup()),
     * just return and skip this log */
    if (g_loggingCbIfaceObj == NULL) {
        if (needsDetach == 1) {
            (*g_vm)->DetachCurrentThread(g_vm);
        }
        return;
    }

    /* check if our stored object reference is valid */
    refcheck = (*jenv)->GetObjectRefType(jenv, g_loggingCbIfaceObj);
    if (refcheck == 2) {

        /* lookup WolfSSLLoggingCallback class from global object ref */
        logClass = (*jenv)->GetObjectClass(jenv, g_loggingCbIfaceObj);
        if (!logClass) {
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
            }

            if (needsDetach == 1) {
                (*g_vm)->DetachCurrentThread(g_vm);
            }
            return;
        }

        logMethod = (*jenv)->GetMethodID(jenv, logClass,
                                            "loggingCallback",
                                            "(ILjava/lang/String;)V");
        if (logMethod == 0) {
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
            }
            if (needsDetach == 1) {
                (*g_vm)->DetachCurrentThread(g_vm);
            }
            return;
        }

        /* create jstring from char* */
        logMsg = (*jenv)->NewStringUTF(jenv, logMessage);

        (*jenv)->CallVoidMethod(jenv, g_loggingCbIfaceObj, logMethod,
                logLevel, logMsg);

        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);

            /* Not throwing exception, just move on without printing the log.
             * Otherwise, our non-important exception here could cause
             * bad things to happen at the Java level - ie, causing the
             * certificate verify callback to fail unnecessarily. */
            if (needsDetach == 1) {
                (*g_vm)->DetachCurrentThread(g_vm);
            }
            return;
        }

    }

    if (needsDetach == 1) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}

void NativeFIPSErrorCallback(const int ok, const int err,
                             const char* const hash)
{
#ifdef HAVE_FIPS
    JNIEnv*   jenv;
    jint      vmret  = 0;
    jclass    excClass;
    jclass    fipsCbClass;
    jmethodID errorMethod;
    jobjectRefType refcheck;
    jstring hashString;

    /* get JNIEnv from JavaVM */
    vmret = (int)((*g_vm)->GetEnv(g_vm, (void**) &jenv, JNI_VERSION_1_6));
    if (vmret == JNI_EDETACHED) {
#ifdef __ANDROID__
        vmret = (*g_vm)->AttachCurrentThread(g_vm, &jenv, NULL);
#else
        vmret = (*g_vm)->AttachCurrentThread(g_vm, (void**) &jenv, NULL);
#endif
        if (vmret) {
            printf("Failed to attach JNIEnv to thread\n");
        }
    } else if (vmret != JNI_OK) {
        printf("Unable to get JNIEnv from JavaVM\n");
    }

    /* find exception class */
    excClass = (*jenv)->FindClass(jenv, "java/lang/Exception");
    if ((*jenv)->ExceptionOccurred(jenv)) {
        (*jenv)->ExceptionDescribe(jenv);
        (*jenv)->ExceptionClear(jenv);
        return;
    }

    /* check if our stored object reference is valid */
    refcheck = (*jenv)->GetObjectRefType(jenv, g_fipsCbIfaceObj);
    if (refcheck == JNIGlobalRefType) {

        /* lookup WolfSSLLoggingCallback class from global object ref */
        fipsCbClass = (*jenv)->GetObjectClass(jenv, g_fipsCbIfaceObj);
        if (!fipsCbClass) {
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
            }

            (*jenv)->ThrowNew(jenv, excClass,
                "Can't get native WolfSSLFIPSErrorCallback class reference");
            return;
        }

        errorMethod = (*jenv)->GetMethodID(jenv, fipsCbClass,
                                            "errorCallback",
                                            "(IILjava/lang/String;)V");
        if (errorMethod == 0) {
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
            }
            (*jenv)->ThrowNew(jenv, excClass,
                "Error getting errorCallback method from JNI");
            return;
        }

        /* create jstring from char* */
        hashString = (*jenv)->NewStringUTF(jenv, hash);

        (*jenv)->CallVoidMethod(jenv, g_fipsCbIfaceObj, errorMethod,
                ok, err, hashString);

        /* release local reference to jstring, since returning to native */
        (*jenv)->DeleteLocalRef(jenv, hashString);

        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);

            (*jenv)->ThrowNew(jenv, excClass,
                    "Error calling FIPS error callback from JNI");
            return;
        }

    } else {
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
        }

        (*jenv)->ThrowNew(jenv, excClass,
                "Object reference invalid in NativeFIPSErrorCallback");
    }
#else
    (void)ok;
    (void)err;
    (void)hash;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_setFIPSCb
  (JNIEnv* jenv, jclass jcl, jobject callback)
{
    int ret = NOT_COMPILED_IN;
    (void)jcl;

#ifdef HAVE_FIPS
    if (jenv == NULL) {
        return BAD_FUNC_ARG;
    }

    /* release existing FIPS callback object if set */
    if (g_fipsCbIfaceObj != NULL) {
        (*jenv)->DeleteGlobalRef(jenv, g_fipsCbIfaceObj);
        g_fipsCbIfaceObj = NULL;
    }

    if (callback != NULL) {
        /* store Java FIPS callback Interface object */
        g_fipsCbIfaceObj = (*jenv)->NewGlobalRef(jenv, callback);
        if (g_fipsCbIfaceObj == NULL) {
            printf("error storing global wolfCrypt FIPS callback interface\n");
            return SSL_FAILURE;
        }

        /* register NativeFIPSErrorCallback, wraps Java callback */
        ret = wolfCrypt_SetCb_fips(NativeFIPSErrorCallback);
        if (ret == 0) {
            ret = SSL_SUCCESS;
        }
    }
#else
    (void)jenv;
    (void)callback;
#endif

    return ret;
}

JNIEXPORT jstring JNICALL Java_com_wolfssl_WolfSSL_getWolfCryptFIPSCoreHash
  (JNIEnv* jenv, jclass jcl)
{
#ifdef HAVE_FIPS
    return (*jenv)->NewStringUTF(jenv, wolfCrypt_GetCoreHash_fips());
#else
    (void)jenv;
    (void)jcl;
    return NULL;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_memsaveSessionCache
  (JNIEnv* jenv, jclass jcl, jbyteArray mem, jint sz)
{
#ifdef PERSIST_SESSION_CACHE
    int ret;
    int cacheSz;
    unsigned char* memBuf = NULL;

    (void)jcl;

    if (!jenv || !mem || (sz <= 0))
        return BAD_FUNC_ARG;

    memBuf = (unsigned char*)XMALLOC((int)sz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (memBuf == NULL) {
        return MEMORY_E;
    }
    XMEMSET(memBuf, 0, (int)sz);

    ret = wolfSSL_memsave_session_cache(memBuf, sz);

    /* how much data do we need to write? */
    cacheSz = wolfSSL_get_session_cache_memsize();

    /* set jbyteArray for return */
    if (cacheSz >= 0) {
        (*jenv)->SetByteArrayRegion(jenv, mem, 0, cacheSz, (jbyte*)memBuf);
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            XFREE(memBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
            return SSL_FAILURE;
        }
    }

    XFREE(memBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    return ret;
#else
    (void)jenv;
    (void)jcl;
    (void)mem;
    (void)sz;
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_memrestoreSessionCache
  (JNIEnv* jenv, jclass jcl, jbyteArray mem, jint sz)
{
#ifdef PERSIST_SESSION_CACHE
    int ret = SSL_SUCCESS;
    unsigned char* memBuf = NULL;

    (void)jcl;

    if (!jenv || !mem || (sz <= 0))
        return BAD_FUNC_ARG;

    memBuf = (unsigned char*)XMALLOC((int)sz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (memBuf == NULL) {
        return MEMORY_E;
    }
    XMEMSET(memBuf, 0, (int)sz);

    (*jenv)->GetByteArrayRegion(jenv, mem, 0, sz, (jbyte*)memBuf);
    if ((*jenv)->ExceptionOccurred(jenv)) {
        (*jenv)->ExceptionDescribe(jenv);
        (*jenv)->ExceptionClear(jenv);
        ret = SSL_FAILURE;
    }

    if (ret == SSL_SUCCESS) {
        ret = wolfSSL_memrestore_session_cache(memBuf, sz);
    }

    XFREE(memBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    return ret;
#else
    (void)jenv;
    (void)jcl;
    (void)mem;
    (void)sz;
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getSessionCacheMemsize
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;
#ifdef PERSIST_SESSION_CACHE
    return wolfSSL_get_session_cache_memsize();
#else
    return NOT_COMPILED_IN;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getPkcs8TraditionalOffset
  (JNIEnv* jenv, jclass jcl, jbyteArray in, jlong idx, jlong sz)
{
    int ret;
    word32 inOutIdx;
    unsigned char* inBuf;

    (void)jcl;

    if (!jenv || !in || (sz <= 0))
        return BAD_FUNC_ARG;

    inBuf = (unsigned char*)XMALLOC((long)sz, NULL, DYNAMIC_TYPE_TMP_BUFFER);
    if (inBuf == NULL) {
        return MEMORY_E;
    }
    XMEMSET(inBuf, 0, DYNAMIC_TYPE_TMP_BUFFER);

    (*jenv)->GetByteArrayRegion(jenv, in, 0, (jsize)sz, (jbyte*)inBuf);
    if ((*jenv)->ExceptionOccurred(jenv)) {
        (*jenv)->ExceptionDescribe(jenv);
        (*jenv)->ExceptionClear(jenv);
    	XFREE(inBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);
        return SSL_FAILURE;
    }

    inOutIdx = (word32)idx;
    ret = wc_GetPkcs8TraditionalOffset(inBuf, &inOutIdx, (word32)sz);

    XFREE(inBuf, NULL, DYNAMIC_TYPE_TMP_BUFFER);

    if (ret < 0)
        return ret;

    return (int)inOutIdx;
}

JNIEXPORT jbyteArray JNICALL Java_com_wolfssl_WolfSSL_x509_1getDer
  (JNIEnv* jenv, jclass jcl, jlong x509Ptr)
{
#if defined(KEEP_PEER_CERT) || defined(SESSION_CERTS)
    int outSz = 0;
    const unsigned char* derCert;
    jbyteArray out = NULL;
    WOLFSSL_X509* x509 = (WOLFSSL_X509*)(uintptr_t)x509Ptr;

    (void)jcl;

    if (jenv == NULL || x509 == NULL) {
        return NULL;
    }

    derCert = wolfSSL_X509_get_der(x509, &outSz);

    if (outSz >= 0) {

        (*jenv)->SetByteArrayRegion(jenv, out, 0, outSz, (jbyte*)derCert);
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            return NULL;
        }
        return out;

    } else {
        return NULL;
    }
#else
    (void)jenv;
    (void)jcl;
    (void)x509Ptr;
    return NULL;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getHmacMaxSize
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

    return WC_MAX_DIGEST_SIZE;
}

JNIEXPORT jlong JNICALL Java_com_wolfssl_WolfSSL_getLibVersionHex
  (JNIEnv* jenv, jclass jcl)
{
    return (jlong)wolfSSL_lib_version_hex();
}

JNIEXPORT jstring JNICALL Java_com_wolfssl_WolfSSL_getEnabledCipherSuites
  (JNIEnv* jenv, jclass jcl)
{
    int ret;
    char ciphers[4096];
    jstring retString;

    (void)jcl;

    ret = wolfSSL_get_ciphers(ciphers, sizeof(ciphers));
    if (ret != WOLFSSL_SUCCESS) {
        return NULL;
    }

    retString = (*jenv)->NewStringUTF(jenv, ciphers);

    return retString;
}

JNIEXPORT jstring JNICALL Java_com_wolfssl_WolfSSL_getEnabledCipherSuitesIana
  (JNIEnv *jenv, jclass jcl)
{
    int ret;
    char ciphers[4096];
    jstring retString;

    (void)jcl;

    ret = wolfSSL_get_ciphers_iana(ciphers, sizeof(ciphers));
    if (ret != WOLFSSL_SUCCESS) {
        return NULL;
    }

    retString = (*jenv)->NewStringUTF(jenv, ciphers);

    return retString;
}

/* Returns list of available cipher suites in IANA format. Uses
 * wolfSSL_get_ciphers_compat() in order to get a prioritized list. Normal
 * wolfSSL_get_ciphers() returns list of compiled-in cipher suites, but not
 * in same priority order that would be set during a normal connection.
 *
 * @param protocolVersion protocol version that matches the Enum in
 *        src/java/com/wolfssl/WolfSSL.java:
 *
 *        public static enum TLS_VERSION {
 *            INVALID, (0)
 *            TLSv1,   (1)
 *            TLSv1_1, (2)
 *            TLSv1_2, (3)
 *            TLSv1_3, (4)
 *            SSLv23   (5)
 *        }
 * @returns colon-separated cipher suite string.
 */
JNIEXPORT jstring JNICALL Java_com_wolfssl_WolfSSL_getAvailableCipherSuitesIana
  (JNIEnv* jenv, jclass jcl, jint protocolVersion)
{
    char cipherList[4096];
    int i = 0;
    int numCiphers = 0;
#if defined(WOLFSSL_CIPHER_INTERNALNAME) || defined(NO_ERROR_STRINGS) || \
    defined(WOLFSSL_QT)
    int ret = 0;
    int flags;
    byte cipherSuite0;
    byte cipherSuite;
#endif
    const char* cipherName = NULL;
    const char* ianaName = NULL;
    WOLFSSL_METHOD* method = NULL;

    WOLFSSL* ssl = NULL;
    WOLFSSL_CTX* ctx = NULL;

    STACK_OF(SSL_CIPHER) *supportedCiphers = NULL;
    const SSL_CIPHER* cipher = NULL;

    jstring retString;
    (void)jcl;

    if (jenv == NULL) {
        return NULL;
    }

    if (protocolVersion < 0 || protocolVersion > 8) {
        printf("Input protocol version invalid: %d\n", protocolVersion);
        return NULL;
    }

    XMEMSET(cipherList, 0, sizeof(cipherList));

    switch (protocolVersion) {
#ifndef NO_OLD_TLS
    #ifdef WOLFSSL_ALLOW_TLSV10
        case 1:
            method = wolfTLSv1_client_method();
            break;
    #endif
        case 2:
            method = wolfTLSv1_1_client_method();
            break;
#endif /* NO_OLD_TLS */
#ifndef WOLFSSL_NO_TLS12
        case 3:
            method = wolfTLSv1_2_client_method();
            break;
#endif
#ifdef WOLFSSL_TLS13
        case 4:
            method = wolfTLSv1_3_client_method();
            break;
#endif
        case 5:
            method = wolfSSLv23_client_method();
            break;
#ifdef WOLFSSL_DTLS
    #ifndef NO_OLD_TLS
        case 6:
            method = wolfDTLSv1_client_method();
            break;
    #endif
    #ifndef WOLFSSL_NO_TLS12
        case 7:
            method = wolfDTLSv1_2_client_method();
            break;
    #endif
    #ifdef WOLFSSL_DTLS13
        case 8:
            method = wolfDTLSv1_3_client_method();
            break;
    #endif
#endif
        default:
            printf("Input protocol version invalid: %d\n", protocolVersion);
            return NULL;
    }

    /* create temporary WOLFSSL_CTX and WOLFSSL structs to get expected
     * available cipher list */
    ctx = wolfSSL_CTX_new(method);
    if (ctx == NULL) {
        return NULL;
    }

    ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        wolfSSL_CTX_free(ctx);
        return NULL;
    }

    supportedCiphers = wolfSSL_get_ciphers_compat(ssl);
    if (supportedCiphers == NULL) {
        wolfSSL_free(ssl);
        wolfSSL_CTX_free(ctx);
        return NULL;
    }

    numCiphers = sk_num(supportedCiphers);

    for (i = 0; i < numCiphers; i++) {
        cipher = (const WOLFSSL_CIPHER*)sk_value(supportedCiphers, i);
        if (cipher != NULL) {
            cipherName =  wolfSSL_CIPHER_get_name(cipher);

        #if defined(WOLFSSL_CIPHER_INTERNALNAME) || \
            defined(NO_ERROR_STRINGS) || defined(WOLFSSL_QT)
            /* CIPHER_get_name() returns internal cipher format in this case,
             * need to convert to IANA format next */
            ret = wolfSSL_get_cipher_suite_from_name(cipherName,
                        &cipherSuite0, &cipherSuite, &flags);
            if (ret == 0) {
                ianaName = wolfSSL_get_cipher_name_iana_from_suite(
                                cipherSuite0, cipherSuite);
            }
        #else
            /* cipherName already in IANA format */
            ianaName = cipherName;
        #endif
            if (ianaName != NULL) {
                /* colon separated list */
                if (i != 0 && (XSTRLEN(cipherList) + 1) < sizeof(cipherList)) {
                    XSTRNCAT(cipherList, ":",
                             sizeof(cipherList) - XSTRLEN(cipherList) - 1);
                }
                if ((XSTRLEN(ianaName) + XSTRLEN(cipherList) + 1) <
                        sizeof(cipherList)) {
                    XSTRNCAT(cipherList, ianaName,
                             sizeof(cipherList) - XSTRLEN(cipherList) - 1);
                }
            }
        }
        /* reset ianaName to NULL for next loop */
        ianaName = NULL;
    }

    wolfSSL_free(ssl);
    wolfSSL_CTX_free(ctx);

    /* build and return Java String from cipherList array */
    retString = (*jenv)->NewStringUTF(jenv, cipherList);

    return retString;
}

#ifdef WOLFSSLJNI_USE_NATIVE_CRYPTOCB
/**
 * Default native wolfSSL crypto callback function.
 * This is called by default when wolfJSSE's WolfSSLProvider.registerDevId()
 * is called, and is called by the native JNI API below.
 *
 * This function should be directly edited here to meet required
 * functionality, or re-implemented and the registration point in the following
 * function below should be edited:
 *
 * Java_com_wolfssl_WolfSSL_wc_1CryptoCb_1RegisterDevice()
 */
int DefaultNativeCryptoDevCb(int devId, wc_CryptoInfo* info, void* ctx)
{
    int ret = CRYPTOCB_UNAVAILABLE;
    (void)devId;
    (void)info;
    (void)ctx;

    /* Return CRYPTOCB_UNAVAILABLE to bypass HW and use SW. Edit function
     * body here for your correct/expected functionality. */
    return ret;
}
#endif /* WOLFSSLJNI_USE_NATIVE_CRYPTOCB */

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_wc_1CryptoCb_1RegisterDevice
  (JNIEnv* jenv, jclass jcl, jint devId)
{
#ifdef WOLF_CRYPTO_CB

    /* WOLFSSLJNI_USE_NATIVE_CRYPTOCB callback is mutually exclusive of other
     * callback support below, and is intended to meet a specific use case
     * where a single user-implemented native crypto callback needs to be
     * written and registered in the stub function above */
    #ifdef WOLFSSLJNI_USE_NATIVE_CRYPTOCB
        return wc_CryptoCb_RegisterDevice((int)devId,
                    DefaultNativeCryptoDevCb, NULL);
    #else
        /* Lookup the devId and see if it matches a known implementation.
         * For future hardware crypto implementations, please consider adding
         * directly to native wolfSSL if possible (ie: HW-specific code
         * inside wolfcrypt/src/port directory). */

        /* WISeKey VaultIC crypto callback implementation */
        #if defined(HAVE_CCBVAULTIC) && defined(WOLF_CRYPTO_CB_CMD)
            #include "ccb_vaultic.h"
            if(devId == CCBVAULTIC420_DEVID) {
                return wc_CryptoCb_RegisterDevice((int)devId,
                                                  ccbVaultIc_CryptoCb, NULL);
            }
        #endif

        /* could add additional elif blocks for ports / known callbacks */

        /* No matching callback, return CRYPTOCB_UNAVAILABLE */
        return CRYPTOCB_UNAVAILABLE;
    #endif
#else
    /* no-op if crypto callbacks not compiled into native wolfSSL */
    (void)jenv;
    (void)jcl;
    (void)devId;
    return 0;
#endif
}

JNIEXPORT void JNICALL Java_com_wolfssl_WolfSSL_wc_1CryptoCb_1UnRegisterDevice
  (JNIEnv* jenv, jclass jcl, jint devId)
{
#ifdef WOLF_CRYPTO_CB
    wc_CryptoCb_UnRegisterDevice((int)devId);
#else
    /* no-op if crypto callbacks not compiled into native wolfSSL */
    (void)jenv;
    (void)jcl;
    (void)devId;
    return;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledCRL
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_CRL
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledCRLMonitor
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_CRL_MONITOR
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledOCSP
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_OCSP
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledPSK
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifndef NO_PSK
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledDTLS
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_DTLS
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledSendHrrCookie
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef WOLFSSL_SEND_HRR_COOKIE
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledAtomicUser
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef ATOMIC_USER
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledPKCallbacks
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#ifdef HAVE_PK_CALLBACKS
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_isEnabledTLSExtendedMasterSecret
  (JNIEnv* jenv, jclass jcl)
{
    (void)jenv;
    (void)jcl;

#if defined(HAVE_EXTENDED_MASTER) && !defined(NO_WOLFSSL_CLIENT)
    return 1;
#else
    return 0;
#endif
}

JNIEXPORT jobjectArray JNICALL Java_com_wolfssl_WolfSSL_getProtocols
  (JNIEnv* jenv, jclass jcl)
{
    return Java_com_wolfssl_WolfSSL_getProtocolsMask(jenv, jcl, 0);
}

JNIEXPORT jobjectArray JNICALL Java_com_wolfssl_WolfSSL_getProtocolsMask
  (JNIEnv* jenv, jclass jcl, jlong mask)
{
    jobjectArray ret;
    int numProtocols = 0, idx = 0;

    (void)jcl;

    /* Get the number of protocols enabled, based on provided mask. Native
     * wolfSSL doesn't have mask values for DTLS, so we lump them together
     * with their corresponding TLS version, if correct defines are set. */
#ifdef WOLFSSL_TLS13
    if(!(mask & SSL_OP_NO_TLSv1_3)) {
        numProtocols += 1;
    #if defined(WOLFSSL_DTLS) && defined(WOLFSSL_DTLS13)
        numProtocols += 1;
    #endif
    }
#endif
#ifndef WOLFSSL_NO_TLS12
    if(!(mask & SSL_OP_NO_TLSv1_2))  {
        numProtocols += 1;
    #if defined(WOLFSSL_DTLS) && !defined(WOLFSSL_NO_TLS12)
        numProtocols += 1;
    #endif
    }
#endif
#ifndef NO_OLD_TLS
    if(!(mask & SSL_OP_NO_TLSv1_1)) {
        numProtocols += 1;
    }
#ifdef WOLFSSL_ALLOW_TLSV10
    if(!(mask & SSL_OP_NO_TLSv1)) {
        numProtocols += 1;
    #ifdef WOLFSSL_DTLS
        numProtocols += 1;
    #endif
    }
#endif /* WOLFSSL_ALLOW_TLSv10 */
#endif /* !NO_OLD_TLS */
#ifdef WOLFSSL_ALLOW_SSLv3
    if(!(mask & SSL_OP_NO_SSLv3)) {
        numProtocols += 1;
    }
#endif

    ret = (*jenv)->NewObjectArray(jenv, numProtocols,
            (*jenv)->FindClass(jenv, "java/lang/String"), NULL);
    if (ret == NULL) {
        return NULL;
    }

#ifdef WOLFSSL_TLS13
    if(!(mask & SSL_OP_NO_TLSv1_3)) {
        (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                (*jenv)->NewStringUTF(jenv, "TLSv1.3"));
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            (*jenv)->ThrowNew(jenv, jcl, "Error setting TLSv1.3 string");
            return NULL;
        }
    }
#endif

#ifndef WOLFSSL_NO_TLS12
    if(!(mask & SSL_OP_NO_TLSv1_2)) {
        (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                (*jenv)->NewStringUTF(jenv, "TLSv1.2"));
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            (*jenv)->ThrowNew(jenv, jcl, "Error setting TLSv1.2 string");
            return NULL;
        }
    }
#endif

#ifndef NO_OLD_TLS
    if(!(mask & SSL_OP_NO_TLSv1_1)) {
        (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                (*jenv)->NewStringUTF(jenv, "TLSv1.1"));
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            (*jenv)->ThrowNew(jenv, jcl, "Error setting TLSv1.1 string");
            return NULL;
        }
    }
#ifdef WOLFSSL_ALLOW_TLSV10
    if(!(mask & SSL_OP_NO_TLSv1)) {
        (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                (*jenv)->NewStringUTF(jenv, "TLSv1"));
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            (*jenv)->ThrowNew(jenv, jcl, "Error setting TLSv1 string");
            return NULL;
        }
    }
#endif /* WOLFSSL_ALLOW_TLSv10 */
#endif /* !NO_OLD_TLS */

#ifdef WOLFSSL_ALLOW_SSLv3
    if(!(mask & SSL_OP_NO_SSLv3)) {
        (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                (*jenv)->NewStringUTF(jenv, "SSLv3"));
        if ((*jenv)->ExceptionOccurred(jenv)) {
            (*jenv)->ExceptionDescribe(jenv);
            (*jenv)->ExceptionClear(jenv);
            (*jenv)->ThrowNew(jenv, jcl, "Error setting SSLv3 string");
            return NULL;
        }
    }
#endif

#ifdef WOLFSSL_DTLS
    #ifndef NO_OLD_TLS
        if(!(mask & SSL_OP_NO_TLSv1)) {
            (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                    (*jenv)->NewStringUTF(jenv, "DTLSv1"));
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
                (*jenv)->ThrowNew(jenv, jcl, "Error setting DTLSv1 string");
                return NULL;
            }
        }
    #endif
    #ifndef WOLFSSL_NO_TLS12
        if(!(mask & SSL_OP_NO_TLSv1_2)) {
            (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                    (*jenv)->NewStringUTF(jenv, "DTLSv1.2"));
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
                (*jenv)->ThrowNew(jenv, jcl, "Error setting DTLSv1.2 string");
                return NULL;
            }
        }
    #endif
    #if defined(WOLFSSL_TLS13) && defined(WOLFSSL_DTLS13)
        if(!(mask & SSL_OP_NO_TLSv1_3)) {
            (*jenv)->SetObjectArrayElement(jenv, ret, idx++,
                    (*jenv)->NewStringUTF(jenv, "DTLSv1.3"));
            if ((*jenv)->ExceptionOccurred(jenv)) {
                (*jenv)->ExceptionDescribe(jenv);
                (*jenv)->ExceptionClear(jenv);
                (*jenv)->ThrowNew(jenv, jcl, "Error setting DTLSv1.3 string");
                return NULL;
            }
        }
    #endif
#endif
    return ret;
}

JNIEXPORT jint JNICALL Java_com_wolfssl_WolfSSL_getErrno
  (JNIEnv* jenv, jclass jcl)
{
#ifndef USE_WINDOWS_API
    return errno;
#else
    return 0;
#endif
}
