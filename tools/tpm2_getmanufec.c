//**********************************************************************;
// Copyright (c) 2015, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of Intel Corporation nor the names of its contributors
// may be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//**********************************************************************;
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <getopt.h>
#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sapi/tpm20.h>

#include "../lib/tpm2_util.h"
#include "log.h"
#include "files.h"
#include "main.h"
#include "options.h"
#include "tpm_hash.h"

char *outputFile;
char *ownerPasswd;
char *endorsePasswd;
char *ekPasswd;
bool hexPasswd = false;
TPM_HANDLE persistentHandle;
UINT32 algorithmType = TPM_ALG_RSA;

char *ECcertFile;
char *EKserverAddr = NULL;
unsigned int nonPersistentRead = 0;
unsigned int SSL_NO_VERIFY = 0;
unsigned int OfflineProv = 0;
bool is_session_based_auth = false;
TPMI_SH_AUTH_SESSION auth_session_handle = 0;


BYTE authPolicy[] = {0x83, 0x71, 0x97, 0x67, 0x44, 0x84, 0xB3, 0xF8,
                     0x1A, 0x90, 0xCC, 0x8D, 0x46, 0xA5, 0xD7, 0x24,
                     0xFD, 0x52, 0xD7, 0x6E, 0x06, 0x52, 0x0B, 0x64,
                     0xF2, 0xA1, 0xDA, 0x1B, 0x33, 0x14, 0x69, 0xAA};
int setKeyAlgorithm(UINT16 algorithm, TPM2B_PUBLIC *inPublic)
{
    inPublic->t.publicArea.nameAlg = TPM_ALG_SHA256;
    // First clear attributes bit field.
    *(UINT32 *)&(inPublic->t.publicArea.objectAttributes) = 0;
    inPublic->t.publicArea.objectAttributes.restricted = 1;
    inPublic->t.publicArea.objectAttributes.userWithAuth = 0;
    inPublic->t.publicArea.objectAttributes.adminWithPolicy = 1;
    inPublic->t.publicArea.objectAttributes.sign = 0;
    inPublic->t.publicArea.objectAttributes.decrypt = 1;
    inPublic->t.publicArea.objectAttributes.fixedTPM = 1;
    inPublic->t.publicArea.objectAttributes.fixedParent = 1;
    inPublic->t.publicArea.objectAttributes.sensitiveDataOrigin = 1;
    inPublic->t.publicArea.authPolicy.t.size = 32;
    memcpy(inPublic->t.publicArea.authPolicy.t.buffer, authPolicy, 32);

    inPublic->t.publicArea.type = algorithm;

    switch (algorithm)
    {
    case TPM_ALG_RSA:
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.algorithm = TPM_ALG_AES;
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.keyBits.aes = 128;
        inPublic->t.publicArea.parameters.rsaDetail.symmetric.mode.aes = TPM_ALG_CFB;
        inPublic->t.publicArea.parameters.rsaDetail.scheme.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.parameters.rsaDetail.keyBits = 2048;
        inPublic->t.publicArea.parameters.rsaDetail.exponent = 0x0;
        inPublic->t.publicArea.unique.rsa.t.size = 256;
        break;
    case TPM_ALG_KEYEDHASH:
        inPublic->t.publicArea.parameters.keyedHashDetail.scheme.scheme = TPM_ALG_XOR;
        inPublic->t.publicArea.parameters.keyedHashDetail.scheme.details.exclusiveOr.hashAlg = TPM_ALG_SHA256;
        inPublic->t.publicArea.parameters.keyedHashDetail.scheme.details.exclusiveOr.kdf = TPM_ALG_KDF1_SP800_108;
        inPublic->t.publicArea.unique.keyedHash.t.size = 0;
        break;
    case TPM_ALG_ECC:
        inPublic->t.publicArea.parameters.eccDetail.symmetric.algorithm = TPM_ALG_AES;
        inPublic->t.publicArea.parameters.eccDetail.symmetric.keyBits.aes = 128;
        inPublic->t.publicArea.parameters.eccDetail.symmetric.mode.sym = TPM_ALG_CFB;
        inPublic->t.publicArea.parameters.eccDetail.scheme.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.parameters.eccDetail.curveID = TPM_ECC_NIST_P256;
        inPublic->t.publicArea.parameters.eccDetail.kdf.scheme = TPM_ALG_NULL;
        inPublic->t.publicArea.unique.ecc.x.t.size = 32;
        inPublic->t.publicArea.unique.ecc.y.t.size = 32;
        break;
    case TPM_ALG_SYMCIPHER:
        inPublic->t.publicArea.parameters.symDetail.sym.algorithm = TPM_ALG_AES;
        inPublic->t.publicArea.parameters.symDetail.sym.keyBits.aes = 128;
        inPublic->t.publicArea.parameters.symDetail.sym.mode.sym = TPM_ALG_CFB;
        inPublic->t.publicArea.unique.sym.t.size = 0;
        break;
    default:
        printf("\nThe algorithm type input(%4.4x) is not supported!\n", algorithm);
        return -1;
    }

    return 0;
}

