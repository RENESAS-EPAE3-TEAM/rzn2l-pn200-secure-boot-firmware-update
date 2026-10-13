# make_secure_app_package.py User Guide

English version of [make_secure_app_package.md](make_secure_app_package.md).

Encrypted-segment packaging requires the Python package `pycryptodome`. The project's Python 3.12 environment already provides this package. Install it first when using another packaging machine:

```powershell
python -m pip install pycryptodome
```

The repository's `secureboot_common_key.bin` is a publicly disclosed demo key. Use it only for demonstrations, never for production devices. Sections 10 and 11 contain dated generation and validation records; use Section 11 for the all-segment workflow and the latest generated summaries for current sizes and offsets.

## 1. Purpose

`make_secure_app_package.py` creates PN2.0 Secure App files, including:

- A legacy RZAP manifest copy or a Secure Manifest.
- A Package Body.
- A Signed Package.

The script reads the PN2.0 App raw binary and parses its existing legacy `RZAP app_manifest_t`.

The current default is Scheme A: `--scheme rzsm`. This mode generates the RZSM v2 ABI used by the Loader, with a 64-byte header and 96-byte entries. It uses package-relative source offsets and calculates SHA-256 for each active segment [1].

To use Scheme B, explicitly specify `--scheme overall-app`. This mode does not generate a custom RZSM package body. Instead, it treats the PN2.0 App raw binary as one continuous overall App body, authenticated as a whole through the Renesas Key Certificate and Code Certificate.

Scheme B generates the following structure:

```text
Package Body = PN2.0 App raw binary
             = legacy RZAP manifest + App raw image
```

Scheme A/RZSM generates the following structure:

```text
Package Body = Secure Manifest + padding + ordered enabled segment payloads
```

The complete Signed Package has the following structure:

```text
Signed Package = Key Certificate + Code Certificate + Package Body
```

The Signed Package is the complete Flash/update object consumed by the SSBL [1].

---

## 2. Runtime Environment

The script is a Python 3 script. The author used Python 3.12.0. Its shebang is:

```python
#!/usr/bin/env python3
```

Run it with Python 3:

```bash
python3 make_secure_app_package.py [arguments]
```

On Windows, you can also use:

```powershell
python make_secure_app_package.py [arguments]
```

---

## 3. Minimal Usage: Scheme A/RZSM Body

The minimum required arguments are:

- `--app-bin`
- `--manifest-out`

Example:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin
```

Arguments:

| Argument | Required | Description |
| --- | --- | --- |
| `--app-bin` | Yes | Input PN2.0 App raw binary |
| `--manifest-out` | Yes | Output Secure Manifest binary |
| `--body-out` | No | Output Scheme A/RZSM package body |

This example generates:

```text
secure_manifest.bin
package_body.bin
```

The script prints information such as the scheme, segment count, and entry point [1].

To generate a Scheme B overall App body, use:

```bash
python3 make_secure_app_package.py \
  --scheme overall-app \
  --app-bin app_raw.bin \
  --body-out package_body.bin
```

---

## 4. Generating a Package Body

Add `--body-out` to generate a Package Body:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin
```

The generated `package_body.bin` has the following structure:

```text
Secure Manifest + padding + ordered enabled segment payloads
```

RZSM v2 extracts every enabled, nonzero-length segment from the input App's legacy RZAP and writes the payloads compactly in RZAP entry order. Each RZSM entry's `src_offset` is the authoritative location of that segment in the new body. The legacy RZAP is only a packaging input and is not retained in the RZSM body. You can encrypt one known segment or use `--encrypt-all` to encrypt all active segments, without an additional RAM staging buffer.

The padding before the payload area uses `0xFF` up to the aligned payload offset [1].

## 4.1 Generating One ckey0 AES-CBC Encrypted Segment

`--aes-key` specifies a 16-byte AES-128 host-side key file that must match the provisioned OTP `ckey0`. It is used only on the packaging host and must not be copied into the Loader, App, or Flash package. `--encrypt-segment` accepts a known segment name or ID. Additional encrypted segment types require the updated Loader described in Section 11.

Encrypted segments are zero-padded to the next 16-byte AES block boundary; PKCS#7 is not used. An entry's `file_size` always represents the original plaintext length, while `cipher_size = align_up(file_size, 16)`. The Loader writes back only `file_size` bytes, so padding does not overwrite adjacent runtime memory.

```powershell
python .\make_secure_app_package.py `
  --scheme rzsm `
  --app-bin $appRaw `
  --manifest-out .\rzsm_v2_manifest.bin `
  --body-out .\rzsm_v2_body.bin `
  --encrypt-segment SYSTEM_DATA `
  --aes-key .\secureboot_common_key.bin `
  --summary-out .\rzsm_v2_summary.json
