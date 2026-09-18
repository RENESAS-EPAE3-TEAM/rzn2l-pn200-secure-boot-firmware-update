#ifndef SECURE_APP_MANIFEST_H_
#define SECURE_APP_MANIFEST_H_

#include <stdint.h>

#define SECURE_APP_MANIFEST_MAGIC          (0x4D535A52u) /* 'RZSM' little-endian */
#define SECURE_APP_MANIFEST_VERSION        (0x00020000u)
#define SECURE_APP_MANIFEST_HEADER_SIZE    (64u)
#define SECURE_APP_MANIFEST_ENTRY_SIZE     (96u)
#define SECURE_APP_MANIFEST_MAX_SEGMENTS   (16u)
#define SECURE_APP_AES_CBC_IV_SIZE          (16u)

#define SECURE_APP_SEGMENT_FLAG_ENABLE     (1u << 0)
#define SECURE_APP_SEGMENT_FLAG_ENCRYPTED  (1u << 1)
#define SECURE_APP_SEGMENT_FLAG_ZERO_FILL  (1u << 2)
#define SECURE_APP_SEGMENT_FLAG_HASH_VALID (1u << 3)

typedef enum e_secure_app_segment_id
{
    SECURE_APP_SEGMENT_ID_LDR_PRG = 0,
    SECURE_APP_SEGMENT_ID_LDR_DATA,
    SECURE_APP_SEGMENT_ID_VECTOR,
    SECURE_APP_SEGMENT_ID_USER_PRG,
    SECURE_APP_SEGMENT_ID_USER_DATA,
    SECURE_APP_SEGMENT_ID_SYSTEM_PRG,
    SECURE_APP_SEGMENT_ID_SYSTEM_DATA,
    SECURE_APP_SEGMENT_ID_NONCACHE,
    SECURE_APP_SEGMENT_ID_SHARED_NONCACHE_BUFFER,
    SECURE_APP_SEGMENT_ID_VENDOR_START = 0x80
} secure_app_segment_id_t;

typedef enum e_secure_app_aes_key_selection
{
    SECURE_APP_AES_KEY_SELECTION_NONE  = 0u,
    SECURE_APP_AES_KEY_SELECTION_CKEY0 = 1u,
} secure_app_aes_key_selection_t;

typedef struct st_secure_app_manifest_header
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t header_size;
    uint32_t manifest_size;
    uint32_t package_version;
    uint32_t package_flags;
    uint32_t package_size;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t segment_count;
    uint32_t entry_point;
    uint32_t image_base_addr;
    uint32_t app_manifest_offset;
    uint32_t signed_region_offset;
    uint32_t signed_region_size;
    uint32_t header_crc32;
} secure_app_manifest_header_t;

typedef struct st_secure_app_manifest_entry
{
    uint32_t segment_id;
    uint32_t flags;
    uint32_t src_offset;
    uint32_t dst_addr;
    uint32_t file_size;
    uint32_t mem_size;
    uint32_t load_attr;
    uint32_t cipher_size;
    uint32_t aes_key_selection;
    uint8_t  aes_cbc_iv[SECURE_APP_AES_CBC_IV_SIZE];
    uint32_t sha256[8];
    uint32_t reserved[3];
} secure_app_manifest_entry_t;

typedef char secure_app_manifest_header_size_must_match[
    (sizeof(secure_app_manifest_header_t) == SECURE_APP_MANIFEST_HEADER_SIZE) ? 1 : -1];
typedef char secure_app_manifest_entry_size_must_match[
    (sizeof(secure_app_manifest_entry_t) == SECURE_APP_MANIFEST_ENTRY_SIZE) ? 1 : -1];

typedef struct st_secure_app_manifest
{
    secure_app_manifest_header_t header;
    secure_app_manifest_entry_t  entries[SECURE_APP_MANIFEST_MAX_SEGMENTS];
} secure_app_manifest_t;

#endif /* SECURE_APP_MANIFEST_H_ */
