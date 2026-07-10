# RZN2L PN2.0 Secure SSBL + Secure App + FWUpdate Proposal

This document proposes a phased engineering plan for applying Renesas secure boot and secure firmware update concepts to the RZN2L PN2.0 loader/app split project.

The current PN2.0 project already separates the original xSPI boot application into:

| Project | Role |
| --- | --- |
| `RZN2L_bsp_xspi0bootx1_loader` | Current non-secure Loader. It initializes required hardware, reads the App manifest, copies App sections to their runtime memories, and jumps to App `system_init`. |
| `RZN2L_bsp_xspi0bootx1_app` | PROFINET IRT App. It publishes a fixed App manifest and runs across ATCM, BTCM, System RAM, SDRAM/HRAM, and non-cache regions. |

The secure design should not force the PN2.0 App into the simple single contiguous RAM image model used by the Renesas secure firmware update sample. Instead, it should keep the existing multi-section App manifest idea and add secure verification, optional encryption, secure update, dual-bank management, and anti-rollback in controlled phases.

## 1. Core Proposal

Use **whole-package authentication with section-based deployment**.

In other words:

```text
xSPI Flash keeps one secure App package.
SSBL verifies the whole secure package before trusting it.
After successful verification, SSBL parses a signed secure manifest.
SSBL decrypts and copies each App section to its own target memory.
SSBL jumps to the signed App entry point, normally App system_init.
```

This is referred to as **方案 A: 整包验签，分段搬运**.

The key point is that authentication is done on the complete secure App package, while deployment still follows the PN2.0 App's real memory layout.

## 2. Why The Single-Image Sample Model Is Not Enough

The Renesas secure update sample for RZN2L uses a simple UserApp parameter model:

```text
src_addr
img_size
app_start_addr
signature
```

The sample SSBL then copies one continuous image to `app_start_addr` and verifies it through the secure boot certificate flow.

That model works when the App is small and runs from one contiguous RAM region, such as ATCM. PN2.0 is different:

| PN2.0 App area | Runtime memory |
| --- | --- |
| App startup / LDR code | BTCM |
| Vector table | ATCM |
| Main App code/data | System RAM |
| PROFINET/system code/data | SDRAM or HRAM |
| Non-cache data | System RAM non-cache mirror |
| Shared buffers | Shared non-cache mirror |

Therefore, PN2.0 needs a secure manifest that describes multiple sections, not just one `app_start_addr` and `img_size`.

## 3. Proposed Secure App Package Format

A practical secure App package can be arranged like this:

```text
Secure App Package in xSPI Flash
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+
| Secure App Manifest         |
+-----------------------------+
| Segment 0 cipher/plain data |
+-----------------------------+
| Segment 1 cipher/plain data |
+-----------------------------+
| Segment 2 cipher/plain data |
+-----------------------------+
| ...                         |
+-----------------------------+
```

The signed target should include at least:

```text
Code Certificate
Secure App Manifest
All segment payloads, preferably encrypted payloads when App encryption is enabled
```

The secure manifest should include enough information for SSBL to safely deploy the App:

| Field | Purpose |
| --- | --- |
| magic/version | Identify secure manifest format. |
| manifest_size | Allow future format extension. |
| package_version | App/package version for anti-rollback. |
| entry_point | Final jump target, normally App `system_init`. |
| segment_count | Number of section records. |
| segment records | Per-section source offset/address, destination address, size, flags, memory type, IV/key info if needed. |
| total_package_size | Boundary check for package parsing. |
| hash/signature binding | Ensure section layout and payload cannot be tampered with. |

Each segment record should be explicit:

```text
segment_type
flash_cipher_offset or flash_plain_offset
dst_addr
plain_size
cipher_size
flags
iv or iv_index
alignment requirement
```

The destination address, size, and segment type must be part of the authenticated data. Otherwise, an attacker could reuse valid payload bytes but redirect them to an unexpected memory location.

## 4. RSIP Behavior Relevant To This Proposal

Based on the Renesas FSP secure boot library used by the sample:

