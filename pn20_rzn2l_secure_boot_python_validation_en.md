# PN2.0 RZ/N2L Socket Board Secure Boot Python Validation Procedure

## 1. Purpose and Scope

This document records the verified Python-tool procedure for the PN2.0 `Secure SSBL -> Secure App` flow on an RZ/N2L Socket board. It uses the Renesas R01AN6526 Device Setup and Secure Boot tools to program the currently built Secure SSBL and signed overall-App package into xSPI0 Flash, then verifies Secure Boot through normal xSPI startup.

The current procedure uses Scheme B: the overall App is signed and deployed in plaintext.

```text
BootROM
  -> authenticates and starts the Secure SSBL
  -> Secure SSBL uses RSIP to verify the overall Secure App package
  -> Secure SSBL parses the signed RZAP manifest and deploys the App sections
  -> jumps to App system_init
```

The routine validation procedure in this document is for boards that have already been security-provisioned. OTP programming, `setboot --enable`, and disabling SCI/USB Boot are one-time or irreversible operations and are not routine validation steps.

## 2. Tools, Project, and Prerequisites

| Item | Path or requirement |
| --- | --- |
| Project root | `C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project` |
| Renesas tool directory | `C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl` |
| Device Setup S-record | `..\rzn2l\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec` |
| Connection port | COM port assigned to the RSK USB Serial Port, for example `COM3` |
| Python | Python 3; run `python` in the tool directory |
| Keys | `rootkey-pair.pem` and `loaderkey-pair.pem` must correspond to the Root Public Key provisioned in the board OTP |

Build the current Loader and App projects in IAR before starting. This document does not run IAR builds automatically.

The Secure SSBL project must have the following configuration:

```c
#define SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE (1u)
```

In addition, `hal_entry.c` must actually call `secure_app_verify_package()`. Printing a successful verification message while skipping this function leaves the verified package-body length at zero, and deployment will then fail deterministically.

## 3. Flash Address Layout

The `writeflash --addr` argument is an eight-digit hexadecimal string with no `0x` prefix.

| Flash start address | File | Purpose |
| ---: | --- | --- |
| `60000000` | `pn20_ssbl_parameter_xspi0.bin` | BootROM Loader parameter: xSPI configuration, SSBL source address, length, and RAM destination |
| `60000050` | `pn20_secure_ssbl_xspi0.bin` | Authenticated Secure SSBL image |
| `60100050` | `pn20_secure_app_package.bin` | Key Certificate + Code Certificate + overall App body |

The `src_addr=60000050` contained in the Loader parameter must match the actual Flash address of the Secure SSBL image. Do not program `pn20_secure_ssbl_xspi0.bin` at `60000000`; doing so overwrites the parameter read by BootROM.

For `--dest_addr 00102000`, the Secure SSBL image begins with a `0x800`-byte Loader Certificate. The actual Loader program entry is `0x00102800`.

## 4. Generate the Secure SSBL

Open PowerShell, change to the tool directory, and define the current build input files:

```powershell
Set-Location "C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl"

$raw = "C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\RZN2L_bsp_xspi0bootx1_loader\Debug\Exe\RZN2L_bsp_xspi0bootx1_loader.bin"
$padded = ".\pn20_ssbl_padded.bin"
```

### 4.1 Pad to the Current Loader Size

`parameter_generator.py loader` requires a Loader input length that is a multiple of `0x200`. Round up the actual size of the current `$raw` build and fill the remainder with `0xFF`; do not use a fixed size from a historical image.

The following is compatible with Windows PowerShell 5.1 and does not depend on `[System.Array]::Fill()`:

```powershell
$rawBytes = [System.IO.File]::ReadAllBytes($raw)
$unitSize = 0x200
$paddedSize = [Math]::Ceiling($rawBytes.Length / [double]$unitSize) * $unitSize

$paddedBytes = New-Object byte[] $paddedSize
0..($paddedBytes.Length - 1) | ForEach-Object {
    $paddedBytes[$_] = 0xFF
}

[Array]::Copy($rawBytes, $paddedBytes, $rawBytes.Length)
[System.IO.File]::WriteAllBytes($padded, $paddedBytes)

"Raw size    : 0x{0:X}" -f $rawBytes.Length
"Padded size : 0x{0:X}" -f $paddedBytes.Length
```

### 4.2 Generate the Loader Parameter and Secure SSBL Image

```powershell
python .\parameter_generator.py loader `
  --mpu rzn2l `
  --target_cpu cr52 `
  --src_addr 60000050 `
  --dest_addr 00102000 `
  --mode xspi0 `
  --secureboot `
  -i $padded `
  -o ".\pn20_ssbl_parameter_xspi0.bin"

python .\secureboot_utility.py image `
  -i $padded `
  -o ".\pn20_secure_ssbl_xspi0.bin" `
  --param ".\pn20_ssbl_parameter_xspi0.bin" `
  --rkey ".\rootkey-pair.pem" `
  --ikey ".\loaderkey-pair.pem" `
  --sign_target cert-img `
  --keyselect ckey0 `
  --ver 0
```

Confirm that at least the following outputs exist:

```powershell
Get-Item .\pn20_ssbl_parameter_xspi0.bin, .\pn20_secure_ssbl_xspi0.bin
Get-FileHash .\pn20_secure_ssbl_xspi0.pubkey
```

## 5. Generate the Signed Overall Secure App Package