```

Each run generates a new random 16-byte IV for the encrypted segment and stores it in the authenticated RZSM entry. Before producing output, the script decrypts the ciphertext with the same host key, compares it byte for byte with the original App segment, and verifies that every padding byte is zero.

## 4.2 Signing the RZSM Body with the 6526/6527 Tools

6526 and 6527 refer to the official example packages `r01an6526ej0310-rzt2-n2-security-secureboot` and `r01an6527ej0310-rzt2-n2-security-fwupdate`, respectively.

`rzsm_v2_manifest.bin` is a copy of the body's manifest prefix for inspecting the RZSM ABI. It is not the signing input for the Renesas tools. Pass the complete `rzsm_v2_body.bin` to both official tools. This file consists of `RZSM manifest + padding + ordered segment payloads` and already contains the selected segment's AES-CBC ciphertext.

The generator pads the complete body with `0x00` to a multiple of 16 bytes and records that padded length in `RZSM package_size`. The official tools therefore do not change the body length, and the Code Certificate's `img_len` can strictly match the Loader's Scheme A checks.

Run the following in the 6526 `pn20_secure_ssbl` tool directory, adjusting the paths to match your local installation. The 6527 tools use the same `parameter_generator.py userapp` and `secureboot_utility.py image` mechanism with the same arguments:

```powershell
$body = "C:\path\to\rzsm_v2_body.bin"
$param = ".\pn20_rzsm_app_param.bin"
$signed = ".\pn20_secure_rzsm_app_package.bin"

python .\parameter_generator.py userapp `
  --src_addr 60100050 `
  --app_start_addr 60100250 `
  --secureboot `
  -i $body `
  -o $param

python .\secureboot_utility.py image `
  -i $body `
  -o $signed `
  --param $param `
  --rkey .\rootkey-pair.pem `
  --ikey .\loaderkey-pair.pem `
  --sign_target cert-img `
  --ver 0
```

Do not add `--enckey` to this outer `image` command. It would encrypt the entire RZSM body again, preventing the Secure SSBL from reading the RZSM header after authentication. Segment-level AES-CBC encryption is already performed by this script. `--keyselect ckey0` is also unnecessary for the outer plaintext body; it populates the Code Certificate's Image Cipher Info only when outer image encryption is enabled.

Finally, use this script to check the complete package's outer length and RZSM body:

```powershell
python .\make_secure_app_package.py `
  --scheme rzsm `
  --inspect-signed-package $signed `
  --summary-out .\pn20_rzsm_signed_summary.json
```

---

## 5. Using the Legacy `--package-out` Argument

`--package-out` is a legacy alias for `--body-out` [1].

The following commands are equivalent:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin
```

Or:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --package-out package_body.bin
```

If both `--body-out` and `--package-out` are specified with different paths, the script reports an error [1].

---

## 6. Generating a Complete Signed Package

To generate a complete signed package, provide all three arguments together:

- `--key-cert`
- `--code-cert`
- `--signed-package-out`

Example:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --key-cert key_cert.bin \
  --code-cert code_cert.bin \
  --signed-package-out signed_package.bin
```

The output files are:

```text
secure_manifest.bin
package_body.bin
signed_package.bin
```

`signed_package.bin` has the following structure:

```text
Key Certificate + Code Certificate + Package Body
```

The script requires `--key-cert`, `--code-cert`, and `--signed-package-out` to be supplied together; otherwise, it reports an error [1].

---

## 7. Certificate Size Requirements

The script defines fixed certificate sizes:

| Certificate | Size |
| --- | ---: |
| Key Certificate | `0xE0` bytes |
| Code Certificate | `0x120` bytes |

If an input certificate has a different size, the script reports an error [1].

---

## 8. Code Certificate Layout Checks

By default, the script checks that the Code Certificate's layout information matches the generated body, including:

- `dest_addr`
- `img_size`

`dest_addr` must equal:

```text
package_base + KEY_CERT_SIZE + CODE_CERT_SIZE
```

`img_size` must equal the generated body length in bytes, not its address.

A mismatch causes the script to report an error [1].

To disable this check, add:

```bash
--no-cert-layout-check
```

Example:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --key-cert key_cert.bin \
  --code-cert code_cert.bin \
  --signed-package-out signed_package.bin \
  --no-cert-layout-check
```

---

## 9. Generating a JSON Summary

Use `--summary-out` to generate a JSON summary:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --summary-out summary.json
```

To also generate a signed package:

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --key-cert key_cert.bin \
  --code-cert code_cert.bin \
  --signed-package-out signed_package.bin \
  --summary-out summary.json
```

## 10. Scheme A: Encrypted Secure SSBL Signing, Flash Programming, and Single-Segment Encryption Acceptance

