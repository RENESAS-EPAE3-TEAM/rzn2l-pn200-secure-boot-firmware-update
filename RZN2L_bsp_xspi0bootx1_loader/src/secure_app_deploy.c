#include "secure_app_deploy.h"

#include <stddef.h>
#include <stdint.h>

#include "app_manifest_abi.h"
#include "secure_app_package.h"

#define SECURE_APP_FLASH_BANK_START              (0x60000000u)
#define SECURE_APP_FLASH_BANK_END_EXCLUSIVE      (0x64000000u)

#define SECURE_APP_ATCM_START                    (0x00000000u)
#define SECURE_APP_ATCM_END_EXCLUSIVE            (0x00020000u)
#define SECURE_APP_LDR_DATA_START                (0x00108000u)
#define SECURE_APP_LDR_DATA_END_EXCLUSIVE        (0x0010A000u)
#define SECURE_APP_LDR_PRG_START                 (0x0010A000u)
#define SECURE_APP_LDR_PRG_END_EXCLUSIVE         (0x00118000u)
#define SECURE_APP_USER_APP_START                (0x10000100u)
#define SECURE_APP_USER_APP_END_EXCLUSIVE        (0x10130000u)
#define SECURE_APP_NONCACHE_START                (0x30160000u)
#define SECURE_APP_NONCACHE_END_EXCLUSIVE        (0x30180000u)
#define SECURE_APP_SHARED_NONCACHE_START         (0x30150000u)
#define SECURE_APP_SHARED_NONCACHE_END_EXCLUSIVE (0x30160000u)
#define SECURE_APP_CS2_RAM_START                 (0x74000000u)
#define SECURE_APP_CS2_RAM_END_EXCLUSIVE         (0x74200000u)

#define SECURE_APP_SUPPORTED_SEGMENT_FLAGS       (SECURE_APP_SEGMENT_FLAG_ENABLE | SECURE_APP_SEGMENT_FLAG_HASH_VALID)

typedef struct st_secure_app_memory_window
{
    uint32_t segment_id;
    uint32_t start;
    uint32_t end_exclusive;
} secure_app_memory_window_t;

static const secure_app_memory_window_t g_secure_app_allowed_windows[] =
{
    { SECURE_APP_SEGMENT_ID_LDR_PRG,                  SECURE_APP_LDR_PRG_START,                 SECURE_APP_LDR_PRG_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_LDR_DATA,                 SECURE_APP_LDR_DATA_START,                SECURE_APP_LDR_DATA_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_VECTOR,                   SECURE_APP_ATCM_START,                    SECURE_APP_ATCM_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_USER_PRG,                 SECURE_APP_USER_APP_START,                SECURE_APP_USER_APP_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_USER_DATA,                SECURE_APP_USER_APP_START,                SECURE_APP_USER_APP_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_SYSTEM_PRG,               SECURE_APP_CS2_RAM_START,                 SECURE_APP_CS2_RAM_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_SYSTEM_DATA,              SECURE_APP_CS2_RAM_START,                 SECURE_APP_CS2_RAM_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_NONCACHE,                 SECURE_APP_NONCACHE_START,                SECURE_APP_NONCACHE_END_EXCLUSIVE },
    { SECURE_APP_SEGMENT_ID_SHARED_NONCACHE_BUFFER,   SECURE_APP_SHARED_NONCACHE_START,         SECURE_APP_SHARED_NONCACHE_END_EXCLUSIVE },
};

static int range_inside(uint32_t base, uint32_t size, uint32_t start, uint32_t end_exclusive)
{
    uint32_t end = base + size;

    if (end < base)
    {
        return 0;
    }

    return (base >= start) && (end <= end_exclusive);
}

static int segment_destination_allowed(uint32_t segment_id, uint32_t dst_addr, uint32_t size)
{
    uint32_t i;

    for (i = 0u; i < (sizeof(g_secure_app_allowed_windows) / sizeof(g_secure_app_allowed_windows[0])); i++)
    {
        const secure_app_memory_window_t * window = &g_secure_app_allowed_windows[i];
        if (segment_id == window->segment_id)
        {
            return range_inside(dst_addr, size, window->start, window->end_exclusive);
        }
    }

    return 0;
}

static int segment_source_allowed(const secure_app_manifest_header_t * header, const secure_app_manifest_entry_t * entry)
{
    uint32_t body_addr = SECURE_APP_PACKAGE_BODY_ADDR;
    uint32_t source_addr = body_addr + entry->src_offset;

    if (source_addr < body_addr)
    {
        return 0;
    }

    if (entry->src_offset < header->payload_offset)
    {
        return 0;
    }

    if (!range_inside(source_addr, entry->file_size, body_addr, body_addr + header->package_size))
    {
        return 0;
    }

    return range_inside(source_addr, entry->file_size, SECURE_APP_FLASH_BANK_START, SECURE_APP_FLASH_BANK_END_EXCLUSIVE);
}