The commands below generate the signed Scheme B overall-App package from the current App raw binary. `--app_start_addr 60100250` is the App body address after the certificates; `60100050` is the Flash start address of the complete package.

```powershell
$appRaw = "C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\RZN2L_bsp_xspi0bootx1_app\Debug_EK52_App1_STANDARD\Exe\RZN2L_bsp_xspi0bootx1_app.bin"

python .\parameter_generator.py userapp `
  --src_addr 60100050 `
  --app_start_addr 60100250 `
  --secureboot `
  -i $appRaw `
  -o ".\pn20_app_param.bin"

python .\secureboot_utility.py image `
  -i $appRaw `
  -o ".\pn20_secure_app_package.bin" `
  --param ".\pn20_app_param.bin" `
  --rkey ".\rootkey-pair.pem" `
  --ikey ".\loaderkey-pair.pem" `
  --sign_target cert-img `
  --ver 0
```

For the current Scheme B flow, `pn20_app_param.bin` is used to generate the Code Certificate. The object programmed into Flash is the complete `pn20_secure_app_package.bin`; the App parameter is not programmed separately.

Confirm that the Loader and App package were generated with the same Root Key:

```powershell
Get-FileHash .\pn20_secure_ssbl_xspi0.pubkey, .\pn20_secure_app_package.pubkey
Get-Item .\pn20_secure_app_package.bin
```

The SHA-256 hashes of the two `.pubkey` files must match. If they do not, stop before programming and check the `rootkey-pair.pem` used in both generation commands.

## 6. Download and Start the Device Setup Program

1. Close Tera Term, IAR serial windows, and other tools that may occupy the COM port.
2. Set the Socket board to SCI Boot Mode.
3. Reset or power-cycle the board so BootROM waits for the SCI download.
4. Run the following commands in the same PowerShell session:

```powershell
$COM = "COM3" # Replace with the RSK USB Serial Port shown in Device Manager.
$setupSrec = "..\rzn2l\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec"

python .\secure_device_setup.py start `
  --port $COM `
  --boot_mode sci `
  -i $setupSrec
```

Program Flash only after `start` succeeds. `Failed to send the program file.` means that the BootROM transfer phase failed; neither Flash nor OTP has been written. Recheck SCI Boot Mode, COM-port ownership, port number, and reset timing.

## 7. Program the Secure SSBL and Secure App

After the Device Setup Program starts successfully, run these three commands in the same terminal session:

```powershell
python .\secure_device_setup.py writeflash `
  --port $COM `
  --addr 60000000 `
  -i ".\pn20_ssbl_parameter_xspi0.bin"

python .\secure_device_setup.py writeflash `
  --port $COM `
  --addr 60000050 `
  -i ".\pn20_secure_ssbl_xspi0.bin"

python .\secure_device_setup.py writeflash `
  --port $COM `
  --addr 60100050 `
  -i ".\pn20_secure_app_package.bin"
```

When all three commands complete, exit the Device Setup Program, switch the board to normal xSPI Startup Mode, then power-cycle or reset the board.

## 8. Validation Criteria and Fault Location

Observe the startup log through UART. A successful validation must show all of the following:

1. BootROM authenticates and transfers control to the Secure SSBL.
2. Secure SSBL reports successful RSIP package verification.
3. Secure SSBL parses the RZAP manifest and deploys all App sections successfully.
4. Secure SSBL jumps to App `system_init`, and the App reaches `main()` and normal application tasks.

| Symptom | Priority checks |
| --- | --- |
| `start` transfer failure | SCI Boot Mode, COM-port ownership, port number, reset timing, and `SCI_qspi.out.srec` path |
| BootROM does not start SSBL | Confirm the Loader parameter was programmed at `60000000`, its `src_addr` is `60000050`, and the SSBL image was not incorrectly programmed at `60000000` |
| SSBL verification failure | Check whether the OTP Root Public Key matches `.pubkey`, whether the package is complete, and whether both artifacts use the same Root Key |
| SSBL deployment failure | Confirm `SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE` is `1u`, `secure_app_verify_package()` is called, and the App package is programmed at `60100050` |
| HyperRAM initialization hangs | Preserve the verified ordering: call `hram_init()` before `bsp_qspi_quad_enable()`; QSPI Flash and HyperRAM share the XSPI0 controller |

## 9. First-Time Provisioning Boundary

Only new boards that have not yet been configured for Secure Boot require OTP Root Key programming and secure boot enablement. Before proceeding, verify that `.pubkey` matches the Root Public Key Hash planned for OTP. This configuration cannot be reverted through normal routine testing.

Do not repeat the following operations during routine validation:

```text
setboot --enable
Program the OTP Root Public Key Hash
setsciboot --disable
setusbboot --disable
```

In particular, do not disable SCI Boot as part of startup debugging. Keeping SCI Boot enabled allows the Device Setup Program to be downloaded again and the Flash contents to be updated later.

## 10. References

- Renesas `r01an6526ej0310-rzt2-n2-security-secureboot.pdf`, section 2.2.7, "Program to Flash".
- [pn20_secure_ssbl_freertos_fault_investigation.md](pn20_secure_ssbl_freertos_fault_investigation.md)
- [README_loader_app_split_en.md](README_loader_app_split_en.md)
- [pn20_rzn2l_secure_app_scheme_comparison_en.md](pn20_rzn2l_secure_app_scheme_comparison_en.md)