This section applies to a Loader Debug/Scheme A build that has already been compiled.

"SYSTEM_DATA-only encryption" means that the complete App package still contains all seven enabled segments, but only SYSTEM_DATA is encrypted. It does not mean a package containing only SYSTEM_DATA. A package missing required segments such as LDR_PRG and VECTOR cannot boot the App correctly.

### 10.1 Recorded Inputs and Safety Preconditions

- Loader Debug uses `SSBL_CFG_SECURE_APP_SCHEME_RZSM` and `USE_HRAM`, with signature verification, deployment, and UART logging enabled.
- Use the latest Debug Loader `.bin`. The input signed in this record was the 51,924-byte file dated 2026-10-09 16:16:34.
- The `rzsm_v2_body.bin` recorded here was 1,569,520 bytes and passed offline segment-byte, CBC, and outer-signature verification.
- On 2026-10-09, the host key was replaced with the author-specified `work_secureboot_rzn2l\secureboot_common_key.bin`, and the App and SSBL were regenerated. The default 6526 `Key Data` AES key is no longer used. Root/Image signing keys still come from the `pn20_secure_ssbl` directory specified in the PN2.0 validation document.
- The SSBL is encrypted as a whole image and is authenticated and decrypted by BootROM before startup. The App's outer image is not encrypted; the SSBL authenticates it and then decrypts only SYSTEM_DATA. These two encryption layers are independent, but use the same ckey0 in this record.
- The board must already have the corresponding Root Public Key and OTP ckey0 provisioned. The host AES key must match that ckey0. A host-side encryption/decryption round trip does not prove that the OTP key matches.
- This workflow updates Flash only. It does not write OTP, run `setboot --enable`, or disable SCI/USB Boot.
- Flash programming replaces the existing Loader and App.

### 10.2 Defining Paths

Open a PowerShell session and run the following commands one at a time in the same session. After each Python command, stop and resolve the error if `$LASTEXITCODE` is nonzero.

```powershell
$workspace = "C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project"
$toolDir = "C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl"
$keyDir = "C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\work_secureboot_rzn2l"
$tools = "$workspace\tools"
$aesKey = "$tools\secureboot_common_key.bin"
$raw = "$workspace\RZN2L_bsp_xspi0bootx1_loader\Debug\Exe\RZN2L_bsp_xspi0bootx1_loader.bin"
$padded = "$tools\pn20_scheme_a_ssbl_padded.bin"
$loaderParam = "$tools\pn20_scheme_a_ssbl_parameter_xspi0.bin"
$ssbl = "$tools\pn20_scheme_a_secure_ssbl_xspi0.bin"
$body = "$tools\rzsm_v2_body.bin"
$appParam = "$tools\pn20_rzsm_app_param.bin"
$appPackage = "$tools\pn20_secure_rzsm_app_package.bin"
Set-Location $workspace
```

The official tools write `.log` and `.pubkey` files to the command's working directory, not the directory specified by `-o`. This section uses the project root as the working directory.

Copy the host AES key from the specified custom experiment directory, or otherwise ensure that it matches the board's OTP ckey0. Do not choose the default example key merely because it has the same filename. Do not print the key contents:

```powershell
if ([System.IO.File]::ReadAllBytes("$keyDir\secureboot_common_key.bin").Length -ne 16) { throw "Expected 16-byte AES key" }
[System.IO.File]::Copy("$keyDir\secureboot_common_key.bin", $aesKey, $true)
```

### 10.3 Complete Commands to Reproduce Loader Signing

The following SSBL files were generated for this record. They can be reused if the Loader has not been rebuilt. After any rebuild, repeat padding, parameter generation, and signing; do not reuse the old SSBL.

Pad the actual raw length to a multiple of 512 bytes using `0xFF`. Do not hard-code the length:

```powershell
$rawBytes = [System.IO.File]::ReadAllBytes($raw)
$paddedSize = [int]([Math]::Ceiling($rawBytes.Length / 512.0) * 512)
$paddedBytes = [byte[]]::new($paddedSize)
for ($index = 0; $index -lt $paddedSize; $index++) { $paddedBytes[$index] = 0xFF }
[Array]::Copy($rawBytes, $paddedBytes, $rawBytes.Length)
[System.IO.File]::WriteAllBytes($padded, $paddedBytes)
"Raw={0} Padded={1}" -f $rawBytes.Length, $paddedSize
```

Expected for this record: Raw=51924, Padded=52224, with 300 bytes of padding.

```powershell
python "$toolDir\parameter_generator.py" loader `
  --mpu rzn2l --target_cpu cr52 `
  --src_addr 60000050 --dest_addr 00102000 `
  --mode xspi0 --secureboot -i $padded -o $loaderParam