- `R_RSIP_SB_ManifestVerify()` receives Key Certificate and Code Certificate pointers.
- It does not receive an explicit image pointer.
- The image address is taken from the Code Certificate header field `dest_addr`.
- The image length is taken from the Code Certificate header field `img_len`.
- For encrypted images, Image Cipher Info TLV provides cipher parameters and a decryption destination address.

This means xSPI Flash can be used as the verification source when these conditions are met:

| Requirement | Note |
| --- | --- |
| xSPI is memory mapped and readable | SSBL must initialize xSPI/protocol before verification. |
| Certificate image address points to xSPI | `code_cert.dest_addr` can point at the secure package payload in xSPI memory space. |
| Address alignment is valid | The library checks 4-byte alignment for certificate/image-related addresses. |
| xSPI cache/MPU/bus settings are valid | CPU/RSIP path must read consistent package bytes. |

However, `R_RSIP_SB_ManifestVerify()` is still a continuous-image secure boot API. It is not a scatter-loader for PN2.0 sections.

Recommended interpretation:

```text
Use RSIP secure boot/certificate verification to authenticate the whole package.
Use SSBL-owned logic to parse the signed secure manifest.
Use RSIP AES APIs or an equivalent RSIP-supported decrypt path to decrypt each segment from xSPI to its target RAM/SDRAM region.
```

Do not depend on one secure boot API call to automatically decrypt and scatter-load PN2.0 sections.

## 5. Proposed SSBL Boot Flow

The secure SSBL should perform this sequence:

```text
1. Boot ROM verifies and starts Secure SSBL.
2. Secure SSBL initializes minimum runtime context.
3. Secure SSBL initializes xSPI memory-mapped read access.
4. Secure SSBL initializes SDRAM/HRAM if App sections need external RAM before jump.
5. Secure SSBL opens RSIP.
6. Secure SSBL selects active App bank or single App package address.
7. Secure SSBL reads Key Certificate and Code Certificate.
8. Secure SSBL verifies the whole secure App package.
9. Secure SSBL parses the authenticated secure App manifest.
10. Secure SSBL validates all segment ranges against fixed memory whitelists.
11. Secure SSBL decrypts/copies each segment to its target memory.
12. Secure SSBL cleans/invalidates cache and executes DSB/ISB.
13. Secure SSBL jumps to authenticated `entry_point`, normally App `system_init`.
```

The SSBL should not jump directly to App `main`. The current PN2.0 split design correctly keeps App startup ownership inside the App project.

## 6. Segment Validation Rules

Before copying or decrypting any segment, SSBL should enforce strict checks.

Required checks:

| Check | Reason |
| --- | --- |
| `dst_addr + size` does not overflow | Prevent wraparound. |
| Segment destination is in an allowed region | Prevent writing into SSBL, RSIP buffers, stack, or registers. |
| Segment source is inside the active package/bank | Prevent out-of-package reads. |
| Segment size is nonzero when required | Avoid malformed entries. |
| Alignment matches RSIP and CPU requirements | AES/CBC usually needs 16-byte cipher length; certificates need 4-byte alignment. |
| Segments do not illegally overlap | Prevent corruption and unexpected aliasing. |
| Entry point is inside allowed App LDR code region | Prevent jump redirection. |
| Segment type matches destination whitelist | Example: VECTOR only to ATCM vector area, SYSTEM_PRG only to SDRAM/HRAM code area. |

Example whitelist idea:

| Segment type | Allowed destination |
| --- | --- |
| LDR_PRG / LDR_DATA | App BTCM offset area, not SSBL BTCM area. |
| VECTOR | ATCM vector area. |
| USER_PRG / USER_DATA | App System RAM area. |
| SYSTEM_PRG / SYSTEM_DATA | SDRAM or HRAM App area. |
| NONCACHE | App non-cache init/runtime area. |
| SHARED_NONCACHE | Shared non-cache buffer area. |

These checks are as important as cryptographic verification. A correctly signed image is still dangerous if the manifest is too permissive.

