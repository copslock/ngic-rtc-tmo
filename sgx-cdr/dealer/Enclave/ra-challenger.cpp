//#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/base64.h>
#include "mbedtls/ctr_drbg.h"
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/pk_internal.h>

#include "memmem.h"

#include "ra.h"
#include "ra-challenger.h"


#define OID(N) {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF8, 0x4D, 0x8A, 0x39, N}

const uint8_t ias_response_body_oid[] =    OID(0x02);
const uint8_t ias_root_cert_oid[] =        OID(0x03);
const uint8_t ias_leaf_cert_oid[] =        OID(0x04);
const uint8_t ias_report_signature_oid[] = OID(0x05);

static
void find_oid(const unsigned char* ext, size_t ext_len,
              const unsigned char* oid,
              size_t oid_len,
              unsigned char** val, size_t* len) {

    uint8_t* p = (uint8_t *) memmem((const void *)ext, ext_len, oid, oid_len);
    assert(p != NULL);

    p += oid_len;

    // three bytes encoding criticality 0x01, 0x01, 0xFF
    int i = 0;
#if 0
    // Enable again if extension is deemed critical. Most TLS
    // implementation will fail validation of a certificate with
    // unknown critical extensions.
    assert(p[i++] == 0x01);
    assert(p[i++] == 0x01);
    assert(p[i++] == 0xFF);
#endif

    // Now comes the octet string
    assert(p[i++] == 0x04); // tag for octet string
    assert(p[i++] == 0x82); // length encoded in two bytes
    *len  =  p[i++] << 8;
    *len +=  p[i++];
    *val  = &p[i++];
}

static
void extract_x509_extension
(
    uint8_t* ext,
    int ext_len,
    const uint8_t* oid,
    size_t oid_len,
    uint8_t* data,
    uint32_t* data_len,
    uint32_t data_max_len
)
{
    uint8_t* base64_data;
    size_t base64_data_len;
    
    find_oid(ext, ext_len, oid, oid_len,
             &base64_data, &base64_data_len);
    
    assert(base64_data != NULL);
    assert(base64_data_len <= data_max_len);

    size_t out_len;
    int ret;
    ret = mbedtls_base64_decode(data,
                                data_max_len,
                                &out_len,
                                base64_data,
                                base64_data_len);

    assert(ret == 0);
    assert(out_len <= UINT32_MAX);
    *data_len = (uint32_t) out_len;
}

/* Extract extensions from X509 and decode base64. */
static
void extract_x509_extensions
(
    uint8_t* ext,
    int ext_len,
    attestation_verification_report_t* attn_report
)
{
    extract_x509_extension(ext, ext_len,
                           ias_response_body_oid, sizeof(ias_response_body_oid),
                           attn_report->ias_report,
                           &attn_report->ias_report_len,
                           sizeof(attn_report->ias_report));

    extract_x509_extension(ext, ext_len,
                           ias_root_cert_oid, sizeof(ias_root_cert_oid),
                           attn_report->ias_sign_ca_cert,
                           &attn_report->ias_sign_ca_cert_len,
                           sizeof(attn_report->ias_sign_ca_cert));

    extract_x509_extension(ext, ext_len,
                           ias_leaf_cert_oid, sizeof(ias_leaf_cert_oid),
                           attn_report->ias_sign_cert,
                           &attn_report->ias_sign_cert_len,
                           sizeof(attn_report->ias_sign_cert));

    extract_x509_extension(ext, ext_len,
                           ias_report_signature_oid, sizeof(ias_report_signature_oid),
                           attn_report->ias_report_signature,
                           &attn_report->ias_report_signature_len,
                           sizeof(attn_report->ias_report_signature));
}

static
void get_quote_from_extension(uint8_t* ext, size_t ext_len, sgx_quote_t* q) {

    uint8_t report[2048];
    uint32_t report_len;
    
    extract_x509_extension(ext, ext_len,
                           ias_response_body_oid, sizeof(ias_response_body_oid),
                           report, &report_len, sizeof(report));
    
    get_quote_from_report(report, report_len, q);
}

void get_quote_from_cert
(
    const uint8_t* der_crt,
    uint32_t der_crt_len,
    sgx_quote_t* q
)
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    mbedtls_x509_crt_parse(&crt, der_crt, der_crt_len);
    get_quote_from_extension(crt.v3_ext.p, crt.v3_ext.len, q);
    mbedtls_x509_crt_free(&crt);
}

void get_quote_from_cert1
(
    const uint8_t* der_crt,
    uint32_t der_crt_len,
    sgx_quote_t* q
)
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    mbedtls_x509_crt_parse(&crt, der_crt, der_crt_len);
    get_quote_from_extension(crt.v3_ext.p, crt.v3_ext.len, q);
    mbedtls_x509_crt_free(&crt);
}

void get_quote_from_report(const uint8_t* report /* in */,
                           const int report_len  /* in */,
                           sgx_quote_t* quote)
{
    (void) report_len;

    const char* json_string = "\"isvEnclaveQuoteBody\":\"";
    char* p_begin = strstr((const char*) report, json_string);
    assert(p_begin != NULL);
    p_begin += strlen(json_string);
    const char* p_end = strchr(p_begin, '"');
    assert(p_end != NULL);

    const int quote_base64_len = p_end - p_begin;
    uint8_t* quote_bin = (uint8_t *) malloc(quote_base64_len);
    size_t quote_bin_len = quote_base64_len;

    mbedtls_base64_decode(quote_bin, quote_base64_len,
                          &quote_bin_len,
                          (unsigned char*) p_begin, quote_base64_len);

    assert(quote_bin_len <= sizeof(sgx_quote_t));
    memset(quote, 0, sizeof(sgx_quote_t));
    memcpy(quote, quote_bin, quote_bin_len);
    free(quote_bin);
}