```

The tool's reminder that the actual Loader program address should be `dest_addr + 0x800 = 0x00102800` is expected. It must match this Loader's linked layout.

```powershell
python "$toolDir\secureboot_utility.py" image `
  -i $padded -o $ssbl --param $loaderParam `
  --rkey "$toolDir\rootkey-pair.pem" `
  --ikey "$toolDir\loaderkey-pair.pem" `
  --enckey $aesKey --keyselect ckey0 `
  --sign_target cert-img --ver 0
```

For the SSBL, `--enckey` enables AES encryption, and the default `enc-sign` order signs the certificates and ciphertext. The complete SSBL length in this record is `52224 + 0x800 = 54272` bytes. Offline verification confirmed that the decrypted SSBL is byte-for-byte identical to the Debug Loader used for this record, including all `0xFF` padding.

### 10.4 Complete RZSM App Outer Signing and Pre-Programming Checks

The App package in this record was encrypted with the new host key and signed. If only the Loader is rebuilt and neither the App nor host key changes, the App body does not need to be regenerated. When changing the host key, regenerate the SYSTEM_DATA ciphertext and manifest, then sign again. Simply re-signing the old body is insufficient. The complete reproduction commands are:

```powershell
python "$tools\make_secure_app_package.py" --scheme rzsm `
  --app-bin "$tools\RZN2L_bsp_xspi0bootx1_app.bin" `
  --manifest-out "$tools\rzsm_v2_manifest.bin" `
  --body-out $body --encrypt-segment SYSTEM_DATA --aes-key $aesKey `
  --summary-out "$tools\rzsm_v2_summary.json"
```

```powershell
python "$toolDir\parameter_generator.py" userapp `
  --src_addr 60100050 --app_start_addr 60100250 `
  --secureboot -i $body -o $appParam
```

```powershell
python "$toolDir\secureboot_utility.py" image `
  -i $body -o $appPackage --param $appParam `
  --rkey "$toolDir\rootkey-pair.pem" `
  --ikey "$toolDir\loaderkey-pair.pem" `
  --sign_target cert-img --ver 0
```

Do not pass `--enckey`. SYSTEM_DATA is already encrypted inside the body, and the outer RZSM body must remain directly readable.

```powershell
python "$tools\make_secure_app_package.py" --scheme rzsm `
  --inspect-signed-package $appPackage `
  --summary-out "$tools\pn20_rzsm_signed_summary.json"
```

For this record, expect body=1569520, segments=7, and entry=0x0011A000. This command checks structure, not cryptographic signatures. Inspect the summary: only id=6 should have flags `0x0000000B`; all other segments should have `0x00000009`.

```powershell
Get-Content "$workspace\pn20_scheme_a_secure_ssbl_xspi0.log"
Get-Content "$workspace\pn20_secure_rzsm_app_package.log"
Get-FileHash "$workspace\pn20_scheme_a_secure_ssbl_xspi0.pubkey", "$workspace\pn20_secure_rzsm_app_package.pubkey"
Get-Item $loaderParam, $ssbl, $appPackage
```

Both logs must show `cert-img`. The SSBL log must show `Image encryption: Enable` and `Key select: ckey0`; the App log must show `Image encryption: Disable`. The two `.pubkey` files must be identical. They contain the Root Public Key hash, but matching each other alone does not prove that they match OTP. Continue using the board's previously verified key chain.

### 10.5 Downloading the Device Setup Program over SCI

First close all serial windows using the programming COM port. Select SCI Boot Mode according to the board manual, then reset or power-cycle the board before running the command. Do not assume the COM number or switch positions from historical records.

```powershell
$COM = "COM3" # Replace with the actual port shown in Device Manager
$setupSrec = "$toolDir\..\rzn2l\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec"
python "$toolDir\secure_device_setup.py" start `
  --port $COM --boot_mode sci -i $setupSrec
```

Continue only after `start` succeeds. If it reports `Failed to send the program file.`, check the boot mode, port usage, and reset timing. Do not perform provisioning.

### 10.6 Programming the Three Flash Objects in Order

Run each command separately and continue only after it succeeds. Addresses are eight-digit hexadecimal values without `0x`.

```powershell
python "$toolDir\secure_device_setup.py" writeflash `
  --port $COM --addr 60000000 -i $loaderParam
```

```powershell
python "$toolDir\secure_device_setup.py" writeflash `
  --port $COM --addr 60000050 -i $ssbl
```

```powershell
python "$toolDir\secure_device_setup.py" writeflash `
  --port $COM --addr 60100050 -i $appPackage
```

