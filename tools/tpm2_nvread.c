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

#include <stdbool.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <sapi/tpm20.h>

#include "../lib/tpm2_util.h"
#include "log.h"
#include "main.h"
#include "options.h"
#include "password_util.h"

typedef struct tpm_nvread_ctx tpm_nvread_ctx;
struct tpm_nvread_ctx {
    UINT32 nv_index;
    UINT32 auth_handle;
    UINT32 size_to_read;
    UINT32 offset;
    TPM2B_AUTH handle_passwd;
    bool is_hex_password;
    TSS2_SYS_CONTEXT *sapi_context;
    bool is_auth_session;
    TPMI_SH_AUTH_SESSION auth_session_handle;
};

static bool nv_read(tpm_nvread_ctx *ctx) {

    TPMS_AUTH_COMMAND session_data = TPMS_AUTH_COMMAND_EMPTY_INIT;
    TPMS_AUTH_RESPONSE session_data_out;
    TSS2_SYS_CMD_AUTHS sessions_data;
    TSS2_SYS_RSP_AUTHS sessions_data_out;

    TPM2B_MAX_NV_BUFFER nv_data = TPM2B_TYPE_INIT(TPM2B_MAX_NV_BUFFER, buffer);

    TPMS_AUTH_COMMAND *session_data_array[1];
    TPMS_AUTH_RESPONSE *session_data_out_array[1];

    session_data_array[0] = &session_data;
    session_data_out_array[0] = &session_data_out;

    sessions_data_out.rspAuths = &session_data_out_array[0];
    sessions_data.cmdAuths = &session_data_array[0];

    sessions_data_out.rspAuthsCount = 1;
    sessions_data.cmdAuthsCount = 1;

    session_data.sessionHandle = TPM_RS_PW;
    if (ctx->is_auth_session) {
        session_data.sessionHandle = ctx->auth_session_handle;
    }

    bool result = password_tpm2_util_to_auth(&ctx->handle_passwd, ctx->is_hex_password,
            "handle password", &session_data.hmac);
    if (!result) {
        return false;
    }

    TPM_RC rval = Tss2_Sys_NV_Read(ctx->sapi_context, ctx->auth_handle, ctx->nv_index,
            &sessions_data, ctx->size_to_read, ctx->offset, &nv_data, &sessions_data_out);
    if (rval != TPM_RC_SUCCESS) {
        LOG_ERR("Failed to read NVRAM area at index 0x%x (%d). Error:0x%x",
                ctx->nv_index, ctx->nv_index, rval);
        return false;
    }

    printf("\nThe size of data:%d\n", nv_data.t.size);
    int i;
    for (i = 0; i < nv_data.t.size; i++)
        printf(" %2.2x ", nv_data.t.buffer[i]);
    printf("\n");

    return true;
}

static bool init(int argc, char *argv[], tpm_nvread_ctx *ctx) {

    struct option long_options[] = {
        { "index"       , required_argument, NULL, 'x' },
        { "authHandle"  , required_argument, NULL, 'a' },
        { "size"        , required_argument, NULL, 's' },
        { "offset"      , required_argument, NULL, 'o' },
        { "handlePasswd", required_argument, NULL, 'P' },
        { "passwdInHex" , no_argument,       NULL, 'X' },
        { "input-session-handle",1,          NULL, 'S' },
        { NULL          , no_argument,       NULL, 0   },
    };

    int opt;
    bool result;
    while ((opt = getopt_long(argc, argv, "x:a:s:o:P:S:X", long_options, NULL))
            != -1) {
        switch (opt) {
        case 'x':
            result = tpm2_util_string_to_uint32(optarg, &ctx->nv_index);
            if (!result) {
                LOG_ERR("Could not convert NV index to number, got: \"%s\"",
                        optarg);
                return false;
            }

            if (ctx->nv_index == 0) {
                LOG_ERR("NV Index cannot be 0");
                return false;
            }

            break;
        case 'a':
            result = tpm2_util_string_to_uint32(optarg, &ctx->auth_handle);
            if (!result) {
                LOG_ERR("Could not convert auth handle to number, got: \"%s\"",
                        optarg);
                return false;
            }

            if (ctx->auth_handle == 0) {
                LOG_ERR("Auth handle cannot be 0");
                return false;
            }
            break;
        case 'P':
            result = password_tpm2_util_copy_password(optarg, "handle password",
                    &ctx->handle_passwd);
            if (!result) {
                return false;
            }
            break;
        case 's':
            result = tpm2_util_string_to_uint32(optarg, &ctx->size_to_read);
            if (!result) {
                LOG_ERR("Could not convert size to number, got: \"%s\"",
                        optarg);
                return false;
            }
            break;
        case 'o':
            result = tpm2_util_string_to_uint32(optarg, &ctx->offset);
            if (!result) {
                LOG_ERR("Could not convert offset to number, got: \"%s\"",
                        optarg);
                return false;
            }
            break;
        case 'X':
            ctx->is_hex_password = true;
            break;
        case 'S':
             if (!tpm2_util_string_to_uint32(optarg, &ctx->auth_session_handle)) {
                 LOG_ERR("Could not convert session handle to number, got: \"%s\"",
                         optarg);
                 return false;
             }
             ctx->is_auth_session = true;
             break;
        case ':':
            LOG_ERR("Argument %c needs a value!\n", optopt);
            return false;
        case '?':
            LOG_ERR("Unknown Argument: %c\n", optopt);
            return false;
        default:
            LOG_ERR("?? getopt returned character code 0%o ??\n", opt);
            return false;
        }
    }
    return true;
}

int execute_tool(int argc, char *argv[], char *envp[], common_opts_t *opts,
        TSS2_SYS_CONTEXT *sapi_context) {

    (void)opts;
    (void)envp;

    tpm_nvread_ctx ctx = {
            .nv_index = 0,
            .auth_handle = TPM_RH_PLATFORM,
            .size_to_read = 0,
            .offset = 0,
            .handle_passwd = TPM2B_EMPTY_INIT,
            .is_hex_password = false,
            .sapi_context = sapi_context,
            .is_auth_session = false
    };

    bool result = init(argc, argv, &ctx);
    if (!result) {
        return 1;
    }

    return nv_read(&ctx) != true;
}
