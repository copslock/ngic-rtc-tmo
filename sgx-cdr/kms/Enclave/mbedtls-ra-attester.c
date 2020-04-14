/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Code to create an extended X.509 certificate using the mbedtls
   library. */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sgx_uae_service.h>

#include <mbedtls/certs.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>

#include "ra.h"
#include "mbedtls-ra-attester.h"

void do_remote_attestation(sgx_report_data_t* report_data,
                           attestation_verification_report_t* r);

static const size_t SHA256_DIGEST_SIZE = 32;

static
void sha256_rsa_pubkey(unsigned char hash[SHA256_DIGEST_SIZE],
                       const mbedtls_pk_context* pk) {

    static const int pk_der_size_max = 512;
    uint8_t pk_der[pk_der_size_max];
    memset(pk_der, 0, pk_der_size_max);
    
    /* From the mbedtls documentation: Write a public key to a
       SubjectPublicKeyInfo DER structure Note: data is written at the
       end of the buffer! Use the return value to determine where you
       should start using the buffer. */
    int pk_der_size_byte = mbedtls_pk_write_pubkey_der((mbedtls_pk_context*) pk,
                                                       pk_der, pk_der_size_max);

    // Can only handle 2048 bit RSA keys for now. Other key sizes will
    // have a different pk_der_offset.
    assert(pk_der_size_byte == 294);


    /* Move the data to the beginning of the buffer, to avoid pointer
       arithmetic from this point forward. */
    memmove(pk_der, pk_der + pk_der_size_max - pk_der_size_byte, pk_der_size_byte);

    /* 24 since we skip the DER structure header. (i.e, PKCS#1 header). */
    static const uint64_t pk_der_offset = 24;

    memset(hash, 0, SHA256_DIGEST_SIZE);
    mbedtls_sha256(pk_der + pk_der_offset, pk_der_size_byte - pk_der_offset,
                   hash, 0 /* is224 */);
}

static
void generate_x509
(
    mbedtls_x509_crt* crt /* out */,
    mbedtls_pk_context* subject_key /* in */,
    const attestation_verification_report_t* attn_report
)
{
    mbedtls_x509write_cert writecrt;
    mbedtls_entropy_context entropy;
    mbedtls_entropy_init( &entropy );
    /* mbedtls_pk_context subject_key; */
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ctr_drbg_init( &ctr_drbg );
    
    const char *pers = "crt example app";
    assert(0 == mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy,
                                       (const unsigned char *) pers,
                                       strlen( pers ) ));

    int ret;
    mbedtls_x509write_crt_init(&writecrt);
    mbedtls_x509write_crt_set_md_alg(&writecrt, MBEDTLS_MD_SHA256);

    mbedtls_x509write_crt_set_subject_key(&writecrt, subject_key);
    mbedtls_x509write_crt_set_issuer_key(&writecrt, subject_key);

    ret = mbedtls_x509write_crt_set_subject_name(&writecrt,
                                                 "CN=kms.c3po.com,O=Intel Corporation,C=US");
    assert(ret == 0);
    ret = mbedtls_x509write_crt_set_issuer_name(&writecrt,
                                                "CN=kms.c3po.com,O=Intel Corporation,C=US");
    assert(ret == 0);

    mbedtls_mpi serial;
    mbedtls_mpi_init(&serial);
    ret = mbedtls_mpi_read_string(&serial, 10, "1");
    assert(ret == 0);

    ret = mbedtls_x509write_crt_set_serial(&writecrt, &serial);
    assert(ret == 0);
    ret = mbedtls_x509write_crt_set_validity(&writecrt,
                                             "20010101000000", "20301231235959");
    assert(ret == 0);
    ret = mbedtls_x509write_crt_set_basic_constraints(&writecrt, 0, -1);
    assert(ret == 0);
    ret = mbedtls_x509write_crt_set_subject_key_identifier(&writecrt);
    assert(ret == 0);
    ret = mbedtls_x509write_crt_set_authority_key_identifier(&writecrt);
    assert(ret == 0);

    // 1.2.840.113741.1337.2
    unsigned char oid_ias_report[] = {0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, 0x02};
    // 1.2.840.113741.1337.3
    unsigned char oid_ias_sign_ca_cert[] = {0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, 0x03};
    // 1.2.840.113741.1337.4
    unsigned char oid_ias_sign_cert[] = {0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, 0x04};
    // 1.2.840.113741.1337.5
    unsigned char oid_ias_report_signature[] = {0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, 0x05};