Program only the complete signed App package, not a standalone RZSM manifest/body. Do not write the App parameter separately to Flash. Do not write the SSBL at `60000000`. Successful Flash programming is only the first gate; it does not prove successful BootROM/SSBL verification.

### 10.7 Normal Boot and UART Acceptance

Switch the board back to normal xSPI Startup Mode. Start serial log capture. The Loader UART uses 115200 baud; use the corresponding debug UART with 8N1 and no flow control, as configured in the project. Power-cycle the board. Do not substitute an IAR download followed by reset for a real Flash boot.

Other log messages may appear between these markers, but the following key markers must appear in order:

```text
[SSBL] secure package verify start
[SSBL][VERIFY] R_RSIP_SB_ManifestVerify fsp error=0x00000000 (success)
[SSBL][VERIFY][OK] result=0 ...
[SSBL][RZSM] ... magic=0x4d535a52 ... segments=7
[SSBL][RZSM][SEG6] id=6 flags=0x0000000b ... file=0x0003ef5c cipher=0x0003ef60 ... key=1
[SSBL] secure package deploy start
[SSBL][DEPLOY][OK] result=0 ...
[SSBL] jump app=0x0011a000
```

Letter case and numeric leading zeros depend on the actual printf output. There must be no `[FAIL]`, `RSIP CLOSE FAIL`, or indication that deployment is disabled.

The current `jump app` log appears before UART closure, RSIP closure, and the `app_prg()` call, so it proves only that the Loader is preparing to jump. Also observe the App's normal startup logs/application operation, or set breakpoints at App `system_init` and `main` to confirm that execution actually reaches them.

### 10.8 Accepting the Decrypted SYSTEM_DATA Runtime Contents

DEPLOY OK alone does not prove that OTP ckey0 matches the host key. Check plaintext bytes. Two boots are recommended: capture complete UART/application startup on the first, and inspect memory before App startup with the debugger on the second.

1. In IAR, use an existing suitable attach/debug session and load only the current Loader `.out` symbols. Do not download the program, execute a Flash loader, or let the debugger initialize RAM or overwrite the App. If the current debug configuration downloads or initializes memory, stop and confirm the configuration separately; do not directly use Download and Debug.
2. Set a hardware breakpoint immediately before `app_prg();` in `hal_entry.c`. Reset, run, and halt there. Confirm successful deployment and no RSIP close failure, while the App has not yet started modifying SYSTEM_DATA. Use a hardware breakpoint so that a software breakpoint does not modify deployed code.
3. At this point, the Loader has performed cache clean/invalidate and DSB/ISB. In the Memory window, inspect SYSTEM_DATA `[0x64000000,0x6403EF5C)`. Use the actual address and length from the generated summary.
4. Save this memory range as a plain binary of 257884 bytes named `tools\system_data_runtime.bin`. Do not include address headers, ASCII text, or the final four bytes of CBC padding. If the optimized breakpoint location is unreliable, halt at an equivalent instruction address after deployment and cache maintenance.
5. Run the following complete-segment byte comparison. It extracts the reference plaintext from id=6 in the current RZAP automatically and does not depend on a hard-coded original Flash offset:

```powershell
python -c "from pathlib import Path; import struct,hashlib; t=Path(r'$tools'); a=(t/'RZN2L_bsp_xspi0bootx1_app.bin').read_bytes(); h=struct.unpack_from('<16I',(t/'rzsm_v2_body.bin').read_bytes()); r=h[12]; assert struct.unpack_from('<I',a,r)[0]==0x50415A52; src,dst,size,flags=struct.unpack_from('<4I',a,r+16+6*16); off=src-h[11]; assert flags&1 and size>0 and 0<=off and off+size<=len(a); expected=a[off:off+size]; actual=(t/'system_data_runtime.bin').read_bytes(); assert len(actual)==size, 'runtime dump length mismatch'; assert actual==expected, 'SYSTEM_DATA plaintext mismatch'; print('SYSTEM_DATA runtime bytes PASS',size,'SHA256',hashlib.sha256(actual).hexdigest())"
```

The SYSTEM_DATA end address is also the SYSTEM_PRG start address, `0x6403EF5C`. Comparing the first 16 bytes of SYSTEM_PRG against the corresponding original App segment provides an additional check that tail padding has not overwritten the adjacent segment.

The SYSTEM_DATA reference values for the original App in this record are shown below. Recalculate them after rebuilding the App; do not reuse these values:

```text
SHA-256: e408275d85f4636820b90b9acfe75bf42a9bd9734c206389fcae7bafc5c249cf
0x64000000 first16: 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0x6403EF4C last16:  44 00 65 00 00 00 00 00 ff ff ff ff 00 00 00 00
0x6403EF5C SYSTEM_PRG first16: 00 20 01 49 08 60 70 47 ac e8 03 64 38 b5 04 00
```

