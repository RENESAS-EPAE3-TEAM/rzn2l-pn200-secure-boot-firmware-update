#include "secure_app_verify.h"

#include "secure_app_package.h"
#include "secure_ssbl_config.h"

#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
#include "hal_data.h"
#endif

static uint32_t g_secure_app_verify_last_fsp_error;

uint32_t secure_app_verify_last_fsp_error(void)
{
    return g_secure_app_verify_last_fsp_error;
}

secure_app_verify_result_t secure_app_verify_package(void)
{
#if SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
    fsp_err_t fsp_err;

    g_secure_app_verify_last_fsp_error = 0u;

    fsp_err = R_RSIP_Open(&g_rsip_ctrl, &g_rsip_cfg);
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        return SECURE_APP_VERIFY_RSIP_OPEN_FAILED;
    }

    fsp_err = R_RSIP_SB_ManifestVerify(&g_rsip_ctrl,
                                       secure_app_key_cert_ptr(),
                                       SECURE_APP_KEY_CERT_SIZE,
                                       secure_app_code_cert_ptr(),
                                       SECURE_APP_CODE_CERT_SIZE);
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        (void) R_RSIP_Close(&g_rsip_ctrl);
        return SECURE_APP_VERIFY_AUTH_FAILED;
    }

    fsp_err = R_RSIP_Close(&g_rsip_ctrl);
    if (FSP_SUCCESS != fsp_err)
    {
        g_secure_app_verify_last_fsp_error = (uint32_t) fsp_err;
        return SECURE_APP_VERIFY_RSIP_CLOSE_FAILED;
    }

    return SECURE_APP_VERIFY_OK;
#else
    g_secure_app_verify_last_fsp_error = 0u;
    return SECURE_APP_VERIFY_DISABLED;
#endif
}