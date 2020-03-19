/* Copyright (C) 2020 Invisible Things Lab
                      Rafal Wojdyla <omeg@invisiblethingslab.com>
   This file is part of Graphene Library OS.
   Graphene Library OS is free software: you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.
   Graphene Library OS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.
   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include "ias.h"
#include "sgx_arch.h"
#include "sgx_attest.h"
#include "util.h"

/** Default base URL for IAS API endpoints. Remove "/dev" for production environment. */
#define IAS_URL_BASE "https://api.trustedservices.intel.com/sgx/dev"

/** Default URL for IAS "verify attestation evidence" API endpoint. */
#define IAS_URL_REPORT IAS_URL_BASE "/attestation/v3/report"

/** Default URL for IAS "Retrieve SigRL" API endpoint. EPID group id is added at the end. */
#define IAS_URL_SIGRL IAS_URL_BASE "/attestation/v3/sigrl"

struct option g_options[] = {
    { "help", no_argument, 0, 'h' },
    { "verbose", no_argument, 0, 'v' },
    { "quote-path", required_argument, 0, 'q' },
    { "api-key", required_argument, 0, 'k' },
    { "nonce", required_argument, 0, 'n' },
    { "report-path", required_argument, 0, 'r' },
    { "sig-path", required_argument, 0, 's' },
    { "cert-path", required_argument, 0, 'c' },
    { "advisory-path", required_argument, 0, 'a' },
    { "gid", required_argument, 0, 'g' },
    { "sigrl-path", required_argument, 0, 'i' },
    { "report-url", required_argument, 0, 'R' },
    { "sigrl-url", required_argument, 0, 'S' },
    { 0, 0, 0, 0 }
};

void usage(const char* exec) {
    printf("Usage: %s <request> [options]\n", exec);
    printf("Available requests:\n");
    printf("  sigrl                     Retrieve signature revocation list for a given EPID group\n");
    printf("  report                    Verify attestation evidence (quote)\n");
    printf("Available general options:\n");
    printf("  --help, -h                Display this help\n");
    printf("  --verbose, -v             Enable verbose output\n");
    printf("  --api-key, -k STRING      IAS API key\n");
    printf("Available sigrl options:\n");
    printf("  --gid, -g STRING          EPID group ID (hex string)\n");
    printf("  --sigrl-path, -i PATH     Path to save SigRL to\n");
    printf("  --sigrl-url, -S URL       URL for the IAS SigRL endpoint (default:\n"
           "                            %s)\n", IAS_URL_SIGRL);
    printf("Available report options:\n");
    printf("  --quote-path, -q PATH     Path to quote to submit\n");
    printf("  --nonce, -n STRING        Nonce to use (optional)\n");
    printf("  --report-path, -r PATH    Path to save IAS report to\n");
    printf("  --sig-path, -s PATH       Path to save IAS report's signature to (optional)\n");
    printf("  --cert-path, -c PATH      Path to save IAS certificate to (optional)\n");
    printf("  --advisory-path, -a PATH  Path to save IAS advisories to (optional)\n");
    printf("  --report-url, -R URL      URL for the IAS attestation report endpoint (default:\n"
           "                            %s)\n", IAS_URL_REPORT);
}

int report(struct ias_context_t* ias, const char* quote_path, const char* nonce,
           const char* report_path, const char* sig_path, const char* cert_path,
           const char* advisory_path) {
    int ret          = -1;
    void* quote_data = NULL;

    if (!quote_path) {
        ERROR("Quote path not specified\n");
        goto out;
    }

    ssize_t quote_size;
    quote_data = read_file(quote_path, &quote_size);
    if (!quote_data) {
        ERROR("Failed to read quote file '%s'\n", quote_path);
        goto out;
    }

    if (quote_size < sizeof(sgx_quote_t)) {
        ERROR("Quote is too small\n");
        goto out;
    }

    sgx_quote_t* quote = (sgx_quote_t*)quote_data;
    if (quote_size < sizeof(sgx_quote_t) + quote->signature_len) {
        ERROR("Quote is too small\n");
        goto out;
    }
    quote_size = sizeof(sgx_quote_t) + quote->signature_len;

    ret = ias_verify_quote(ias, quote_data, quote_size, nonce, report_path, sig_path, cert_path,
                           advisory_path);
    if (ret != 0) {
        ERROR("Failed to submit quote to IAS\n");
        goto out;
    }

    printf("IAS submission successful\n");
out:
    free(quote_data);
    return ret;
}