#ifdef SGX_MODE
    ret = mbedtls_x509write_crt_set_extension(&writecrt,
                                              (char*) oid_ias_report,
                                              sizeof(oid_ias_report),
                                              0,
                                              (const uint8_t*) attn_report->ias_report,
                                              attn_report->ias_report_len);
    assert(ret == 0);
    
    ret = mbedtls_x509write_crt_set_extension(&writecrt,
                                              (char*) oid_ias_sign_ca_cert,
                                              sizeof(oid_ias_sign_ca_cert),
                                              0,
                                              (const uint8_t*) attn_report->ias_sign_ca_cert,
                                              attn_report->ias_sign_ca_cert_len);
    assert(ret == 0);

    ret = mbedtls_x509write_crt_set_extension(&writecrt,
                                              (char*) oid_ias_sign_cert,
                                              sizeof(oid_ias_sign_cert),
                                              0,
                                              (const uint8_t*) attn_report->ias_sign_cert,
                                              attn_report->ias_sign_cert_len);
    assert(ret == 0);

    ret = mbedtls_x509write_crt_set_extension(&writecrt,
                                              (char*) oid_ias_report_signature,
                                              sizeof(oid_ias_report_signature),
                                              0,
                                              (const uint8_t*) attn_report->ias_report_signature,
                                              attn_report->ias_report_signature_len);
    assert(ret == 0);
#endif

    unsigned char output_buf[16 * 1024] = {0, };
    int len;
    len = mbedtls_x509write_crt_der(&writecrt, output_buf, sizeof(output_buf),
                                    mbedtls_ctr_drbg_random, &ctr_drbg);
    assert(len > 0);
    
    mbedtls_x509_crt_parse_der(crt, output_buf + sizeof(output_buf) - len, len);

    mbedtls_mpi_free( &serial );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy );
}

/* Given a key, generate a certificate for it. */
static
void create_x509
(
    mbedtls_pk_context* key,
    mbedtls_x509_crt* cert
)
{
    sgx_report_data_t report_data = {0, };
    sha256_rsa_pubkey(report_data.d, key);
    attestation_verification_report_t attestation_report;
    memset(&attestation_report,0,sizeof(attestation_report));

#ifdef SGX_MODE
    do_remote_attestation(&report_data, &attestation_report);
#endif
    generate_x509(cert, key, &attestation_report);
    
    /* printf_sgx("attestation_report.ias_report=\n"); */
    /* printf_sgx("%.s", attestation_report.ias_report_len, attestation_report.ias_report); */
}

/* Generate a key and certificate. */
void create_key_and_x509
(
    mbedtls_pk_context* key,
    mbedtls_x509_crt* cert
)
{
    int ret;

    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ctr_drbg_init( &ctr_drbg );

    mbedtls_entropy_context entropy;
    mbedtls_entropy_init( &entropy );

    const char* pers = "deadbeef";
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *) pers,
                                strlen(pers));
    assert(ret == 0);

    mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));

    mbedtls_rsa_init((mbedtls_rsa_context*)key->pk_ctx,
                     MBEDTLS_RSA_PKCS_V15, 0);
    
    ret = mbedtls_rsa_gen_key((mbedtls_rsa_context*)key->pk_ctx,
                              mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
    assert(ret == 0);
    
    create_x509(key, cert);
}
