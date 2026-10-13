# PN2.0 RZN2L Scheme A Phase 0 Baseline

## Objective

Prepare the existing PN2.0 RZN2L Secure Boot project for Scheme A without changing board provisioning, IAR/RASC configuration, linker layout, or Loader/App source in this phase.

The target design is a single-App-bank secure package with an authenticated `RZSM` manifest and per-segment payloads. Each encrypted segment uses AES-128-CBC with its own 16-byte IV. The initial encrypted candidate will be a controllable, non-startup-critical portion of `SYSTEM_PRG` or `SYSTEM_DATA`; all other segments remain plaintext until the deployment path is demonstrated on the board.

## Frozen Working Baseline

| Item | Current value | Evidence |
| --- | --- | --- |
| Git revision | `7cf9d0f18403a22b568cb1110128b48e06e9d23a` | Captured 2026-09-17. |
| Current active scheme | Scheme B / `SSBL_CFG_SECURE_APP_SCHEME_OVERALL_APP` | `RZN2L_bsp_xspi0bootx1_loader/src/secure_ssbl_config.h` |
| Secure App package base | `0x60100050` | `RZN2L_bsp_xspi0bootx1_loader/src/secure_app_package.h` |
| Key Certificate size | `0xE0` | `secure_app_package.h` |
| Code Certificate size | `0x120` | `secure_app_package.h` |
| Authenticated package body base | `0x60100250` | `secure_app_package.h` |
| Existing RZSM ABI | version `0x00010000`, 64-byte header, up to 16 entries | `src/secure_app_manifest.h` |
| Legacy App manifest | `RZAP`, 9 segment records | `RZN2L_bsp_xspi0bootx1_app/src/app_manifest.c` |
| App entry point | `system_init` | `app_manifest.c` |
| RSIP package verification | enabled | `secure_ssbl_config.h` |
| Secure package deployment | enabled | `secure_ssbl_config.h` |
| Legacy fallback | disabled | `secure_ssbl_config.h` |

Two dirty files existed when this baseline was recorded:

```text
RZN2L_bsp_xspi0bootx1_loader/settings/RZN2L_bsp_xspi0bootx1_loader.dnx
settings/RZN2L_bsp_xspi0bootx1_loader_app.wsdt
```

They are user-owned debug/workspace changes and are excluded from Scheme A work.

## Locked Security Boundaries

- The outer Renesas Key Certificate and Code Certificate must authenticate the complete `RZSM manifest + every payload` body before any RZSM field is read or used.
- Package format is single App bank for the first implementation. Segment sources use package-relative offsets, not App-link-time absolute Flash addresses.
- `package_version` is retained in the RZSM header but anti-rollback behavior is not enabled in this release.
- On any verification, manifest, range, AES, or deployment failure, Secure SSBL stops and must not fall back to an unsigned legacy table.
- Do not write OTP, inject keys, change `SBEN`, enable Secure Boot, or disable SCI/USB Boot.
- Do not emit AES keys, PEM keys, wrapped key material, or their contents into source, logs, package manifests, or this document.

## Existing Destination Allowlist

Scheme A must preserve the running App's current destination layout, already implemented in `RZN2L_bsp_xspi0bootx1_loader/src/secure_app_deploy.c`.

| Segment | Allowed runtime destination |
| --- | --- |
| `LDR_PRG` | `0x0011A000 .. 0x0011FFFF` |
| `LDR_DATA` | `0x00118000 .. 0x00119FFF` |
| `VECTOR` | `0x00000000 .. 0x0001FFFF` |
| `USER_PRG`, `USER_DATA` | `0x10000100 .. 0x1012FFFF` |
| `SYSTEM_PRG`, `SYSTEM_DATA` | `0x74000000 .. 0x741FFFFF` |
| `NONCACHE` | `0x30160000 .. 0x3017FFFF` |
| `SHARED_NONCACHE_BUFFER` | `0x30150000 .. 0x3015FFFF` |

