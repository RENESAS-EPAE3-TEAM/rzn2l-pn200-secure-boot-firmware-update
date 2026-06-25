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

secure_app_verify_result_t secure_app_verify_package(void);
uint32_t secure_app_verify_last_fsp_error(void);

#endif /* SECURE_APP_VERIFY_H_ */