## 7. App Encryption Strategy

After whole-package authentication is working, App encryption can be added.

Recommended encryption model:

```text
Flash stores encrypted segment payloads.
The secure manifest records each encrypted segment offset, cipher size, plain size, destination, IV, and flags.
SSBL verifies the complete package first.
SSBL decrypts each segment from xSPI Flash directly to its target RAM/SDRAM destination.
```

Preferred order:

```text
Verify encrypted package first.
Then decrypt only after verification succeeds.
```

This avoids executing or deploying unauthenticated plaintext and avoids needing a large temporary RAM buffer.

Implementation notes:

- AES-CBC requires 16-byte block alignment. Package generation should pad each encrypted segment.
- Manifest should keep both `plain_size` and `cipher_size` so SSBL can clear or ignore padding correctly.
- Each segment should use a defined IV policy. Per-segment IVs are easier to reason about than reusing one IV across disjoint sections.
- The SSBL project must enable the required RSIP AES functionality, not just secure boot verification.
- If using Renesas secure boot Image Cipher Info directly, be aware it is intended for a continuous image, not a multi-destination scatter image.

## 8. FWUpdate Strategy

Secure FWUpdate should write a complete new secure App package and only switch boot state after validation succeeds.

Recommended update flow:

```text
1. Running App or update mode receives a new secure App package.
2. FWUpdate writes the package to the inactive App bank or staging area.
3. FWUpdate verifies package structure and cryptographic authenticity.
4. FWUpdate writes new App parameter/metadata only after image write succeeds.
5. FWUpdate switches flash management state to the new bank.
6. Device resets.
7. SSBL validates the selected bank again before boot.
8. If boot validation fails, SSBL falls back to the previous valid bank when dual-bank is enabled.
```

FWUpdate should never switch active state before the full package is written and verified.

## 9. Dual-Bank Strategy

For an industrial PROFINET product, dual-bank is strongly recommended.

A possible xSPI layout:

```text
xSPI Flash
+-----------------------------+
| Loader parameter            |
+-----------------------------+
| Secure SSBL                 |
+-----------------------------+
| Secure FWUpdate program     |
+-----------------------------+
| Key / KUK / public key area |
+-----------------------------+
| App parameter area          |
+-----------------------------+
| Flash management area       |
+-----------------------------+
| App Bank A secure package   |
+-----------------------------+
| App Bank B secure package   |
+-----------------------------+
```

The final addresses must be calculated from real flash capacity, erase block size, maximum App package size, and required alignment. Bank boundaries should be erase-block aligned and should not share erase blocks with SSBL/key/parameter areas.

Dual-bank boot policy:

| State | Behavior |
| --- | --- |
| Active bank valid | Boot it. |
| Active bank invalid but previous bank valid | Fall back to previous bank. |
| Both invalid | Stay in recovery/update mode. |
| Update interrupted | Continue using previous bank. |

## 10. Anti-Rollback Strategy

Anti-rollback should be added after secure boot, encryption, FWUpdate, and bank switching are stable.

Recommended design:

```text
Each secure App package has a monotonic version.
SSBL checks package version against OTP/version counter before boot.
FWUpdate only accepts packages newer than the current committed version.
Version counter is advanced only after a successful update policy decision.
```

Important caution:

- OTP/version counters are irreversible.
- Do not burn anti-rollback state during early bring-up unless the full update and recovery path is validated.
- Consider a development mode where version check is compiled in but OTP update is disabled.

## 11. Phased Implementation Plan

Do not implement all security features at once. The recommended sequence is:

### Phase 1: SSBL Security Foundation

Goal: Convert current Loader into Secure SSBL while keeping App plaintext.

Tasks:

```text
1. Port RSIP initialization and certificate verification from Renesas SecureSSBL sample.
2. Define secure App package address and certificate layout.
3. Generate signed plaintext App package.
4. Verify the package directly from xSPI Flash.
5. Keep current App manifest copy flow, but only trust it after package verification.
6. Add segment destination whitelist checks.
```

Exit criteria:

