#ifndef SECURE_APP_DEPLOY_H_
#define SECURE_APP_DEPLOY_H_

#include <stdint.h>

#include "app_manifest_abi.h"
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

typedef enum e_secure_app_deploy_stage
{
    SECURE_APP_DEPLOY_STAGE_IDLE = 0,
    SECURE_APP_DEPLOY_STAGE_HEADER,
    SECURE_APP_DEPLOY_STAGE_PACKAGE_RANGE,
    SECURE_APP_DEPLOY_STAGE_ENTRY_POINT,
    SECURE_APP_DEPLOY_STAGE_LEGACY_MANIFEST,
    SECURE_APP_DEPLOY_STAGE_SEGMENT_FLAGS,
    SECURE_APP_DEPLOY_STAGE_SEGMENT_SOURCE,
    SECURE_APP_DEPLOY_STAGE_SEGMENT_DESTINATION,
    SECURE_APP_DEPLOY_STAGE_SEGMENT_OVERLAP,
    SECURE_APP_DEPLOY_STAGE_COPY,
    SECURE_APP_DEPLOY_STAGE_DONE,
} secure_app_deploy_stage_t;

typedef struct st_secure_app_deploy_status
{
    uint32_t result;
    uint32_t stage;
    uint32_t segment_index;
    uint32_t segment_id;
    uint32_t address;
    uint32_t size;
    uint32_t detail;
} secure_app_deploy_status_t;

extern volatile secure_app_deploy_status_t g_secure_app_deploy_last_status;

secure_app_deploy_result_t secure_app_deploy_validate(const secure_app_manifest_t * manifest);
secure_app_deploy_result_t secure_app_deploy_copy(const secure_app_manifest_t * manifest,
                                                  void (*copy_func)(uintptr_t * src,
                                                                    uintptr_t * dst,
                                                                    uintptr_t bytesize),
                                                  void (** p_entry)(void));
secure_app_deploy_result_t secure_app_deploy_copy_overall_app(const app_manifest_t * manifest,
                                                              uint32_t image_link_base,
                                                              uint32_t image_runtime_base,
                                                              uint32_t image_runtime_size,
                                                              void (*copy_func)(uintptr_t * src,
                                                                                uintptr_t * dst,
                                                                                uintptr_t bytesize),
                                                              void (** p_entry)(void));
const volatile secure_app_deploy_status_t * secure_app_deploy_last_status(void);

#endif /* SECURE_APP_DEPLOY_H_ */