static int segments_overlap(uint32_t start_a, uint32_t size_a, uint32_t start_b, uint32_t size_b)
{
    uint32_t end_a = start_a + size_a;
    uint32_t end_b = start_b + size_b;

    if ((end_a < start_a) || (end_b < start_b))
    {
        return 1;
    }

    return (start_a < end_b) && (start_b < end_a);
}

static int later_enabled_segments_overlap(const secure_app_manifest_t * manifest, uint32_t index)
{
    const secure_app_manifest_entry_t * entry = &manifest->entries[index];
    uint32_t i;

    for (i = index + 1u; i < manifest->header.segment_count; i++)
    {
        const secure_app_manifest_entry_t * other = &manifest->entries[i];
        if (0u == (other->flags & SECURE_APP_SEGMENT_FLAG_ENABLE))
        {
            continue;
        }

        if (segments_overlap(entry->dst_addr, entry->mem_size, other->dst_addr, other->mem_size))
        {
            return 1;
        }
    }

    return 0;
}

static const secure_app_manifest_entry_t * find_secure_entry(const secure_app_manifest_t * manifest,
                                                             uint32_t segment_id)
{
    uint32_t i;

    for (i = 0u; i < manifest->header.segment_count; i++)
    {
        const secure_app_manifest_entry_t * entry = &manifest->entries[i];
        if ((entry->segment_id == segment_id) && (entry->flags & SECURE_APP_SEGMENT_FLAG_ENABLE))
        {
            return entry;
        }
    }

    return NULL;
}

static int validate_legacy_manifest_equivalence(const secure_app_manifest_t * manifest)
{
    uint32_t app_manifest_end = manifest->header.app_manifest_offset + sizeof(app_manifest_t);
    const app_manifest_t * legacy_manifest;
    uint32_t active_legacy_count = 0u;
    uint32_t i;

    if ((app_manifest_end < manifest->header.app_manifest_offset) ||
        (app_manifest_end > manifest->header.payload_size))
    {
        return 0;
    }

    legacy_manifest = (const app_manifest_t *)(uintptr_t)(SECURE_APP_PACKAGE_BODY_ADDR +
                                                          manifest->header.payload_offset +
                                                          manifest->header.app_manifest_offset);

    if ((APP_MANIFEST_MAGIC != legacy_manifest->magic) ||
        (legacy_manifest->entry_count > APP_MANIFEST_ENTRIES) ||
        (legacy_manifest->entry_point != manifest->header.entry_point))
    {
        return 0;
    }

    for (i = 0u; i < legacy_manifest->entry_count; i++)
    {
        const app_manifest_entry_t * legacy_entry = &legacy_manifest->entries[i];
        const secure_app_manifest_entry_t * secure_entry;
        uint32_t image_offset;
        uint32_t expected_src_offset;

        if (0u == ((legacy_entry->flags & APP_MANIFEST_ENTRY_FLAG_ENABLE) && legacy_entry->size))
        {
            continue;
        }

        if (legacy_entry->src < manifest->header.image_base_addr)
        {
            return 0;
        }

        image_offset = legacy_entry->src - manifest->header.image_base_addr;
        expected_src_offset = manifest->header.payload_offset + image_offset;
        if (expected_src_offset < manifest->header.payload_offset)
        {
            return 0;
        }

        secure_entry = find_secure_entry(manifest, i);
        if ((NULL == secure_entry) ||
            (secure_entry->dst_addr != legacy_entry->dst) ||
            (secure_entry->file_size != legacy_entry->size) ||
            (secure_entry->mem_size != legacy_entry->size) ||
            (secure_entry->src_offset != expected_src_offset))
        {
            return 0;
        }

        active_legacy_count++;
    }

    return active_legacy_count == manifest->header.segment_count;
}