`SYSTEM_PRG` and `SYSTEM_DATA` require the existing HyperRAM/SDRAM initialization before deployment. App `system_init` remains the only permitted application transfer target after all deployment and cache maintenance complete.

## AES Key And IV Decision

Use the existing RZ/N2L Secure Boot common key already provisioned as `ckey0`; do not create a new OTP key path for this work.

The host-side key input and packaging model are defined by the installed Renesas R01AN6526 material:

```text
C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\work_secureboot_rzn2l\secureboot_common_key.bin
C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\secureboot_utility.py
```

The established tool invocation uses `--enckey secureboot_common_key.bin --keyselect ckey0`; the secureboot hands-on guide states that `secureboot_common_key.bin` is the AES-128-CBC key corresponding to injected OTP `ckey0`. The current board's `ckey0` provenance must be confirmed before producing an encrypted test package.

Each encrypted RZSM entry will carry a unique random 16-byte CBC IV. The IV is metadata, not secret, but it is authenticated by the outer signed package. The same AES-128 key may be used across entries only when IVs are unique for every encryption operation.

## Current Gaps Before Stage 1

1. RASC currently generates `RSIP_CFG_AES_128_ENABLE (0)` and `RSIP_CFG_AES_ECB_CBC_CTR_ENABLE (0)`. AES-CBC API calls cannot be used until those options are enabled and code is regenerated.
2. The existing RZSM ABI has no explicit `cipher_size`, IV, or AES key-selector fields. Stage 1 must version the ABI before any package is generated.
3. The current `secure_app_deploy.c` flag allowlist excludes `SECURE_APP_SEGMENT_FLAG_ENCRYPTED`; it only deploys the established plaintext RZSM path.
4. The current Loader does not load a reusable wrapped AES key for `R_RSIP_AES_Cipher_Init()`. Stage 1 must determine whether the verified Code Certificate/RSIP secure-boot flow exposes the `ckey0` key for manual segment decryption, or whether a supported Renesas key-wrap/import path is required.
5. Current RZSM source validation uses the fixed package body base and is single-bank compatible, which matches this phase's scope.

## Stage 1 ABI Status

The Loader now defines RZSM ABI version `0x00020000`. The header remains 64 bytes and each segment entry is fixed at 96 bytes, enforced by compile-time size guards.

Each entry now includes:

```text
file_size          Plaintext bytes deployed to the destination.
cipher_size        Payload bytes in the package; equals file_size for plaintext.
aes_key_selection  NONE for plaintext or CKEY0 for AES-128-CBC encrypted payloads.
aes_cbc_iv[16]     Unique, nonzero 16-byte CBC IV for each encrypted segment.
```

The current validation rejects an encrypted entry with a zero or reused IV, a non-`ckey0` selector, a zero or non-16-byte-aligned cipher size, or a cipher size smaller than the plaintext. Plaintext entries must declare `cipher_size == file_size` and key selection `NONE`.

The current copy-only deployment function explicitly stops when it encounters an encrypted entry. This is intentional: AES-CBC decrypt-to-destination and the supported `ckey0` key acquisition path are not yet implemented, so ciphertext cannot be copied into an App runtime region.

## Stage 0 Exit Criteria

- Current Scheme B baseline and address contract are recorded.
- Scheme A initial scope is single bank, one non-critical `SYSTEM_PRG` or `SYSTEM_DATA` encryption candidate, no anti-rollback.
- Per-segment unique IV policy is recorded.
- Existing destination allowlist is frozen as the Scheme A allowlist source.
- Existing `ckey0` is the only permitted AES key source; provisioning is explicitly excluded.
- No IAR/RASC configuration, linker, source, board, OTP, or Flash content has changed.

## Next Approval Required

The approved IAR/RASC generation step has enabled AES-128 and ECB/CBC/CTR. The next step requires a manual IAR Loader compile to validate the regenerated RSIP sources and the new ABI guards. No automatic IAR build is permitted.