```text
Secure SSBL verifies a signed plaintext App package from xSPI and boots App system_init successfully.
```

### Phase 2: APP Encryption

Goal: Keep App payload encrypted in xSPI Flash.

Tasks:

```text
1. Extend secure manifest with per-segment encryption metadata.
2. Enable required RSIP AES functionality in SSBL.
3. Update package generation tool to encrypt segments and pad to block size.
4. Verify encrypted package first, then decrypt/copy segments.
5. Confirm no plaintext App remains in xSPI App bank except allowed metadata/certificates.
```

Exit criteria:

```text
xSPI stores encrypted App segments, SSBL decrypts them into RAM/SDRAM, and App boots normally.
```

### Phase 3: Secure FWUpdate

Goal: Add authenticated update of secure App package.

Tasks:

```text
1. Port or adapt Renesas FWUpdate program to PN2.0 package format.
2. Receive secure App package over the selected update channel.
3. Write package to staging area or inactive bank.
4. Verify package before marking it bootable.
5. Preserve recovery path on write failure or power loss.
```

Exit criteria:

```text
A new signed package can be installed without external flashing tools, and invalid packages are rejected.
```

### Phase 4: Dual-Bank Boot And Update

Goal: Make updates robust against interruption and bad images.

Tasks:

```text
1. Define App Bank A and App Bank B layout.
2. Add flash management state for active/pending/previous bank.
3. Make FWUpdate write only the inactive bank.
4. Make SSBL validate selected bank before boot.
5. Add fallback behavior when selected bank validation fails.
```

Exit criteria:

```text
Power loss during update does not brick the device, and SSBL can fall back to a previous valid bank.
```

### Phase 5: Anti-Rollback

Goal: Prevent booting or installing older validly signed firmware.

Tasks:

```text
1. Define version field in secure manifest/certificate policy.
2. Add SSBL version check against OTP/version counter.
3. Add FWUpdate version acceptance policy.
4. Update version counter only at the correct commit point.
5. Validate rollback rejection and recovery behavior.
```

Exit criteria:

```text
Older signed packages are rejected, and version state remains consistent across update and fallback scenarios.
```

## 12. Possible Limitations And Risks

### 12.1 RSIP Secure Boot API Is Continuous-Image Oriented

The Renesas secure boot API verifies an image described by the Code Certificate. It does not natively understand PN2.0's multi-section App manifest. SSBL must add secure manifest parsing and scatter-load logic.

### 12.2 Automatic Decrypt-And-Scatter Is Not Provided By One API

Renesas SB library can decrypt an encrypted image using Image Cipher Info, but the built-in model is a continuous source and destination. PN2.0 segment deployment needs SSBL-controlled per-segment decrypt/copy.

### 12.3 xSPI Direct Verification Depends On Memory-Mapped Read Stability

Directly verifying from xSPI Flash requires correct xSPI protocol mode, bus timing, MPU/cache settings, and 4-byte alignment. Any inconsistency can cause verification failure or hard faults.

### 12.4 SDRAM/HRAM Must Be Ready Before Segment Deployment

If App sections are deployed to SDRAM or HRAM, SSBL must initialize that memory before decrypting/copying those segments. This increases SSBL responsibility and must not conflict with App-side initialization.

### 12.5 AES Padding And Section Sizes Need Care

Encrypted segment sizes must satisfy cipher mode requirements. The package generator and SSBL must agree on `plain_size`, `cipher_size`, and padding behavior.

### 12.6 Key Management Must Be Planned Early

The design must define whether image encryption keys come from OTP secure boot common keys, wrapped keys in flash, or another RSIP-supported mechanism. Key injection and key update policy must be aligned with FWUpdate.

### 12.7 Debugging Becomes More Difficult After Encryption

Once App payload is encrypted in xSPI, map-file based debugging and flash inspection become less direct. Keep a plaintext signed bring-up mode until secure boot and section deployment are stable.

### 12.8 Dual-Bank Requires Enough Flash Capacity

