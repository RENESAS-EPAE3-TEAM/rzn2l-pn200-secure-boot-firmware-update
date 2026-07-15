#include "secure_app_verify.h"

#include "secure_app_package.h"
#include "secure_ssbl_config.h"
#include "sio_char.h"
#include <stdio.h>

#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
#include "hal_data.h"
#endif

#define SECURE_APP_CODE_CERT_MAGIC              (0x636F6463u)
#define SECURE_APP_CODE_CERT_VERSION            (0x00010000u)
#define SECURE_APP_CODE_CERT_FLAGS_NON_ENC      (0x00000000u)
#define SECURE_APP_FLASH_BANK_START             (0x60000000u)
#define SECURE_APP_FLASH_BANK_END_EXCLUSIVE     (0x64000000u)


#if SSBL_CFG_DEBUG_UART_ENABLE
#define SSBL_VERIFY(...) printf(__VA_ARGS__)
#else
#define SSBL_TRACE(...) ((void) 0)
#endif

typedef struct st_secure_app_code_cert_header
{
    uint32_t magic;
    uint32_t manifest_version;
    uint32_t flags;
    uint32_t load_addr;
    uint32_t dest_addr;
    uint32_t img_len;
    uint32_t img_version;
    uint32_t build_num;
} secure_app_code_cert_header_t;

static uint32_t g_secure_app_verify_last_fsp_error;
static uint32_t g_secure_app_verified_body_size;
volatile secure_app_verify_status_t g_secure_app_verify_last_status;

static const char * manifest_verify_error_string(fsp_err_t fsp_err)
{
    switch (fsp_err)
    {
        case FSP_SUCCESS:
        {
            return "success";
        }
        case FSP_ERR_SB_INTERNAL_FAIL:
        {
            return "SB internal failure";
        }
        case FSP_ERR_SB_INVALID_ARG:
        {
            return "SB invalid argument";
        }
        case FSP_ERR_SB_UNSUPPORTED_FUNCTION:
        {
            return "SB unsupported function";
        }
        case FSP_ERR_SB_INVALID_ALIGNMENT:
        {
            return "SB invalid alignment";
        }
        case FSP_ERR_SB_MANI_INVALID_MAGIC:
        {
            return "manifest invalid magic number";
        }
        case FSP_ERR_SB_MANI_UNSUPPORTED_VERSION:
        {
            return "manifest unsupported version";
        }
        case FSP_ERR_SB_MANI_OUT_OF_RANGE_LEN:
        {
            return "manifest out-of-range TLV length";
        }
        case FSP_ERR_SB_MANI_TLV_FIELD_ERR:
        {
            return "manifest missing required TLV field";
        }
        case FSP_ERR_SB_MANI_TLV_INVALID_LEN:
        {
            return "manifest TLV length exceeds manifest end";
        }
        case FSP_ERR_SB_MANI_INVALID_IMAGE_LEN:
        {
            return "manifest invalid image length";
        }
        case FSP_ERR_SB_MANI_MISMATCH_SIGN_ALGORITHM:
        {
            return "manifest mismatched signature algorithm";
        }
        case FSP_ERR_SB_MANI_UNSUPPORTED_ALGORITHM:
        {
            return "manifest unsupported algorithm";
        }
        case FSP_ERR_SB_CRYPTO_FAIL:
        {
            return "SB cryptographic processing failure";
        }
        case FSP_ERR_SB_CRYPTO_AUTH_FAIL:
        {
            return "SB verification failed";
        }
        case FSP_ERR_SB_CRYPTO_UNSUPPORTED_ALGORITHM:
        {
            return "SB crypto unsupported algorithm";
        }
        case FSP_ERR_SB_CRYPTO_RESOURCE_CONFLICT:
        {
            return "SB CryptoIP is in use";
        }
        case FSP_ERR_SB_CRYPTO_PARAM_ERR:
        {
            return "SB crypto parameter error";
        }
        default:
        {
            return "unknown ManifestVerify error";
        }
    }
}

