#ifndef SECURE_SSBL_CONFIG_H_
#define SECURE_SSBL_CONFIG_H_

/* Keep the legacy loader_table fallback available while the secure package
 * path is being brought up. Disable this for secure/release builds. */
#ifndef SSBL_CFG_LEGACY_TABLE_FALLBACK_ENABLE
#define SSBL_CFG_LEGACY_TABLE_FALLBACK_ENABLE    (0u)
#endif

#ifndef SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE
#define SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE      (0u)
#endif

#ifndef SSBL_CFG_SECURE_PACKAGE_DEPLOY_ENABLE
#define SSBL_CFG_SECURE_PACKAGE_DEPLOY_ENABLE    (0u)
#endif

#define SSBL_CFG_SECURE_APP_SCHEME_RZSM          (1u)
#define SSBL_CFG_SECURE_APP_SCHEME_OVERALL_APP   (2u)

/* Scheme A keeps the custom RZSM secure manifest + segment deploy path.
 * Scheme B signs the whole PN2.0 App raw binary and deploys via legacy RZAP. */
#ifndef SSBL_CFG_SECURE_APP_SCHEME
#define SSBL_CFG_SECURE_APP_SCHEME               SSBL_CFG_SECURE_APP_SCHEME_OVERALL_APP
#endif

#if ((SSBL_CFG_SECURE_APP_SCHEME != SSBL_CFG_SECURE_APP_SCHEME_RZSM) && \
     (SSBL_CFG_SECURE_APP_SCHEME != SSBL_CFG_SECURE_APP_SCHEME_OVERALL_APP))
#error "SSBL_CFG_SECURE_APP_SCHEME must be SSBL_CFG_SECURE_APP_SCHEME_RZSM or SSBL_CFG_SECURE_APP_SCHEME_OVERALL_APP"
#endif

#ifndef SSBL_CFG_DEBUG_UART_ENABLE
#define SSBL_CFG_DEBUG_UART_ENABLE               (1u)
#endif

#endif /* SECURE_SSBL_CONFIG_H_ */