static secure_app_deploy_result_t validate_header(const secure_app_manifest_header_t * header)
{
    uint32_t expected_manifest_size;

    if (SECURE_APP_MANIFEST_MAGIC != header->magic)
    {
        return SECURE_APP_DEPLOY_ERR_HEADER;
    }

    if ((SECURE_APP_MANIFEST_VERSION != header->format_version) ||
        (SECURE_APP_MANIFEST_HEADER_SIZE != header->header_size) ||
        (0u == header->segment_count) ||
        (header->segment_count > SECURE_APP_MANIFEST_MAX_SEGMENTS))
    {
        return SECURE_APP_DEPLOY_ERR_HEADER;
    }

    expected_manifest_size = SECURE_APP_MANIFEST_HEADER_SIZE +
                             (header->segment_count * SECURE_APP_MANIFEST_ENTRY_SIZE);

    if ((expected_manifest_size != header->manifest_size) ||
        (header->payload_offset < header->manifest_size) ||
        (header->payload_offset > header->package_size) ||
        (header->payload_size != (header->package_size - header->payload_offset)) ||
        (0u != header->signed_region_offset) ||
        (header->signed_region_size != header->package_size))
    {
        return SECURE_APP_DEPLOY_ERR_HEADER;
    }

    if (!range_inside(SECURE_APP_PACKAGE_BODY_ADDR,
                      header->package_size,
                      SECURE_APP_FLASH_BANK_START,
                      SECURE_APP_FLASH_BANK_END_EXCLUSIVE))
    {
        return SECURE_APP_DEPLOY_ERR_PACKAGE_RANGE;
    }

    return SECURE_APP_DEPLOY_OK;
}

secure_app_deploy_result_t secure_app_deploy_validate(const secure_app_manifest_t * manifest)
{
    secure_app_deploy_result_t result;
    uint32_t i;

    if (NULL == manifest)
    {
        return SECURE_APP_DEPLOY_ERR_NULL;
    }

    result = validate_header(&manifest->header);
    if (SECURE_APP_DEPLOY_OK != result)
    {
        return result;
    }

    if (!segment_destination_allowed(SECURE_APP_SEGMENT_ID_LDR_PRG, manifest->header.entry_point, 1u))
    {
        return SECURE_APP_DEPLOY_ERR_ENTRY_POINT;
    }

    if (!validate_legacy_manifest_equivalence(manifest))
    {
        return SECURE_APP_DEPLOY_ERR_LEGACY_MISMATCH;
    }

    for (i = 0u; i < manifest->header.segment_count; i++)
    {
        const secure_app_manifest_entry_t * entry = &manifest->entries[i];

        if (0u == (entry->flags & SECURE_APP_SEGMENT_FLAG_ENABLE))
        {
            continue;
        }

        if ((entry->flags & ~SECURE_APP_SUPPORTED_SEGMENT_FLAGS) ||
            (0u == (entry->flags & SECURE_APP_SEGMENT_FLAG_HASH_VALID)) ||
            (entry->file_size != entry->mem_size) ||
            (0u == entry->file_size))
        {
            return SECURE_APP_DEPLOY_ERR_SEGMENT_FLAGS;
        }

        if (!segment_source_allowed(&manifest->header, entry) ||
            !segment_destination_allowed(entry->segment_id, entry->dst_addr, entry->mem_size))
        {
            return SECURE_APP_DEPLOY_ERR_SEGMENT_RANGE;
        }

        if (later_enabled_segments_overlap(manifest, i))
        {
            return SECURE_APP_DEPLOY_ERR_SEGMENT_OVERLAP;
        }
    }

    return SECURE_APP_DEPLOY_OK;
}

secure_app_deploy_result_t secure_app_deploy_copy(const secure_app_manifest_t * manifest,
                                                  void (*copy_func)(uintptr_t * src,
                                                                    uintptr_t * dst,
                                                                    uintptr_t bytesize),
                                                  void (** p_entry)(void))
{
    secure_app_deploy_result_t result;
    uint32_t body_addr = SECURE_APP_PACKAGE_BODY_ADDR;
    uint32_t i;

    if ((NULL == copy_func) || (NULL == p_entry))
    {
        return SECURE_APP_DEPLOY_ERR_NULL;
    }

    result = secure_app_deploy_validate(manifest);
    if (SECURE_APP_DEPLOY_OK != result)
    {
        return result;
    }

    for (i = 0u; i < manifest->header.segment_count; i++)
    {
        const secure_app_manifest_entry_t * entry = &manifest->entries[i];
        if (0u == (entry->flags & SECURE_APP_SEGMENT_FLAG_ENABLE))
        {
            continue;
        }

        copy_func((uintptr_t *)(uintptr_t)(body_addr + entry->src_offset),
                  (uintptr_t *)(uintptr_t)entry->dst_addr,
                  (uintptr_t) entry->file_size);
    }

    *p_entry = (void (*)(void))(uintptr_t)manifest->header.entry_point;
    return SECURE_APP_DEPLOY_OK;
}