static void verify_status_set(secure_app_verify_result_t result,
                              secure_app_verify_stage_t stage,
                              uint32_t fsp_error)
{
    g_secure_app_verify_last_status.result         = (uint32_t) result;
    g_secure_app_verify_last_status.stage          = (uint32_t) stage;
    g_secure_app_verify_last_status.fsp_error      = fsp_error;
    g_secure_app_verify_last_status.key_cert_addr  = (uint32_t)(uintptr_t) secure_app_key_cert_ptr();
    g_secure_app_verify_last_status.code_cert_addr = (uint32_t)(uintptr_t) secure_app_code_cert_ptr();
    g_secure_app_verify_last_status.key_cert_size  = SECURE_APP_KEY_CERT_SIZE;
    g_secure_app_verify_last_status.code_cert_size = SECURE_APP_CODE_CERT_SIZE;
}

static int range_inside(uint32_t base, uint32_t size, uint32_t start, uint32_t end_exclusive)
{
    uint32_t end = base + size;

    if (end < base)
    {
        return 0;
    }

    return (base >= start) && (end <= end_exclusive);
}

static secure_app_verify_result_t verify_code_cert_layout(void)
{
    const secure_app_code_cert_header_t * header = (const secure_app_code_cert_header_t *)(uintptr_t) secure_app_code_cert_ptr();

    SSBL_VERIFY("[SSBL][VERIFY][CODECERT] magic=0x%08lx ver=0x%08lx flags=0x%08lx\n",
           (unsigned long) header->magic,
           (unsigned long) header->manifest_version,
           (unsigned long) header->flags);
    SSBL_VERIFY("[SSBL][VERIFY][CODECERT] load=0x%08lx dest=0x%08lx img_len=0x%08lx img_ver=0x%08lx build=0x%08lx\n",
           (unsigned long) header->load_addr,
           (unsigned long) header->dest_addr,
           (unsigned long) header->img_len,
           (unsigned long) header->img_version,
           (unsigned long) header->build_num);

    if (SECURE_APP_CODE_CERT_MAGIC != header->magic)
    {
        SSBL_VERIFY("[SSBL][VERIFY][CODECERT] invalid magic, expected 0x%08lx\n",
               (unsigned long) SECURE_APP_CODE_CERT_MAGIC);
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }

    if (SECURE_APP_CODE_CERT_VERSION != header->manifest_version)
    {
        SSBL_VERIFY("[SSBL][VERIFY][CODECERT] unsupported version, expected 0x%08lx\n",
               (unsigned long) SECURE_APP_CODE_CERT_VERSION);
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }

#if SSBL_CFG_SECURE_APP_SCHEME == SSBL_CFG_SECURE_APP_SCHEME_OVERALL_APP
    if (SECURE_APP_CODE_CERT_FLAGS_NON_ENC != header->flags)
    {
        SSBL_VERIFY("[SSBL][VERIFY][CODECERT] encryption/extra flags are not supported in scheme B now\n");
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }

    if ((SECURE_APP_PACKAGE_BODY_ADDR != header->load_addr) ||
        (SECURE_APP_PACKAGE_BODY_ADDR != header->dest_addr))
    {
        SSBL_VERIFY("[SSBL][VERIFY][CODECERT] load/dest must match body addr 0x%08lx\n",
               (unsigned long) SECURE_APP_PACKAGE_BODY_ADDR);
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }

    if (sizeof(app_manifest_t) > header->img_len)
    {
        SSBL_VERIFY("[SSBL][VERIFY][CODECERT] image length is smaller than RZAP manifest\n");
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }

    if (!range_inside(SECURE_APP_PACKAGE_BODY_ADDR,
                      header->img_len,
                      SECURE_APP_FLASH_BANK_START,
                      SECURE_APP_FLASH_BANK_END_EXCLUSIVE))
    {
        SSBL_VERIFY("[SSBL][VERIFY][CODECERT] image range is outside xSPI flash window\n");
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }

    g_secure_app_verified_body_size = header->img_len;
#endif

    return SECURE_APP_VERIFY_OK;
}