PN2.0 App may be large. Dual-bank requires enough flash for two complete secure packages plus SSBL, FWUpdate, key, parameter, and management areas.

### 12.9 Anti-Rollback Can Permanently Block Test Images

OTP-based version counters are irreversible. Anti-rollback should be enabled late and tested carefully with a development policy first.

### 12.10 RASC Regeneration Risk

The PN2.0 project already contains custom loader/app split changes. RASC regeneration can overwrite ICF, project settings, generated config, or App/Loader handoff assumptions. Review generated changes before build.

## 13. PN2.0 Implementation Recommendation: Whole-Package Authentication + Section-Based Deployment

This section turns **方案 A: whole-package authentication with section-based deployment** into concrete implementation guidance for the PN2.0 Loader/App split project. The main goal is: **do not reshape the PN2.0 App into the Renesas sample's single contiguous RAM image model; keep the existing PN2.0 multi-section deployment model inside a cryptographically authenticated boundary**.

### 13.1 Difference Between PN2.0 And The Renesas Sample

| Item | Renesas SecureSSBL / SecureFWUpdate sample | Recommended PN2.0 design |
| --- | --- | --- |
| App shape | One continuous secure boot image. | One complete secure package containing a secure manifest and multiple segments. |
| Deployment | SSBL places one continuous image at one runtime address. | SSBL deploys multiple segments to ATCM/BTCM/System RAM/SDRAM/non-cache according to the manifest. |
| Authentication target | One continuous image described by the Code Certificate. | The Code Certificate describes the whole secure package; manifest and all segment payloads are authenticated. |
| Decryption model | Secure boot library can decrypt a continuous image. | Verify the whole package first; then PN2.0 SSBL decrypts each segment through public RSIP AES APIs or an equivalent RSIP-supported path. |
| Manifest | UserApp parameter only describes a few fields such as `src_addr/img_size/app_start_addr`. | Secure manifest explicitly describes each segment offset, destination, plain/cipher size, flags, IV/key info, and entry point. |
| FWUpdate content | Writes a sample-format UserApp secure boot image. | Writes a complete PN2.0 secure App package to inactive bank or staging area. |
| Security boundary | Suitable for small continuous images. | Suitable for large Apps, multiple RAM regions, and complex scatter loading. |

Therefore, the PN2.0 implementation should not try to make `R_RSIP_SB_ManifestVerify()` understand multiple sections. Instead, use it to **authenticate the complete package**, then let SSBL parse the authenticated manifest and safely deploy sections.

### 13.2 Authentication Boundary Of The Secure Package

The recommended approach is to make the Code Certificate image region a continuous package body:

```text
package_body = secure_manifest + all_segment_payloads
```

This keeps `R_RSIP_SB_ManifestVerify()` within the Renesas secure boot library's continuous-image model, while the inside of that continuous image is described by a PN2.0-defined manifest.

Recommended Flash layout:

```text
App Bank N
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+ <- Code Certificate dest_addr points here
| Secure Manifest             |
+-----------------------------+
| Segment 0 payload           |
+-----------------------------+
| Segment 1 payload           |
+-----------------------------+
| ...                         |
+-----------------------------+
```

The authentication range should include:

```text
Secure Manifest
All segment payloads
All fields that affect deployment: destination addresses, lengths, types, flags, entry point, version
```

Key principle: **SSBL must not trust any address, length, or entry point field from the secure manifest before `R_RSIP_SB_ManifestVerify()` succeeds.**

### 13.3 SSBL Main Flow Pseudocode

The following pseudocode keeps the current PN2.0 Loader responsibilities, but adds whole-package authentication and whitelist checks before using the manifest or moving segments.