```powershell
python -c "from pathlib import Path; import struct; t=Path(r'$tools'); a=(t/'RZN2L_bsp_xspi0bootx1_app.bin').read_bytes(); h=struct.unpack_from('<16I',(t/'rzsm_v2_body.bin').read_bytes()); src,dst,size,flags=struct.unpack_from('<4I',a,h[12]+16+5*16); print('SYSTEM_PRG start',hex(dst),'expected first16',a[src-h[11]:src-h[11]+16].hex(' '))"
```

Resume execution and confirm that the App actually reaches `system_init`/`main` and operates normally. Checking only a few bytes at the start and end is sampling, not a substitute for a complete-segment comparison. If secure debug attachment is disabled, stop using this method and obtain separate approval to add runtime hash-check logging. Do not claim that runtime contents are correct without verification.

### 10.9 Final Pass Criteria and Failure Diagnosis

All of the following must pass: BootROM boots this SSBL from Flash; outer-signature verification succeeds; all seven RZSM segments deploy successfully; RSIP closes successfully and execution actually enters the App; the complete SYSTEM_DATA runtime memory matches the original App; and the App operates normally. Record the UART logs and memory comparison results.

| Failure Point | Next Step |
| --- | --- |
| No SSBL UART output | Check normal boot mode, the three programming addresses, serial connection, BootROM authentication, and the SSBL linked entry point. Do not rewrite OTP. |
| VERIFY FAIL | Check the complete package, Root key/OTP, Flash contents, and error code. |
| Logs still show RZAP | An old Scheme B Loader is in use. Sign and program the actual Scheme A Debug build. |
| SEG6 CRYPTO/DEPLOY FAIL | Check the CIP error code, RSIP configuration, address alignment, HRAM initialization, and ckey0 availability. |
| DEPLOY OK but plaintext differs | First check OTP ckey0 against the host key, CIP IV byte order, and the chained IV for the tail block. Stop jump testing. |
| Execution hangs after the jump log | The log does not prove actual App entry. Check RSIP close, entry-point breakpoints, App startup, and deployed data. |

Signing and offline checks in this section can be performed on the host. Without board-side UART/memory evidence, do not report generated instructions as successful board validation.

## 11. All Active Segment Encryption Change Record (2026-10-12)

The author confirmed successful SYSTEM_DATA-only encrypted deployment and, on 2026-10-12, also confirmed successful board validation of encryption for all seven active segments. The App operates normally on the RZN2L RSK socket board. The board-level conclusion is based on the author's feedback.

### 11.1 Source Changes and Compatibility

- `make_secure_app_package.py` adds `--encrypt-all`, selecting every enabled, nonzero-length RZAP segment, including ID 0. It is mutually exclusive with `--encrypt-segment`; both require `--aes-key`. Inspection mode and `overall-app` mode reject encryption options.
- Existing `--encrypt-segment SYSTEM_DATA`, `SYSTEM_PRG`, and unencrypted commands remain valid. Single-segment selection now supports all known, enabled, nonempty segments for individual diagnosis. The generation function's new `encrypt_all=False` parameter is appended at the end, preserving existing positional calls.
- Each segment is independently zero-padded to a multiple of 16 bytes, uses the existing ckey0, and receives a nonzero, unique random IV. Structural checks add four-byte encrypted-destination alignment, known-ID validation, and duplicate-IV rejection. Full offline validation compares each segment's decrypted plaintext, SHA-256, and zero padding.
- In `secure_app_deploy.c`, `encrypted_segment_destination_allowed()` now reuses `segment_destination_allowed()`. All known segment types are supported, while the per-ID destination whitelist, range/overlap/alignment checks, CBC metadata checks, and IV checks remain in place. The tail-block algorithm still writes back only valid plaintext bytes.
- The RZSM v2 header/entry ABI, Flash addresses, App entry point, and original App memory layout are unchanged. No FSP/RSIP driver, IAR project, or linker configuration changes were made for this implementation. Inspection of `rsip_validate_addr()` confirmed that it checks alignment only and does not reject the VECTOR destination at address zero. The author confirmed successful board operation of the seven-segment scheme, including VECTOR/ATCM, but no independent byte-for-byte ATCM readback was collected.
- The outer image remains a `cert-img` signature over the RZSM body without whole-body encryption; do not add an outer App `--enckey`. The host AES key is not included in the package. NONCACHE/SHARED_NONCACHE_BUFFER currently have no active payloads; if enabled and nonempty in future, they will also be selected by `--encrypt-all`.

### 11.2 Separate Artifacts and Generation Commands

Keep the verified single-segment artifacts from Section 10. The all-encrypted artifacts below use separate names and must not be deployed with an old Loader that allows encryption only for SYSTEM_PRG/SYSTEM_DATA.

