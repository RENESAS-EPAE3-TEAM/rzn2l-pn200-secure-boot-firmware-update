#ifndef SECURE_APP_VERIFY_H_
#define SECURE_APP_VERIFY_H_

#include <stdint.h>

typedef enum e_secure_app_verify_result
{
    SECURE_APP_VERIFY_OK = 0,
    SECURE_APP_VERIFY_DISABLED,
    SECURE_APP_VERIFY_RSIP_OPEN_FAILED,
    SECURE_APP_VERIFY_AUTH_FAILED,
    SECURE_APP_VERIFY_RSIP_CLOSE_FAILED,
} secure_app_verify_result_t;

typedef enum e_secure_app_verify_stage
{
    SECURE_APP_VERIFY_STAGE_IDLE = 0,
    SECURE_APP_VERIFY_STAGE_RSIP_OPEN,
    SECURE_APP_VERIFY_STAGE_MANIFEST_VERIFY,
    SECURE_APP_VERIFY_STAGE_RSIP_CLOSE,
    SECURE_APP_VERIFY_STAGE_DONE,
} secure_app_verify_stage_t;

typedef struct st_secure_app_verify_status
{
    uint32_t result;
    uint32_t stage;
    uint32_t fsp_error;
    uint32_t key_cert_addr;
    uint32_t code_cert_addr;
    uint32_t key_cert_size;
    uint32_t code_cert_size;
} secure_app_verify_status_t;

extern volatile secure_app_verify_status_t g_secure_app_verify_last_status;

secure_app_verify_result_t secure_app_verify_package(void);
uint32_t secure_app_verify_last_fsp_error(void);
const volatile secure_app_verify_status_t * secure_app_verify_last_status(void);

#endif /* SECURE_APP_VERIFY_H_ */