```c
int secure_ssbl_main(void)
{
	const app_bank_t *bank;
	const key_cert_t *key_cert;
	const code_cert_t *code_cert;
	const secure_app_manifest_t *manifest;

	ssbl_init_clock_stack_bss();
	ssbl_init_xspi_memory_mapped_read();
	ssbl_init_sdram_if_required();

	if (R_RSIP_Open(&g_rsip_ctrl, &g_rsip_cfg) != FSP_SUCCESS)
	{
		enter_recovery_mode();
	}

	bank = select_boot_bank();
	key_cert  = (const key_cert_t *) bank->key_cert_addr;
	code_cert = (const code_cert_t *) bank->code_cert_addr;

	/* Code Certificate should describe the whole package body. */
	if (R_RSIP_SB_ManifestVerify(&g_rsip_ctrl,
								 (uint8_t *) key_cert,
								 KEY_CERT_SIZE,
								 (uint8_t *) code_cert,
								 CODE_CERT_SIZE) != FSP_SUCCESS)
	{
		bank = select_fallback_bank_or_recovery(bank);
		retry_or_enter_recovery(bank);
	}

	manifest = (const secure_app_manifest_t *) bank->manifest_addr;

	if (!validate_secure_manifest_header(manifest, bank))
	{
		enter_recovery_mode();
	}

	if (!validate_all_segments_against_whitelist(manifest, bank))
	{
		enter_recovery_mode();
	}

	if (!validate_entry_point(manifest->entry_point))
	{
		enter_recovery_mode();
	}

	for (uint32_t i = 0; i < manifest->segment_count; i++)
	{
		const secure_segment_t *seg = &manifest->segments[i];

		if (seg->flags & SEG_FLAG_ENCRYPTED)
		{
			rsip_aes_cbc_decrypt_segment(seg);
		}
		else
		{
			copy_plain_segment(seg);
		}

		clean_invalidate_cache_for_segment(seg);
	}

	R_RSIP_Close(&g_rsip_ctrl);

	dsb_isb_before_jump();
	jump_to_app_entry(manifest->entry_point);
}
```

### 13.4 Manifest And Segment Validation Pseudocode

Manifest validation should be completed before deployment starts, instead of discovering invalid later fields after memory writes have already begun.

```c
bool validate_all_segments_against_whitelist(const secure_app_manifest_t *manifest,
											 const app_bank_t *bank)
{
	if (manifest->segment_count == 0U ||
		manifest->segment_count > SECURE_APP_MAX_SEGMENTS)
	{
		return false;
	}

	if (!range_inside_bank(bank->manifest_addr,
						   manifest->total_package_size,
						   bank->package_start,
						   bank->package_limit))
	{
		return false;
	}

	for (uint32_t i = 0; i < manifest->segment_count; i++)
	{
		const secure_segment_t *seg = &manifest->segments[i];
		uintptr_t src = bank->package_body_addr + seg->src_offset;

		if (!range_inside_bank(src, seg->cipher_size, bank->package_start, bank->package_limit))
		{
			return false;
		}

		if (!range_inside_allowed_memory(seg->dst_addr, seg->plain_size, seg->segment_type))
		{
			return false;
		}

		if (address_overflows(seg->dst_addr, seg->plain_size))
		{
			return false;
		}

		if ((seg->flags & SEG_FLAG_ENCRYPTED) && ((seg->cipher_size % 16U) != 0U))
		{
			return false;
		}

		if (segments_illegally_overlap(manifest, i))
		{
			return false;
		}
	}

	return true;
}
```

The whitelist should be a fixed SSBL-owned table, not something fully controlled by the manifest:

```c
static const memory_window_t g_allowed_windows[] =
{
	{ SEG_LDR_PRG,        APP_BTCM_START,       APP_BTCM_END       },
	{ SEG_VECTOR,         APP_ATCM_VECTOR_START,APP_ATCM_VECTOR_END},
	{ SEG_USER_PRG,       APP_SYSRAM_START,     APP_SYSRAM_END     },
	{ SEG_SYSTEM_PRG,     APP_SDRAM_START,      APP_SDRAM_END      },
	{ SEG_NONCACHE,       APP_NCACHE_START,     APP_NCACHE_END     },
	{ SEG_SHARED_NCACHE,  SHARED_NCACHE_START,  SHARED_NCACHE_END  },
};
```

### 13.5 Segment Decrypt/Copy Pseudocode