int createEKHandle(TSS2_SYS_CONTEXT *sapi_context)
{
    UINT32 rval;
    TPMS_AUTH_COMMAND sessionData = {
            .sessionHandle = TPM_RS_PW,
            .nonce = TPM2B_EMPTY_INIT,
            .hmac = TPM2B_EMPTY_INIT,
            .sessionAttributes = SESSION_ATTRIBUTES_INIT(0),
    };
    if (is_session_based_auth) {
        sessionData.sessionHandle = auth_session_handle;
    }
    TPMS_AUTH_RESPONSE sessionDataOut;
    TSS2_SYS_CMD_AUTHS sessionsData;
    TSS2_SYS_RSP_AUTHS sessionsDataOut;
    TPMS_AUTH_COMMAND *sessionDataArray[1];
    TPMS_AUTH_RESPONSE *sessionDataOutArray[1];

    TPM2B_SENSITIVE_CREATE inSensitive = TPM2B_TYPE_INIT(TPM2B_SENSITIVE_CREATE, sensitive);
    TPM2B_PUBLIC inPublic = TPM2B_TYPE_INIT(TPM2B_PUBLIC, publicArea);

    TPM2B_DATA outsideInfo = TPM2B_EMPTY_INIT;
    TPML_PCR_SELECTION creationPCR;

    TPM2B_NAME name = TPM2B_TYPE_INIT(TPM2B_NAME, name);

    TPM2B_PUBLIC outPublic = TPM2B_EMPTY_INIT;
    TPM2B_CREATION_DATA creationData = TPM2B_EMPTY_INIT;
    TPM2B_DIGEST creationHash = TPM2B_TYPE_INIT(TPM2B_DIGEST, buffer);
    TPMT_TK_CREATION creationTicket = TPMT_TK_CREATION_EMPTY_INIT;

    TPM_HANDLE handle2048ek;

    sessionDataArray[0] = &sessionData;
    sessionDataOutArray[0] = &sessionDataOut;

    sessionsDataOut.rspAuths = &sessionDataOutArray[0];
    sessionsData.cmdAuths = &sessionDataArray[0];

    sessionsDataOut.rspAuthsCount = 1;
    sessionsData.cmdAuthsCount = 1;

    /*
     * use enAuth in Tss2_Sys_CreatePrimary
     */
    if (strlen(endorsePasswd) > 0 && !hexPasswd) {
            sessionData.hmac.t.size = strlen(endorsePasswd);
            memcpy( &sessionData.hmac.t.buffer[0], endorsePasswd, sessionData.hmac.t.size );
    }
    else {
        if (strlen(endorsePasswd) > 0 && hexPasswd) {
                sessionData.hmac.t.size = sizeof(sessionData.hmac) - 2;

                if (tpm2_util_hex_to_byte_structure(endorsePasswd, &sessionData.hmac.t.size,
                                      sessionData.hmac.t.buffer) != 0) {
                        printf( "Failed to convert Hex format password for endorsePasswd.\n");
                        return -1;
                }
        }
    }

    if (strlen(ekPasswd) > 0 && !hexPasswd) {
        inSensitive.t.sensitive.userAuth.t.size = strlen(ekPasswd);
        memcpy( &inSensitive.t.sensitive.userAuth.t.buffer[0], ekPasswd,
                inSensitive.t.sensitive.userAuth.t.size );
    }
    else {
        if (strlen(ekPasswd) > 0 && hexPasswd) {
             inSensitive.t.sensitive.userAuth.t.size = sizeof(inSensitive.t.sensitive.userAuth) - 2;
             if (tpm2_util_hex_to_byte_structure(ekPasswd, &inSensitive.t.sensitive.userAuth.t.size,
                                   inSensitive.t.sensitive.userAuth.t.buffer) != 0) {
                  printf( "Failed to convert Hex format password for ekPasswd.\n");
                  return -1;
            }
        }
    }

    inSensitive.t.sensitive.data.t.size = 0;
    inSensitive.t.size = inSensitive.t.sensitive.userAuth.b.size + 2;

    if (setKeyAlgorithm(algorithmType, &inPublic) )
          return -1;

    creationPCR.count = 0;

    rval = Tss2_Sys_CreatePrimary(sapi_context, TPM_RH_ENDORSEMENT, &sessionsData,
                                  &inSensitive, &inPublic, &outsideInfo,
                                  &creationPCR, &handle2048ek, &outPublic,
                                  &creationData, &creationHash, &creationTicket,
                                  &name, &sessionsDataOut);
    if (rval != TPM_RC_SUCCESS ) {
          printf("\nTPM2_CreatePrimary Error. TPM Error:0x%x\n", rval);
          return -2;
    }
    printf("\nEK create succ.. Handle: 0x%8.8x\n", handle2048ek);

    if (!nonPersistentRead) {
         /*
          * To make EK persistent, use own auth
          */
         sessionData.hmac.t.size = 0;
         if (strlen(ownerPasswd) > 0 && !hexPasswd) {
             sessionData.hmac.t.size = strlen(ownerPasswd);
             memcpy( &sessionData.hmac.t.buffer[0], ownerPasswd, sessionData.hmac.t.size );
         }
         else {
            if (strlen(ownerPasswd) > 0 && hexPasswd) {
                sessionData.hmac.t.size = sizeof(sessionData.hmac) - 2;
                if (tpm2_util_hex_to_byte_structure(ownerPasswd, &sessionData.hmac.t.size,
                                   sessionData.hmac.t.buffer) != 0) {
                 printf( "Failed to convert Hex format password for ownerPasswd.\n");
                 return -1;
                }
            }
        }

        rval = Tss2_Sys_EvictControl(sapi_context, TPM_RH_OWNER, handle2048ek,
                                     &sessionsData, persistentHandle, &sessionsDataOut);
        if (rval != TPM_RC_SUCCESS ) {
            printf("\nEvictControl:Make EK persistent Error. TPM Error:0x%x\n", rval);
            return -3;
        }
        printf("EvictControl EK persistent succ.\n");
    }

    rval = Tss2_Sys_FlushContext(sapi_context,
                                 handle2048ek);
    if (rval != TPM_RC_SUCCESS ) {
        printf("\nFlush transient EK failed. TPM Error:0x%x\n", rval);
        return -4;
    }

    printf("Flush transient EK succ.\n");

    /* TODO this serialization is not correct */
    if (!files_save_bytes_to_file(outputFile, (UINT8 *)&outPublic, sizeof(outPublic))) {
        printf("\nFailed to save EK pub key into file(%s)\n", outputFile);
        return -5;
    }

    return 0;
}