```powershell
$tools = Join-Path $PWD 'tools'
$python = 'C:\Program Files\Python312\python.exe'
$toolDir = 'C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl'
$body = Join-Path $tools 'rzsm_all_v2_body.bin'
$param = Join-Path $tools 'pn20_rzsm_all_app_param.bin'
$signed = Join-Path $tools 'pn20_secure_rzsm_all_app_package.bin'

& $python "$tools\make_secure_app_package.py" --scheme rzsm `
  --app-bin "$tools\RZN2L_bsp_xspi0bootx1_app.bin" `
  --manifest-out "$tools\rzsm_all_v2_manifest.bin" --body-out $body `
  --encrypt-all --aes-key "$tools\secureboot_common_key.bin" `
  --summary-out "$tools\rzsm_all_v2_summary.json"
if ($LASTEXITCODE -ne 0) { throw 'RZSM generation failed' }

& $python "$toolDir\parameter_generator.py" userapp `
  --src_addr 60100050 --app_start_addr 60100250 --secureboot -i $body -o $param
if ($LASTEXITCODE -ne 0) { throw 'App parameter generation failed' }

& $python "$toolDir\secureboot_utility.py" image -i $body -o $signed `
  --param $param --rkey "$toolDir\rootkey-pair.pem" --ikey "$toolDir\loaderkey-pair.pem" `
  --sign_target cert-img --ver 0
if ($LASTEXITCODE -ne 0) { throw 'App signing failed' }
```

The current Debug App's seven segments are LDR_PRG, LDR_DATA, VECTOR, USER_PRG, USER_DATA, SYSTEM_PRG, and SYSTEM_DATA. Segment IDs/package order are not sorted by RAM address. SYSTEM_DATA remains below SYSTEM_PRG in RAM. Use the new summary for actual lengths and offsets.

The generated body is 1,569,424 bytes, and the complete signed package is 1,569,936 bytes. The entry point remains `0x0011A000`. The complete package SHA-256 is `3b55f47ce83d1a66b375bf046b471e640dc1d41b6dfdf2c3ae9050520bb4bc6a`. Regeneration changes the hash because of random IVs/signatures.

All seven actual App segments passed decrypted-plaintext, SHA-256, zero-padding, destination-range/non-overlap, and chained tail-block IV software checks. The two Root/Image ECDSA signatures, image key binding, App parameter hash, and outer unencrypted-image flags/addresses/lengths also passed. Inspection results are recorded in `rzsm_all_v2_inspect_summary.json` and `pn20_rzsm_all_signed_summary.json`. The signing public keys match the existing SSBL, but that does not mean the old SSBL binary supports all-segment deployment.

During the first stage, before/after generation hashes were compared for the original single-segment manifest/body/parameter/package and the original Loader padded binary/parameter/secure SSBL. All seven original artifacts were unchanged. At that point, the new Loader had not yet been built or re-signed. A separate SSBL was subsequently generated from the latest Debug binary; see Section 11.4.

### 11.3 Validation Scope and Board Follow-Up Steps

`test_make_secure_app_package.py` was added using a synthetic RZAP and a nonproduction test key. Six tests cover existing single-segment/unencrypted compatibility, selection of all active segments, exclusion of disabled/zero-length segments, IV retries and duplicate rejection, rejection of missing keys/mutually exclusive options/incompatible modes, and CBC tail-block software simulation. Run:

```powershell
& 'C:\Program Files\Python312\python.exe' -m unittest discover -s tools -p test_make_secure_app_package.py -v
```

Host tests do not execute real RSIP operations. The current VS Code environment lacks C standard-header configuration, and no host C compiler is available; the assistant does not run IAR builds. The latest Debug Loader artifact was used for packaging in Section 11.4. The author has confirmed successful hardware decryption and board operation of the all-segment scheme. The following steps remain as a reproduction and additional RAM-readback procedure.

1. The author manually rebuilds the modified Loader in IAR. The unchanged App does not need a rebuild merely because the packaging option changes.
2. Use the new Loader raw binary to repeat padding, Loader parameter generation, signing, and SSBL encryption as described in Section 10. Do not re-sign an old Debug binary and treat it as the new Loader.
3. After offline signature and key-identity checks, update the Loader parameter at `0x60000000`, new SSBL at `0x60000050`, and complete all-encrypted App package at `0x60100050`. Do not write the raw App or only the body, and do not modify OTP.
4. Confirm outer verification, seven-segment decryption/deployment, RSIP close, actual App entry, and application operation. Before App startup changes the data, compare each segment's complete RAM contents against the corresponding original App plaintext. Pay particular attention to VECTOR/ATCM, LDR/BTCM, USER/System RAM, and HRAM, and verify that CBC padding does not overwrite adjacent segments.

The assistant only modified source/documentation and performed offline packaging/tests. It did not automatically run IAR, write Flash, or perform OTP provisioning. The author independently completed board validation and confirmed the all-segment scheme works.

### 11.4 Encrypted SSBL Generation Record for the New Loader (2026-10-12)

The input is `RZN2L_bsp_xspi0bootx1_loader/Debug/Exe/RZN2L_bsp_xspi0bootx1_loader.bin`, modified at 2026-10-12 15:32:10, later than `secure_app_deploy.c` at 15:22:04. The raw file is 51,916 bytes with SHA-256 `1f8611d455a3ca46b6870db8d052cb06e28250dffea96c3a3ba9e20cabd92ca8`, and differs from the old SSBL input.

Following Section 10.3's 512-byte/`0xFF` padding rule, a separate padded binary was generated: 52,224 bytes with 308 bytes of tail padding. The old single-segment SSBL files were not overwritten.

| File (under tools) | Length (bytes) | Purpose/Flash Address |
| --- | ---: | --- |
| pn20_scheme_a_all_ssbl_padded.bin | 52,224 | Intermediate packaging input; not programmed separately |
| pn20_scheme_a_all_ssbl_parameter_xspi0.bin | 76 | Loader parameter, `0x60000000` |
| pn20_scheme_a_all_secure_ssbl_xspi0.bin | 54,272 | New encrypted SSBL, `0x60000050` |
| pn20_secure_rzsm_all_app_package.bin | 1,569,936 | Complete all-encrypted App package from Section 11.2, `0x60100050` |

Run the following from the workspace root. First repeat padding as in Section 10.3, using this section's separate `$padded` path, then generate the parameter and encrypted SSBL:

```powershell
$workspace = $PWD.Path
$tools = Join-Path $workspace 'tools'
$python = 'C:\Program Files\Python312\python.exe'
$toolDir = 'C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl'
$raw = Join-Path $workspace 'RZN2L_bsp_xspi0bootx1_loader\Debug\Exe\RZN2L_bsp_xspi0bootx1_loader.bin'
$padded = Join-Path $tools 'pn20_scheme_a_all_ssbl_padded.bin'
$loaderParam = Join-Path $tools 'pn20_scheme_a_all_ssbl_parameter_xspi0.bin'
$ssbl = Join-Path $tools 'pn20_scheme_a_all_secure_ssbl_xspi0.bin'
$aesKey = Join-Path $tools 'secureboot_common_key.bin'