int sigrl(struct ias_context_t* ias, const char* gid_str, const char* sigrl_path) {
    uint8_t gid[4];
    if (!gid_str || strlen(gid_str) != 8) {
        ERROR("Invalid EPID group ID\n");
        return -1;
    }

    for (size_t i=0; i<4; i++) {
        if (!isxdigit(gid_str[i * 2]) || !isxdigit(gid_str[i * 2 + 1])) {
            ERROR("Invalid EPID group ID\n");
            return -1;
        }
        sscanf(gid_str + i * 2, "%02hhx", &gid[i]);
    }

    size_t sigrl_size = 0;
    void* sigrl = NULL;
    int ret = ias_get_sigrl(ias, gid, &sigrl_size, &sigrl);
    if (ret == 0) {
        if (sigrl_size == 0) {
            printf("No SigRL for given EPID group ID %s\n", gid_str);
        } else {
            printf("SigRL size: %zu\n", sigrl_size);
            ret = write_file(sigrl_path, sigrl_size, sigrl);
        }
    }
    free(sigrl);
    return ret;
}

int main(int argc, char* argv[]) {
    int option          = 0;
    char* mode          = NULL;
    char* quote_path    = NULL;
    char* api_key       = NULL;
    char* nonce         = NULL;
    char* report_path   = NULL;
    char* sig_path      = NULL;
    char* cert_path     = NULL;
    char* advisory_path = NULL;
    char* gid           = NULL;
    char* sigrl_path    = NULL;
    char* report_url    = IAS_URL_REPORT;
    char* sigrl_url     = IAS_URL_SIGRL;

    // parse command line
    while (true) {
        option = getopt_long(argc, argv, "hvq:k:n:r:s:c:a:g:i:R:S:", g_options, NULL);
        if (option == -1)
            break;

        switch (option) {
            case 'h':
                usage(argv[0]);
                return 0;
            case 'v':
                set_verbose(true);
                break;
            case 'q':
                quote_path = optarg;
                break;
            case 'k':
                api_key = optarg;
                break;
            case 'n':
                nonce = optarg;
                break;
            case 'r':
                report_path = optarg;
                break;
            case 's':
                sig_path = optarg;
                break;
            case 'c':
                cert_path = optarg;
                break;
            case 'a':
                advisory_path = optarg;
                break;
            case 'g':
                gid = optarg;
                break;
            case 'i':
                sigrl_path = optarg;
                break;
            case 'R':
                report_url = optarg;
                break;
            case 'S':
                sigrl_url = optarg;
                break;
            default:
                usage(argv[0]);
                return -1;
        }
    }

    if (optind >= argc) {
        ERROR("Request not specified\n");
        usage(argv[0]);
        return -1;
    }

    mode = argv[optind++]; 

    if (!api_key) {
        ERROR("API key not specified\n");
        return -1;
    }

    struct ias_context_t* ias = ias_init(api_key, report_url, sigrl_url);
    if (!ias) {
        ERROR("Failed to initialize IAS library\n");
        return -1;
    }

    if (mode[0] == 'r') {
        if (!report_path) {
            ERROR("Report path not specified\n");
            return -1;
        }
        return report(ias, quote_path, nonce, report_path, sig_path, cert_path, advisory_path);
    } else if (mode[0] == 's') {
        if (!sigrl_path) {
            ERROR("SigRL path not specified\n");
            return -1;
        }
        return sigrl(ias, gid, sigrl_path);
    }

    ERROR("Invalid request\n");
    usage(argv[0]);
    return -1;
}