unsigned char *HashEKPublicKey(void)
{
    unsigned char *hash = NULL;
    FILE *fp = NULL;

    unsigned char EKpubKey[259];

    printf("Calculating the SHA256 hash of the Endorsement Public Key\n");

    fp = fopen(outputFile, "rb");
    if (!fp) {
        LOG_ERR("Could not open file: \"%s\"", outputFile);
        return NULL;
    }

    int rc = fseek(fp, 0x66, 0);
    if (rc < 0) {
        LOG_ERR("Could not perform fseek: %s\n", strerror(errno));
        goto out;
    }

    size_t read = fread(EKpubKey, 1, 256, fp);
    if (read != 256) {
        LOG_ERR ("Could not read whole file.");
        goto out;
    }

    hash = (unsigned char*)malloc(SHA256_DIGEST_LENGTH);
    if (hash == NULL) {
        LOG_ERR ("OOM");
        goto out;
    }

    EKpubKey[256] = 0x01;
    EKpubKey[257] = 0x00;
    EKpubKey[258] = 0x01; //Exponent
    SHA256_CTX sha256;
    int is_success = SHA256_Init(&sha256);
    if (!is_success) {
        LOG_ERR ("SHA256_Init failed");
        goto hash_out;
    }

    is_success = SHA256_Update(&sha256, EKpubKey, sizeof(EKpubKey));
    if (!is_success) {
        LOG_ERR ("SHA256_Update failed");
        goto hash_out;
    }

    is_success = SHA256_Final(hash, &sha256);
    if (!is_success) {
        LOG_ERR ("SHA256_Final failed");
        goto hash_out;
    }

    unsigned i;
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        printf("%02X", hash[i]);
    }
    printf("\n");
    goto out;

