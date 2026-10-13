---
name: rzn2l-pn20-secure-boot
description: "Use when working on RZN2L PN2.0 Secure Boot, SecureSSBL, RSIP, Loader + App split, RZAP manifest, xSPI image signing, secure firmware update, or IAR Loader/App debug and memory layout. Guides safe analysis, implementation, and validation of the established RZN2L workflow."
argument-hint: "Describe the Secure Boot, Loader/App, signing, RSIP, IAR, or update task"
user-invocable: true
---

# RZN2L PN2.0 Secure Boot

Use this skill for the RZN2L PN2.0 workflow that splits the original xSPI boot reference project into an independent Secure SSBL (Loader) and PROFINET IRT App.

## Scope And Safety Boundaries

- Treat the current verified deployment as Scheme B: a Renesas native secure image signs the entire App body, including the legacy `RZAP` manifest and the App raw image.
- Do not read or trust manifest addresses, lengths, segment records, or `entry_point` until `R_RSIP_SB_ManifestVerify()` has succeeded.
- Never replace the PN2.0 multi-segment deployment with a single-RAM-image assumption. The App runs across BTCM, ATCM, System RAM, SDRAM/HRAM, and non-cache areas.
- Do not bypass the App LDR by jumping to `main`; after deployment, jump to the authenticated App `system_init` entry point.
- Treat OTP key writes, secure-boot enablement, and SCI/USB Boot disablement as irreversible provisioning. Do not propose or run these operations as part of routine validation.
- Do not run IAR builds automatically. Ask the user to build Loader and App in IAR when build artifacts are required.
- Before changing IAR project, linker, or debug configuration files (`.ewp`, `.icf`, `.xcl`, `.cspy`, `.dnx`, `.dbgdt`), state the recommendation and obtain explicit user approval.

## Start Here

1. Identify the requested task: architecture/porting, Secure SSBL implementation, App image packaging, IAR/debug configuration, RSIP/FSP regeneration, or update design.
2. Read [reference-map.md](./references/reference-map.md), then load only the linked project document relevant to the task.
3. Inspect the controlling source or configuration first. Do not infer addresses, artifact names, or build configurations from historical documentation when the current project says otherwise.
4. State the active scheme, Flash package address, App manifest address, and relevant Loader/App configuration before proposing a code or configuration change.

## Established Architecture

```text
BootROM
  -> authenticates and starts Secure SSBL
  -> Secure SSBL initializes xSPI and required external RAM
  -> Secure SSBL verifies the complete secure App package with RSIP
  -> Secure SSBL validates authenticated RZAP manifest boundaries and destinations
  -> Secure SSBL deploys each App segment to its runtime memory
  -> Secure SSBL performs cache/barrier handling
  -> jumps to authenticated App system_init
  -> App startup enters main and FreeRTOS/application tasks
```

The App package begins at the established App Flash partition. Keep the complete package address, certificate body address, legacy RZAP manifest location, App ICF layout, and Loader read logic mutually consistent. Do not hard-code new values without confirming them in the active ICF, package-generation command, and Loader source.

## Workflows

### Diagnose Or Implement Secure SSBL

1. Verify `SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE` is enabled and that `secure_app_verify_package()` actually executes.
2. Confirm xSPI memory-mapped access and SDRAM/HRAM initialization happen before verification/deployment when required by a destination segment.
3. Verify the package before parsing the manifest.
4. Before each copy/decrypt, validate package source range, arithmetic overflow, destination whitelist, alignment, required nonzero size, illegal overlap, segment type, and entry-point range.
5. Ensure App LDR BTCM destinations do not overwrite the currently executing Loader/SSBL code or data.
6. After deployment, perform required cache maintenance and `DSB`/`ISB`, then transfer control to the authenticated App `system_init`.

### Modify App Layout Or Manifest

1. Inspect the App ICF and `app_manifest.c` together; a manifest change without a matching linker layout is invalid.
2. Preserve a fixed manifest location in the App raw image for integrated Loader + App operation.
3. Keep the App LDR BTCM offset separated from the Loader/SSBL BTCM region.
4. Confirm each manifest source section and destination section exists and that zero-size optional sections are safely handled.
5. Ask for approval before editing ICF or IAR project settings.
6. Ask the user to rebuild App first, then Loader/SSBL, and verify the new maps and packaged artifact are aligned.

### Package And Flash A Verified Image

1. Use the current App and Loader artifacts, never a stale binary.
2. Pad the Loader binary to the tool-required alignment using its actual size.
3. Generate the Loader parameter and signed Secure SSBL using the configured Root Key.
4. Generate a complete Secure App package; write the package, not only the App parameter or raw App binary.
5. Confirm the Secure SSBL and Secure App public-key artifacts have identical SHA-256 hashes before any Flash write.
6. Keep Loader parameter, Secure SSBL, and Secure App package at their distinct documented xSPI addresses.
7. For routine board verification, use only reversible Flash-writing and normal-boot steps. Exclude provisioning operations.

### IAR, RASC, And RSIP Changes

1. Before proposing edits, inspect current `.ewp`, `.icf`, `.xcl`, and debug configuration paths for stale absolute paths.
2. Keep the Loader independent from the App package: do not link the App `.bin` into the secure Loader through `IlinkRawBinary` or `image_input`; package and write the signed App separately.
3. Ensure RSIP configuration is represented in `configuration.xml` so RASC regeneration preserves it.
4. Place copied RSIP source files in a top-level custom IAR group rather than the RASC-managed `Flex Software` group.
5. Check that RSIP private/public/primitive include paths exist in both IAR include settings and `xcl/rasc_iccarm.xcl`.
6. Obtain approval before changing IAR configuration, then ask the user to regenerate/build with IAR/RASC and review generated diffs.

### Secure Firmware Update Design

1. Keep the update unit as one complete secure App package.
2. Write it to an inactive bank or staging area; validate its structure and authenticity before marking it active.
3. At boot, verify the selected bank again and fall back to the previously valid bank when dual-bank support is enabled.
4. Bind package version, destination layout, entry point, and payloads to authenticated data. Plan anti-rollback before enabling production update behavior.

## Completion Checks

Report these facts, with evidence from the active project or produced artifacts:

- active package scheme and trust boundary;
- Flash locations of the Loader parameter, Secure SSBL, and App package;
- App manifest address and App entry point;
- validation status of manifest bounds and destination allowlists;
- whether the App/Loader build artifacts are current and mutually aligned;
- whether board provisioning was intentionally excluded;
- exact manual IAR, Python-tool, or UART-log checks still needed.

## Reference Material

Read [reference-map.md](./references/reference-map.md) for the project documents and the purpose of each one.