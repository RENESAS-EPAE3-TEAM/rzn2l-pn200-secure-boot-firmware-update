#ifndef SECURE_APP_DEPLOY_H_
#define SECURE_APP_DEPLOY_H_

#include <stdint.h>

#include "secure_app_manifest.h"

typedef enum e_secure_app_deploy_result
{
    SECURE_APP_DEPLOY_OK = 0,
    SECURE_APP_DEPLOY_ERR_NULL,
    SECURE_APP_DEPLOY_ERR_HEADER,
    SECURE_APP_DEPLOY_ERR_PACKAGE_RANGE,
    SECURE_APP_DEPLOY_ERR_SEGMENT_RANGE,
    SECURE_APP_DEPLOY_ERR_SEGMENT_FLAGS,
    SECURE_APP_DEPLOY_ERR_SEGMENT_OVERLAP,
    SECURE_APP_DEPLOY_ERR_LEGACY_MISMATCH,
    SECURE_APP_DEPLOY_ERR_ENTRY_POINT,
} secure_app_deploy_result_t;

secure_app_deploy_result_t secure_app_deploy_validate(const secure_app_manifest_t * manifest);
secure_app_deploy_result_t secure_app_deploy_copy(const secure_app_manifest_t * manifest,
                                                  void (*copy_func)(uintptr_t * src,
                                                                    uintptr_t * dst,
                                                                    uintptr_t bytesize),
                                                  void (** p_entry)(void));

#endif /* SECURE_APP_DEPLOY_H_ */
