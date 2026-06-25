#ifndef SECURE_APP_PACKAGE_H_
#define SECURE_APP_PACKAGE_H_

#include <stdint.h>

#include "secure_app_manifest.h"

#define SECURE_APP_PACKAGE_BASE_ADDR       (0x60100050u)
#define SECURE_APP_KEY_CERT_SIZE           (0x000000E0u)
#define SECURE_APP_CODE_CERT_SIZE          (0x00000120u)
#define SECURE_APP_TOTAL_CERT_SIZE         (SECURE_APP_KEY_CERT_SIZE + SECURE_APP_CODE_CERT_SIZE)

#define SECURE_APP_KEY_CERT_ADDR           (SECURE_APP_PACKAGE_BASE_ADDR)
#define SECURE_APP_CODE_CERT_ADDR          (SECURE_APP_PACKAGE_BASE_ADDR + SECURE_APP_KEY_CERT_SIZE)
#define SECURE_APP_PACKAGE_BODY_ADDR       (SECURE_APP_PACKAGE_BASE_ADDR + SECURE_APP_TOTAL_CERT_SIZE)
#define SECURE_APP_MANIFEST_ADDR           (SECURE_APP_PACKAGE_BODY_ADDR)

typedef struct st_secure_app_package_layout
{
    uint32_t package_base_addr;
    uint32_t key_cert_addr;
    uint32_t key_cert_size;
    uint32_t code_cert_addr;
    uint32_t code_cert_size;
    uint32_t body_addr;
    uint32_t manifest_addr;
} secure_app_package_layout_t;

static inline const uint8_t * secure_app_key_cert_ptr(void)
{
    return (const uint8_t *)(uintptr_t) SECURE_APP_KEY_CERT_ADDR;
}

static inline const uint8_t * secure_app_code_cert_ptr(void)
{
    return (const uint8_t *)(uintptr_t) SECURE_APP_CODE_CERT_ADDR;
}

static inline const secure_app_manifest_t * secure_app_manifest_ptr(void)
{
    return (const secure_app_manifest_t *)(uintptr_t) SECURE_APP_MANIFEST_ADDR;
}

static inline const uint8_t * secure_app_body_ptr(void)
{
    return (const uint8_t *)(uintptr_t) SECURE_APP_PACKAGE_BODY_ADDR;
}

#endif /* SECURE_APP_PACKAGE_H_ */