hash_out:
    free(hash);
    hash = NULL;
out:
    fclose(fp);
    return hash;
}

char *Base64Encode(const unsigned char* buffer)
{
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    printf("Calculating the Base64Encode of the hash of the Endorsement Public Key:\n");

    if (buffer == NULL) {
        LOG_ERR("HashEKPublicKey returned null");
        return NULL;
    }

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, buffer, SHA256_DIGEST_LENGTH);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    BIO_set_close(bio, BIO_NOCLOSE);

    /* these are not NULL terminated */
    char *b64text = bufferPtr->data;
    size_t len = bufferPtr->length;

    size_t i;
    for (i = 0; i < len; i++) {
        if (b64text[i] == '+') {
            b64text[i] = '-';
        }
        if (b64text[i] == '/') {
            b64text[i] = '_';
        }
    }

    char *final_string = NULL;

    CURL *curl = curl_easy_init();
    if (curl) {
        char *output = curl_easy_escape(curl, b64text, len);
        if (output) {
            final_string = strdup(output);
            curl_free(output);
        }
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    BIO_free_all(bio);

    /* format to a proper NULL terminated string */
    return final_string;
}

int RetrieveEndorsementCredentials(char *b64h)
{
    int ret = -1;

    size_t len = 1 + strlen(b64h) + strlen(EKserverAddr);
    char *weblink = (char *)malloc(len);
    if (!weblink) {
        LOG_ERR("Could not open file for writing: \"%s\"", ECcertFile);
                return ret;
    }

    snprintf(weblink, len, "%s%s", EKserverAddr, b64h);

    FILE * respfile = fopen(ECcertFile, "wb");
    if (!respfile) {
        LOG_ERR("Could not open file for writing: \"%s\"", ECcertFile);
                goto out_memory;
    }

    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_global_init failed: %s", curl_easy_strerror(rc));
        goto out_file_close;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERR("curl_easy_init failed");
        goto out_global_cleanup;
    }

    /*
     * should not be used - Used only on platforms with older CA certificates.
     */
    if (SSL_NO_VERIFY) {
        rc = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        if (rc != CURLE_OK) {
            LOG_ERR("curl_easy_setopt for CURLOPT_SSL_VERIFYPEER failed: %s", curl_easy_strerror(rc));
            goto out_easy_cleanup;
        }
    }

    rc = curl_easy_setopt(curl, CURLOPT_URL, weblink);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_setopt for CURLOPT_URL failed: %s", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    rc = curl_easy_setopt(curl, CURLOPT_VERBOSE, respfile);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_setopt for CURLOPT_VERBOSE failed: %s", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, respfile);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_setopt for CURLOPT_WRITEDATA failed: %s", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        LOG_ERR("curl_easy_perform() failed: %s\n", curl_easy_strerror(rc));
        goto out_easy_cleanup;
    }

    ret = 0;

out_easy_cleanup:
    curl_easy_cleanup(curl);
out_global_cleanup:
    curl_global_cleanup();
out_file_close:
    fclose(respfile);
out_memory:
    free(weblink);

    return ret;
}


int TPMinitialProvisioning(void)
{
    if (EKserverAddr == NULL) {
        printf("TPM Manufacturer Endorsement Credential Server Address cannot be NULL\n");
        return -99;
    }

    char *b64 = Base64Encode(HashEKPublicKey());
    if (!b64) {
        LOG_ERR("Base64Encode returned null");
        return -1;
    }

    printf("%s\n", b64);

    int rc = RetrieveEndorsementCredentials(b64);
    free(b64);
    return rc;
}

