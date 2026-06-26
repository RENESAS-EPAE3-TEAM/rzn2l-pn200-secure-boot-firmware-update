#ifndef SECURE_SSBL_CONFIG_H_
#define SECURE_SSBL_CONFIG_H_

/* Keep the legacy loader_table fallback available while the secure package
 * path is being brought up. Disable this for secure/release builds. */
#ifndef SSBL_CFG_LEGACY_TABLE_FALLBACK_ENABLE
#define SSBL_CFG_LEGACY_TABLE_FALLBACK_ENABLE    (1u)
#endif

#ifndef SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
#define SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE      (1u)
#endif

#ifndef SSBL_CFG_SECURE_PACKAGE_DEPLOY_ENABLE
#define SSBL_CFG_SECURE_PACKAGE_DEPLOY_ENABLE    (1u)
#endif

#ifndef SSBL_CFG_DEBUG_UART_ENABLE
#define SSBL_CFG_DEBUG_UART_ENABLE               (0u)
#endif

#endif /* SECURE_SSBL_CONFIG_H_ */