uint32_t secure_app_verify_last_fsp_error(void)
{
    return g_secure_app_verify_last_fsp_error;
}

const volatile secure_app_verify_status_t * secure_app_verify_last_status(void)
{
    return &g_secure_app_verify_last_status;
}

uint32_t secure_app_verified_body_size(void)
{
    return g_secure_app_verified_body_size;
}

secure_app_verify_result_t secure_app_verify_package(void)
{
#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
    fsp_err_t fsp_err;

    g_secure_app_verify_last_fsp_error = 0u;
    g_secure_app_verified_body_size = 0u;
    verify_status_set(SECURE_APP_VERIFY_OK, SECURE_APP_VERIFY_STAGE_RSIP_OPEN, 0u);

    fsp_err = R_RSIP_Open(&g_rsip_ctrl, &g_rsip_cfg);
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        verify_status_set(SECURE_APP_VERIFY_RSIP_OPEN_FAILED,
                          SECURE_APP_VERIFY_STAGE_RSIP_OPEN,
                          (uint32_t) fsp_err);
        return SECURE_APP_VERIFY_RSIP_OPEN_FAILED;
    }

    verify_status_set(SECURE_APP_VERIFY_OK, SECURE_APP_VERIFY_STAGE_MANIFEST_VERIFY, 0u);
    fsp_err = R_RSIP_SB_ManifestVerify(&g_rsip_ctrl,
                                       secure_app_key_cert_ptr(),
                                       SECURE_APP_KEY_CERT_SIZE,
                                       secure_app_code_cert_ptr(),
                                       SECURE_APP_CODE_CERT_SIZE);
    SSBL_VERIFY("[SSBL][VERIFY] R_RSIP_SB_ManifestVerify fsp error=0x%08lx (%s)\n",
           (unsigned long) fsp_err,
           manifest_verify_error_string(fsp_err));
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        verify_status_set(SECURE_APP_VERIFY_AUTH_FAILED,
                          SECURE_APP_VERIFY_STAGE_MANIFEST_VERIFY,
                          (uint32_t) fsp_err);
        (void) R_RSIP_Close(&g_rsip_ctrl);
        return SECURE_APP_VERIFY_AUTH_FAILED;
    }
#if 1   // reserved for the code certificate verification
    verify_status_set(SECURE_APP_VERIFY_OK, SECURE_APP_VERIFY_STAGE_CODE_CERT_LAYOUT, 0u);
    if (SECURE_APP_VERIFY_OK != verify_code_cert_layout())
    {
        verify_status_set(SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED,
                          SECURE_APP_VERIFY_STAGE_CODE_CERT_LAYOUT,
                          0u);
        (void) R_RSIP_Close(&g_rsip_ctrl);
        return SECURE_APP_VERIFY_CODE_CERT_LAYOUT_FAILED;
    }
#endif 
    verify_status_set(SECURE_APP_VERIFY_OK, SECURE_APP_VERIFY_STAGE_RSIP_CLOSE, 0u);
    fsp_err = R_RSIP_Close(&g_rsip_ctrl);
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        verify_status_set(SECURE_APP_VERIFY_RSIP_CLOSE_FAILED,
                          SECURE_APP_VERIFY_STAGE_RSIP_CLOSE,
                          (uint32_t) fsp_err);
        return SECURE_APP_VERIFY_RSIP_CLOSE_FAILED;
    }

    verify_status_set(SECURE_APP_VERIFY_OK, SECURE_APP_VERIFY_STAGE_DONE, 0u);
    return SECURE_APP_VERIFY_OK;
#else
    g_secure_app_verify_last_fsp_error = 0u;
    g_secure_app_verified_body_size = 0u;
    verify_status_set(SECURE_APP_VERIFY_DISABLED, SECURE_APP_VERIFY_STAGE_IDLE, 0u);
    return SECURE_APP_VERIFY_DISABLED;
#endif
}