int execute_tool (int argc, char *argv[], char *envp[], common_opts_t *opts,
                  TSS2_SYS_CONTEXT *sapi_context)
{
    (void) opts;
    (void) envp;

    int return_val = 1;

    static const char *optstring = "e:o:H:P:g:f:X:N:O:E:S:i:U";

    static struct option long_options[] =
    {
        { "endorsePasswd", 1, NULL, 'e' },
        { "ownerPasswd"  , 1, NULL, 'o' },
        { "handle"       , 1, NULL, 'H' },
        { "ekPasswd"     , 1, NULL, 'P' },
        { "alg"          , 1, NULL, 'g' },
        { "file"         , 1, NULL, 'f' },
        { "passwdInHex"  , 0, NULL, 'X' },
        { "NonPersistent", 0, NULL, 'N' },
        { "OfflineProv"  , 0, NULL, 'O' },
        { "ECcertFile"   , 1, NULL, 'E' },
        { "EKserverAddr" , 1, NULL, 'S' },
        { "SSL_NO_VERIFY", 0, NULL, 'U' },
        {"input-session-handle",1,NULL,'i'},
        { NULL           , 0, NULL,  0  },
    };

    if (argc > (int)(2 * sizeof(long_options) / sizeof(struct option)) ) {
        showArgMismatch(argv[0]);
        return -1;
    }

    int opt;
    while ( ( opt = getopt_long( argc, argv, optstring, long_options, NULL ) ) != -1 ) {
              switch ( opt ) {
                case 'H':
                    if (!tpm2_util_string_to_uint32(optarg, &persistentHandle)) {
                        printf("\nPlease input the handle used to make EK persistent(hex) in correct format.\n");
                        return return_val;
                    }
                    break;

                case 'e':
                    if (optarg == NULL || (strlen(optarg) >= sizeof(TPMU_HA)) ) {
                        printf("\nPlease input the endorsement password(optional,no more than %d characters).\n", (int)sizeof(TPMU_HA) - 1);
                        return return_val;
                    }
                    endorsePasswd = optarg;
                    break;

                case 'o':
                    if (optarg == NULL || (strlen(optarg) >= sizeof(TPMU_HA)) ) {
                        printf("\nPlease input the owner password(optional,no more than %d characters).\n", (int)sizeof(TPMU_HA) - 1);
                        return return_val;
                    }
                    ownerPasswd = optarg;
                    break;

                case 'P':
                    if (optarg == NULL || (strlen(optarg) >= sizeof(TPMU_HA)) ) {
                        printf("\nPlease input the EK password(optional,no more than %d characters).\n", (int)sizeof(TPMU_HA) - 1);
                        return return_val;
                    }
                    ekPasswd = optarg;
                    break;

                case 'g':
                    if (!tpm2_util_string_to_uint32(optarg, &algorithmType)) {
                        printf("\nPlease input the algorithm type in correct format.\n");
                        return return_val;
                    }
                    break;

                case 'f':
                    if (optarg == NULL ) {
                        printf("\nPlease input the file used to save the pub ek.\n");
                        return return_val;
                    }
                    outputFile = optarg;
                    break;

                case 'X':
                    hexPasswd = true;
                    break;

                case 'E':
                    if (optarg == NULL ) {
                        printf("\nPlease input the file used to save the EC Certificate retrieved from server\n");
                        return return_val;
                    }
                    ECcertFile = optarg;
                    break;
                case 'N':
                    nonPersistentRead = 1;
                    printf("Tss2_Sys_CreatePrimary called with Endorsement Handle without making it persistent\n");
                    break;
                case 'O':
                    OfflineProv = 1;
                    printf("Setting up for offline provisioning - reading the retrieved EK specified by the file \n");
                    break;
                case 'U':
                    SSL_NO_VERIFY = 1;
                    printf("CAUTION: TLS communication with the said TPM manufacturer server setup with SSL_NO_VERIFY!\n");
                    break;
                case 'S':
                    if (EKserverAddr) {
                        printf("Multiple specifications of -S\n");
                        return return_val;
                    }
                    EKserverAddr = optarg;
                    printf("TPM Manufacturer EK provisioning address -- %s\n", EKserverAddr);
                    break;
                case 'i':
                    return_val = tpm2_util_string_to_uint32(optarg, &auth_session_handle);
                    if (!return_val) {
                        LOG_ERR("Could not convert session handle to number, got: \"%s\"",
                                optarg);
                        return return_val;
                    }
                    is_session_based_auth = true;
                    break;
                default:
                    LOG_ERR("Unknown option\n");
                    return 1;
            }
    }

    int provisioning_return_val = 0;
    if (argc < 2) {
        showArgMismatch(argv[0]);
        return -1;
    }
    else {
        if (!OfflineProv) {
            return_val  = createEKHandle(sapi_context);
        }
        provisioning_return_val = TPMinitialProvisioning();
    }

    if (return_val && provisioning_return_val) {
        return return_val;
    }

    return 0;
}