For PN2.0, treat the secure boot library decryption path as a continuous-image capability. Do not rely on it to perform multi-section scatter loading. Segment decryption should be driven by SSBL using segment records.

```c
static bool deploy_segment(const secure_segment_t *seg, const app_bank_t *bank)
{
	uint8_t *src = (uint8_t *)(bank->package_body_addr + seg->src_offset);
	uint8_t *dst = (uint8_t *) seg->dst_addr;

	if (seg->flags & SEG_FLAG_ENCRYPTED)
	{
		rsip_wrapped_key_t image_key = get_image_decrypt_key(seg->key_slot);

		if (!rsip_aes_cbc_decrypt(&image_key,
								  seg->iv,
								  src,
								  seg->cipher_size,
								  dst))
		{
			return false;
		}

		if (seg->cipher_size > seg->plain_size)
		{
			clear_padding_or_tail(dst + seg->plain_size,
								  seg->cipher_size - seg->plain_size);
		}
	}
	else
	{
		memcpy(dst, src, seg->plain_size);
	}

	return true;
}
```

The exact RSIP API name, key type, and wrapped-key storage policy must be confirmed against the selected FSP/RSIP configuration. Start with a signed plaintext package first; after whole-package verification and segment copy are stable, enable AES-CBC segment decryption.

### 13.6 FWUpdate-Side Pseudocode

PN2.0 FWUpdate should write a complete secure App package instead of a bare binary. After writing the inactive bank, verify the package before switching boot metadata.

```c
update_result_t pn20_secure_fwupdate(const uint8_t *rx_package, uint32_t size)
{
	app_bank_t *inactive = select_inactive_bank();

	if (!basic_package_size_check(rx_package, size, inactive))
	{
		return UPDATE_REJECTED;
	}

	erase_bank(inactive);
	write_package_to_bank(inactive, rx_package, size);

	if (!verify_package_in_bank(inactive))
	{
		mark_bank_invalid(inactive);
		return UPDATE_REJECTED;
	}

	if (!check_package_version_policy(inactive))
	{
		mark_bank_invalid(inactive);
		return UPDATE_ROLLBACK_REJECTED;
	}

	mark_bank_pending(inactive);
	request_reboot();

	return UPDATE_ACCEPTED;
}
```

`mark_bank_pending()` must be one of the last actions. If power is lost while the package is being written, the old active bank should remain valid. If power is lost after pending state is written, SSBL must still re-authenticate the pending bank at next boot and fall back to the previous bank if validation fails.

### 13.7 Recommended Implementation Sequence

To reduce risk, implement and verify in this order:

```text
1. Keep the current PN2.0 non-secure segment manifest and first freeze it as a fixed ABI.
2. Make the PC tool generate a signed plaintext secure package whose segment payloads are still plaintext.
3. Make SSBL authenticate the package body from xSPI.
4. After authentication succeeds, parse the secure manifest and validate every segment against whitelists.
5. Make SSBL copy plaintext payloads by segment and confirm the App boots.
6. Add per-segment AES-CBC encryption and padding in the PC tool.
7. Add per-segment RSIP AES decrypt-to-destination in SSBL.
8. Change FWUpdate to write the complete secure package to inactive bank.
9. Add the dual-bank pending/active/previous state machine.
10. Add anti-rollback version policy last.
```

This sequence keeps each step testable and avoids changing whole-package authentication, section deployment, decryption, and update state machine behavior at the same time.

## 14. Recommended Immediate Next Steps

1. Freeze the current non-secure App manifest ABI and record exact section address rules.
2. Define the secure manifest structure and package generator output format.
3. Build a signed plaintext package first.
4. Modify SSBL to verify the package from xSPI before using the manifest.
5. Add strict segment destination whitelist checks.
6. Only then enable segment encryption and RSIP AES decrypt-copy.

The main engineering principle is:

```text
Authenticate the complete package first.
Trust only authenticated manifest fields.
Deploy only to fixed, whitelisted memory regions.
Add encryption, update, dual-bank, and anti-rollback in separate phases.
```
