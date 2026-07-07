#include "secure_app_verify.h"

#include "secure_app_package.h"
#include "secure_ssbl_config.h"

#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
#include "hal_data.h"
#endif

static uint32_t g_secure_app_verify_last_fsp_error;
volatile secure_app_verify_status_t g_secure_app_verify_last_status;

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

uint32_t secure_app_verify_last_fsp_error(void)
{
    return g_secure_app_verify_last_fsp_error;
}

const volatile secure_app_verify_status_t * secure_app_verify_last_status(void)
{
    return &g_secure_app_verify_last_status;
}

secure_app_verify_result_t secure_app_verify_package(void)
{
#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
    fsp_err_t fsp_err;

    g_secure_app_verify_last_fsp_error = 0u;
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
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        verify_status_set(SECURE_APP_VERIFY_AUTH_FAILED,
                          SECURE_APP_VERIFY_STAGE_MANIFEST_VERIFY,
                          (uint32_t) fsp_err);
        (void) R_RSIP_Close(&g_rsip_ctrl);
        return SECURE_APP_VERIFY_AUTH_FAILED;
    }

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
    verify_status_set(SECURE_APP_VERIFY_DISABLED, SECURE_APP_VERIFY_STAGE_IDLE, 0u);
    return SECURE_APP_VERIFY_DISABLED;
#endif
}