static
int verify_report_data_against_server_cert
(
    mbedtls_x509_crt* crt,
    sgx_quote_t* quote
)
{
    static const int pk_der_size_max = 512;
    uint8_t pk_der[pk_der_size_max];
    memset(pk_der, 0, pk_der_size_max);
    /* From the mbedtls documentation: Write a public key to a
       SubjectPublicKeyInfo DER structure Note: data is written at the
       end of the buffer! Use the return value to determine where you
       should start using the buffer. */
    int pk_der_size_byte = mbedtls_pk_write_pubkey_der(&crt->pk, pk_der, pk_der_size_max);
    /* Move the data to the beginning of the buffer, to avoid pointer
       arithmetic from this point forward. */
    memmove(pk_der, pk_der + pk_der_size_max - pk_der_size_byte, pk_der_size_byte);

    /* 24 since we skip the DER structure header. */
    static const uint64_t pk_der_offset = 24;

    static const size_t SHA256_DIGEST_SIZE = 32;
    uint8_t shaSum[SHA256_DIGEST_SIZE];

    memset(shaSum, 0, SHA256_DIGEST_SIZE);
    mbedtls_sha256(pk_der + pk_der_offset, pk_der_size_byte - pk_der_offset,
                   shaSum, 0 /* is224 */);

#if 0
    printf_sgx("sha256 of server's public key:\n");
    for (unsigned int i=0; i < SHA256_DIGEST_SIZE; ++i) mbedtls_printf("%02x", shaSum[i]);
    mbedtls_printf("\n");

    mbedtls_printf("quote report data:\n");
    for (int i=0; i < SGX_REPORT_DATA_SIZE; ++i) mbedtls_printf("%02x", quote->report_body.report_data.d[i]);
    mbedtls_printf("\n");
#endif
    
    int ret;
    ret = memcmp(quote->report_body.report_data.d,
                 shaSum, SHA256_DIGEST_SIZE);
    assert(ret == 0);
    
#if 0
    if (ret == 0) mbedtls_printf("hash of server's public key matches matches report data\n");
#endif
    
    return ret;
}

static
int verify_ias_report_signature
(
    attestation_verification_report_t* attn_report
)
{
    // Create certificate structure
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    int ret;
    ret = mbedtls_x509_crt_parse(&cert,
                                 attn_report->ias_sign_cert,
                                 attn_report->ias_sign_cert_len);
    assert(ret == 0);

    // Extract RSA public key
    assert(cert.pk.pk_info->type == MBEDTLS_PK_RSA);
    mbedtls_rsa_context* rsa = (mbedtls_rsa_context*) cert.pk.pk_ctx;

    // Compute signature
    uint8_t sha256[32];
    ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                     attn_report->ias_report,
                     attn_report->ias_report_len,
                     sha256);
    assert(ret == 0);

    // Verify signature
    ret = mbedtls_rsa_pkcs1_verify(rsa, NULL, NULL, MBEDTLS_RSA_PUBLIC,
                                   MBEDTLS_MD_SHA256, 0,
                                   sha256,
                                   attn_report->ias_report_signature);
    return ret;
}

static
int verify_ias_certificate_chain(attestation_verification_report_t* attn_report) {

    mbedtls_x509_crt cacert;
    mbedtls_x509_crt_init(&cacert);
    int ret;
    ret = mbedtls_x509_crt_parse(&cacert,
                                 attn_report->ias_sign_ca_cert,
                                 attn_report->ias_sign_ca_cert_len);
    assert(ret == 0);

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse(&cert,
                                 attn_report->ias_sign_cert,
                                 attn_report->ias_sign_cert_len);
    assert(ret == 0);

    uint32_t flags;
    ret = mbedtls_x509_crt_verify(&cert, &cacert, NULL, NULL, &flags,
                                  NULL, NULL);
    assert(ret == 0);
    
    return ret;
}

/**
 * @return 0 if verified successfully, 1 otherwise.
 */
int verify_sgx_cert_extensions
(
    const uint8_t* der_crt,
    uint32_t der_crt_len
)
{
    attestation_verification_report_t attn_report;

    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    
    int ret;
    ret = mbedtls_x509_crt_parse(&crt, der_crt, der_crt_len);
    
    extract_x509_extensions(crt.v3_ext.p,
                            crt.v3_ext.len,
                            &attn_report);

    ret = verify_ias_certificate_chain(&attn_report);
    assert(ret == 0);

    ret = verify_ias_report_signature(&attn_report);
    assert(ret == 0);

    sgx_quote_t quote = {0, };
    get_quote_from_report(attn_report.ias_report,
                          attn_report.ias_report_len,
                          &quote);
    ret = verify_report_data_against_server_cert(&crt, &quote);
    assert(ret == 0);

    mbedtls_x509_crt_free(&crt);

    return 0;
}
