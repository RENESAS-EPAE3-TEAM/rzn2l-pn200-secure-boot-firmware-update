#ifndef APP_MANIFEST_ABI_H_
#define APP_MANIFEST_ABI_H_

#include <stdint.h>

#define APP_MANIFEST_ADDR        (0x60100050u)
#define APP_MANIFEST_MAGIC       (0x50415A52u) /* 'RZAP' little-endian */
#define APP_MANIFEST_VERSION     (0x00000001u)
#define APP_MANIFEST_ENTRIES     (9u)

#define APP_MANIFEST_ENTRY_FLAG_ENABLE    (1u << 0)

typedef enum e_app_manifest_segment_id
{
    APP_MANIFEST_SEGMENT_ID_LDR_PRG = 0,
    APP_MANIFEST_SEGMENT_ID_LDR_DATA,
    APP_MANIFEST_SEGMENT_ID_VECTOR,
    APP_MANIFEST_SEGMENT_ID_USER_PRG,
    APP_MANIFEST_SEGMENT_ID_USER_DATA,
    APP_MANIFEST_SEGMENT_ID_SYSTEM_PRG,
    APP_MANIFEST_SEGMENT_ID_SYSTEM_DATA,
    APP_MANIFEST_SEGMENT_ID_NONCACHE,
    APP_MANIFEST_SEGMENT_ID_SHARED_NONCACHE_BUFFER,
} app_manifest_segment_id_t;

typedef struct st_app_manifest_entry
{
    uint32_t src;
    uint32_t dst;
    uint32_t size;
    uint32_t flags;
} app_manifest_entry_t;

typedef struct st_app_manifest
{
    uint32_t             magic;
    uint32_t             entry_count;
    uint32_t             entry_point;
    uint32_t             reserved;
    app_manifest_entry_t entries[APP_MANIFEST_ENTRIES];
} app_manifest_t;

#endif /* APP_MANIFEST_ABI_H_ */