& $python "$toolDir\parameter_generator.py" loader --mpu rzn2l --target_cpu cr52 `
  --src_addr 60000050 --dest_addr 00102000 --mode xspi0 --secureboot `
  -i $padded -o $loaderParam
if ($LASTEXITCODE -ne 0) { throw 'Loader parameter generation failed' }

& $python "$toolDir\secureboot_utility.py" image -i $padded -o $ssbl `
  --param $loaderParam --rkey "$toolDir\rootkey-pair.pem" --ikey "$toolDir\loaderkey-pair.pem" `
  --enckey $aesKey --keyselect ckey0 --sign_target cert-img --ver 0
if ($LASTEXITCODE -ne 0) { throw 'Encrypted SSBL generation failed' }
```

`cert-img`, encryption enabled, and ckey0 were confirmed. The two Root/Image ECDSA signatures, ciphertext signature, image key binding, and Loader parameter hash all passed. Full AES-CBC decryption is byte-for-byte identical to the new raw Loader plus all `0xFF` padding. Root/Image public keys and `.pubkey` match the all-encrypted App in Section 11.2. The complete SSBL SHA-256 is `906b7ec7c55b69e96457a4caa854ab1e585ecdb47f6146e6e09eaf9fc06e5667`.

The SSBL Code Certificate's `write_addr` and `dest_addr` are both the RAM program address `0x00102800`, not Flash addresses. The Loader parameter contains the Flash source `0x60000050`, SSBL length `0xD400` including both certificates, and RAM load base `0x00102000`. The certificate's `img_len=0xCC00` corresponds to 52,224 bytes of ciphertext. Do not apply the App Code Certificate's address expectations to the SSBL.

The official tools output `pn20_scheme_a_all_secure_ssbl_xspi0.log` and its matching `.pubkey` in the workspace root. Before/after generation hashes of the three old Loader artifacts, original single-segment App package, and existing all-encrypted App package were unchanged. For programming, use the new Loader parameter, new SSBL, and all-encrypted App package listed above; do not mix them with the old single-segment SSBL. The AI assistant performed no Flash/OTP operations; the author confirmed successful all-segment board validation.
