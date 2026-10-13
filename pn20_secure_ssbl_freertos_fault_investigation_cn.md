# PN2.0 Secure SSBL FreeRTOS Task-Creation Fault Investigation Report

## 1. Purpose

This report summarizes the current fault, completed verification work, and items requiring product-line support for the RZ/N2L PN2.0 `Secure SSBL -> App` boot flow.

The objective is to identify the root cause of the following behavior: Secure SSBL completes Secure App verification, copying, and handoff; the App reaches `main()` and starts FreeRTOS static task creation, but subsequently enters an exception path / `Default_Handler`.
Before moving forward, please read through the following Markdown file first, as it contains important information and prerequisites.
1. C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\README_loader_app_split_en.md

2. C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\pn20_rzn2l_secure_ssbl_fwupdate_proposal_en.md

3. C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\pn20_rzn2l_secure_app_scheme_comparison_en.md


## 2. Current Software and Boot Architecture

| Item | Current implementation |
| --- | --- |
| SoC | Renesas RZ/N2L, Cortex-R52 |
| Toolchain | IAR EWARM 9.70.2 |
| Loader project | `RZN2L_bsp_xspi0bootx1_loader` |
| App project | `RZN2L_bsp_xspi0bootx1_app` |
| Secure App scheme | Scheme B: signed plaintext overall App package |
| Secure App package Flash base | `0x60100050` |
| Key + Code Certificate total size | `0x200` bytes |
| App package body / App link base | `0x60100250` |
| App manifest | Legacy RZAP manifest at `0x60100250` |
| App entry point | App `system_init`, currently `0x0011A000` |

The expected boot flow is:

```text
RZ/N2L BootROM
  -> signed plaintext Secure SSBL
  -> Secure SSBL initializes xSPI/SDRAM/RSIP
  -> Secure SSBL verifies signed plaintext App package
  -> Secure SSBL reads RZAP manifest and copies App sections
  -> Secure SSBL jumps to App system_init
  -> App SystemInit / BSP / GIC initialization
  -> App main()
  -> FreeRTOS static task creation and scheduler
```


## 3. Problem Statement

### 3.1 Observed behavior

1. BootROM can start the signed Secure SSBL.
2. Secure SSBL can complete RSIP package verification successfully.
3. Secure SSBL reads a valid RZAP manifest and copies App sections to the expected destinations.
4. App reaches `system_init`, `SystemInit`, and `main()`.
5. App reaches the FreeRTOS static task creation path.
6. During or after this path, execution stops in an exception path / `Default_Handler` instead of continuing to normal application operation.

The issue is therefore no longer an early BootROM, SSBL authentication, App package layout, or initial App entry-point failure.

### 3.2 Trigger area

The issue is observed around FreeRTOS initialization and static task creation. The task stack fill pattern `0xA5A5A5A5` observed in the debugger is expected FreeRTOS behavior and is not, by itself, evidence of a corrupted stack pointer.

The affected static-task runtime data observed during investigation includes:

| Item | Address / value |
| --- | --- |
| Static TCB | `0x7418086C` |
| Static stack buffer | `0x741854E0` |
| Stack depth | `0x500` bytes |
| Calculated stack top | `0x741859D8` |
| Initial fill | `0xA5A5A5A5` |

## 4. Flash Image and Package Integrity Verification

### 4.1 Key conclusion

The Secure and non-secure Flash images contain **identical App body bytes**.

Compared files:

| Image | Location |
| --- | --- |
| Secure image | `RZN2L_bsp_xspi0bootx1_loader/memory_secure.hex` |
| Non-secure image | `RZN2L_bsp_xspi0bootx1_loader/memory_nosecure.hex` |

The App body starts after the certificate area at `0x60100250`. The complete App body in `memory_secure.hex` matches the corresponding App body in `memory_nosecure.hex`.

This eliminates the following as the primary cause of the FreeRTOS fault:

- Secure signature/certificate bytes modifying the App body.
- Secure package generation modifying plaintext App code or initialized data.
- IAR/i-jet download changing active App code or initialized data in Flash.


### 4.2 Additional byte-level verification already completed

| Check | Result |
| --- | --- |
| Raw App bin size | `0x17F1F4` |
| Signed package size | `0x17F400` |
| Package body size | `0x17F200` |
| Raw App equals package body prefix | Pass |
| Package body tail | 12 bytes of `0x00` padding |
| `memory.hex` equals signed package image | Pass |
| Flash programmed by i-jet versus raw App body | Active code/initialized-data bytes match; only unallocated binary holes differ (`0x00` bin fill versus erased Flash `0xFF`) |



## 5. App Manifest and Deployment Verification

The current RZAP manifest and secure package body use the same link base:

```text
APP_MANIFEST_ADDR              = 0x60100250
SECURE_APP_PACKAGE_BODY_ADDR   = 0x60100250
```

The copied App sections are consistent with the App manifest:

| Segment | Flash source | Runtime destination | Size |
| --- | ---: | ---: | ---: |
| LDR_PRG | `0x60100350` | `0x0011A000` | `0x1CE8` |
| LDR_DATA | `0x6027EE00` | `0x00118000` | `0x5AC` |
| VECTOR | `0x6027F400` | `0x00000000` | `0x44` |
| USER_PRG | `0x60102038` | `0x10000100` | `0xF71C0` |
| USER_DATA | `0x60273968` | `0x100F72C0` | build-dependent |
| SYSTEM_PRG | `0x60238154` | `0x7403EF5C` | `0x3B814` |
| SYSTEM_DATA | `0x601F91F8` | `0x74000000` | `0x3EF5C` |

The SSBL copies using `bsp_copy_multibyte()`. The function copies whole words and remaining bytes; all active current segment sizes are word aligned. No copy-unit defect has been identified.

The App LDR code is intentionally relocated away from the Loader BTCM execution area:

```text
Secure SSBL code start: approximately 0x00102800
App LDR_DATA start:      0x00118000
App LDR_PRG entry:       0x0011A000
```

This avoids the previously identified failure where an App LDR section near the original `0x00102000` area could overwrite a Loader that was still executing in BTCM.

## 6. Runtime Handoff Differences Still Relevant

Although the App body and copy manifest are now aligned, secure and non-secure startup are not identical at the CPU/peripheral-context level.

| Area | Secure SSBL path | Non-secure Loader path | Assessment |
| --- | --- | --- | --- |
| RSIP | Opens RSIP, verifies package, closes RSIP before jump | Not used | Must confirm no residual system/interrupt state affects App |
| Cache/MPU before jump | Clean/invalidate; debug build also disables D-cache, I-cache and memory protection | Clean/invalidate | App later reconfigures MPU/cache, but early handoff still requires validation |
| Global interrupts before jump | Secure path executes `cpsid i` | Non-secure path does not explicitly do so | App `bsp_irq_cfg()` subsequently enables IRQ/FIQ/SError; likely not persistent, but should be captured |
| Vector base | App vector is copied to ATCM `0x00000000` | Same intended destination |  |
| SDRAM/xSPI setup | Performed in SSBL before App handoff | Performed by Loader path | Verify runtime registers and memory contents are identical at the handoff point |



## 7. Debug Actions Already Attempted

| Action | Result |
| --- | --- |
| Moved the full App link image to the certificate-adjusted body base `0x60100250` | Resolved the earlier `-0x200` source/link mismatch and enabled App startup |
| Corrected SSBL App manifest reference to `0x60100250` | SSBL reads the signed App body manifest correctly |
| Verified raw App/package/Flash body integrity byte by byte | App body is preserved; Flash content is not the current root cause |
| Verified `bsp_copy_multibyte()` implementation and active sizes | No unit/rounding defect found |
| Moved App LDR execution region away from Loader BTCM | Prevents Loader self-overwrite while copying App LDR sections |
| Disabled D-cache, I-cache, and memory protection before secure handoff | FreeRTOS fault remains; App later reinitializes cache/MPU |
| Disabled interrupts before secure jump | App BSP later executes `cpsie i`, `cpsie f`, and `cpsie a`; fault remains |
| Compared secure/non-secure Flash App bodies | Bodies are identical |
| Compared two App raw bins from copied projects | Difference was traced to embedded compiler source-path strings in RTOS `.rodata`, not signing/programming corruption |
| Stepped into App startup | App reaches `main()` and begins static FreeRTOS task creation |

## 8. Required Debug Captures for Joint Investigation

Please collect the following captures for both the secure SSBL path and the known-good non-secure path, at the same checkpoints whenever possible.

### 8.1 Checkpoint A: immediately before `app_prg()`

Capture:

- PC, SP, LR, CPSR.
- `VBAR`.
- `SCTLR`, MPU enable state, I-cache and D-cache bits.
- GIC distributor/CPU interface enable state and pending interrupts.
- Contents/CRC of all copied App runtime sections.

### 8.2 Checkpoint B: App `system_init` / `SystemInit`

Capture:

- PC and current CPU mode.
- `VBAR`; expected App vector base is `0x00000000`.
- ATCM contents at `0x00000000..0x00000043`; expected to contain App vector entries.
- Stack pointers for SVC, IRQ, Abort, Undefined, System, and FIQ modes after `stack_init()`.

### 8.3 Checkpoint C: before and after first static task creation

Capture:

- Calling function and source line.
- TCB address, task stack start/end, stack fill pattern.
- CPSR and SP before/after FreeRTOS API call.
- FreeRTOS scheduler state and current task pointer.
- Any SVC/IRQ/FIQ/Abort entry.
![alt text](image.png)
### 8.4 Checkpoint D: exception stop

Capture at the first exception, before continuing:

- PC, LR, SP, CPSR and active CPU mode.
- `VBAR`.
- `SPSR_abt`, `SPSR_und`, and `SPSR_irq` as applicable.
- Data-abort status/address: `DFSR`, `DFAR`.
- Prefetch-abort status/address: `IFSR`, `IFAR`.
- The vector entry and handler target used for the exception.
- Call stack/disassembly around the instruction that raised the exception.

### 8.5 Screenshot placeholders

```text

first static task creation, TCB/stack variables, PC and call stack.
```
![alt text](image-2.png)

```text
first exception stop with PC, CPSR, DFSR/DFAR and call stack.
```
![alt text](image-1.png)
![alt text](image-3.png)

## 9. Questions for Product Line / FSP / IAR Support

1. In the RZ/N2L Loader + App split architecture, should the App always explicitly program `VBAR` after the Loader has copied `VECTOR_WBLOCK` to ATCM?
2. Is it supported for `APP_STANDALONE_DEBUG = 0` to skip the `VBAR = __Vectors` operation when handoff is performed by a custom Secure SSBL rather than the original monolithic startup path?
3. What CPU state must a custom SSBL restore or preserve before jumping to an App `system_init` entry on Cortex-R52?
4. Are there required RSIP close/deinitialization, GIC, cache, MPU, or TCM barriers beyond `DSB`/`ISB` before handing off to a FreeRTOS App?
5. Does the IAR Cortex-R52/FreeRTOS port require a specific exception mode, VBAR state, stack setup, or debug setting at the initial App entry?
6. Which fault-status registers should be prioritized for a FreeRTOS task-creation failure that subsequently enters `Default_Handler`?

## 10. Current Status and Proposed Next Step

### Confirmed

- Secure SSBL boots and verifies the signed plaintext App package.
- Secure and non-secure Flash images have identical App body bytes.
- Current package/body/link address agreement is correct at `0x60100250`.
- App manifest deployment reaches the App startup path and `main()`.
- The fault occurs later, around FreeRTOS static task creation/scheduler-related execution.


### Recommended immediate action

Run a paired secure/non-secure debug session with breakpoints at:

```text
1. Secure SSBL: immediately before app_prg()
2. App: system_init()
3. App: SystemInit()
4. App: first static task creation API call
5. App: Default_Handler / Abort_Handler / SVC_Handler / IRQ_Handler
```

At each point, record the register and memory fields in Section 9. The first divergence, exception mode, stack pointer, or fault-status register, should identify the next root-cause investigation direction.

## 11. Debug environment setup


shared project content:
![alt text](image-4.png)
## Important:

The project must reside in the Renesas_PROFINET_IRT_DEVKIT_V2.0.0 SDK installation directory. Otherwise, the build may fail due to missing or incorrect relative paths.
To build the Non-Secure project, rename the folder iar_project_offset0x200 to iar_project.
Only one iar_project folder should exist within the SDK installation directory at any time to avoid workspace and build conflicts.

## 11.1 Debug in non_secure RSK Board
![alt text](image-8.png)

Non-Secure Project
The Loader and application code can be downloaded to the external flash via the I-jet debugger in xSPI mode. This configuration has been verified, and the system operates normally in xSPI mode after programming.
Once the application image has been programmed into the external flash, the PN2.0 Secure Secondary Boot Loader (SSBL) can be debugged on a standard RSK board under the following conditions:


* Set the macro SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE to 0.


* Configure SW4 as follows:
        SW4.1 = ON
        SW4.2 = OFF
        SW4.3 = ON
        SW4.4 = OFF

Any boot configuration other than xSPI Startup Mode is acceptable.


The Secure Loader (SSBL) project must be debugged in RAM mode.

![alt text](image-5.png)

## 11.2 Debug at secure RSK Board
![alt text](image-7.png)
For the secure RSK board, I have generated the related SSBL and app bin in the pn20_secure_boot_image folder.
![alt text](image-6.png)
Once the application image has been programmed into the external flash(using the secure_device_setup.py script), the PN2.0 Secure Secondary Boot Loader (SSBL) can be debugged on a secure RSK board under the following conditions:

* Set the macro SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE to 1.

* Configure SW4 as follows:
        SW4.1 = ON
        SW4.2 = OFF
        SW4.3 = ON
        SW4.4 = OFF

 Any boot configuration other than xSPI Startup Mode is acceptable.
* The Secure Loader (SSBL) project must be debugged in RAM mode.

## script usage (use the Windows PowerShell):
PN2.0 SSBL Part: 

* The original pn20_secure_ssbl.bin path and file
PS C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project> $raw = "C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\RZN2L_bsp_xspi0bootx1_loader\Debug\Exe\RZN2L_bsp_xspi0bootx1_loader.bin"

* padding the original ssbl bin file
PS C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup> $padded = "C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\pn20_secure_boot_image\pn20_ssbl_padded.bin"

* Generate the ssbl parameter:
PS C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\pn20_secure_boot_image>
python .\parameter_generator.py loader --mpu rzn2l --target_cpu cr52 --src_addr 60000050 --dest_addr 00102000 --mode xspi0 --secureboot -i ".\pn20_ssbl_padded.bin" -o ".\pn20_ssbl_parameter_xspi0.bin"

* Generate the ssbl secure boot image and root key hash
>> python .\secureboot_utility.py image `
>>   -i ".\pn20_ssbl_padded.bin" `
>>   -o ".\pn20_secure_ssbl_xspi0.bin" `
>>   --param ".\pn20_ssbl_parameter_xspi0.bin" `
>>   --rkey ".\rootkey-pair.pem" `
>>   --ikey ".\loaderkey-pair.pem" `
>>   --sign_target cert-img `
>>   --keyselect ckey0 `
>>   --ver 0

PN2.0 APP part:
* generate the parameters for the app
python .\parameter_generator.py userapp  --src_addr 60100050 --app_start_addr 60100250 -i ".\pn20_secure_ssbl\RZN2L_bsp_xspi0bootx1_app.bin" -o ".\pn20_secure_ssbl\pn20_app_param.bin" --secureboot

* Generate the app secure image and root key hash
python .\secureboot_utility.py image -i .\pn20_secure_ssbl\RZN2L_bsp_xspi0bootx1_app.bin -o pn20_secure_app_package.bin --param .\pn20_secure_ssbl\pn20_app_param.bin --rkey .\pn20_secure_ssbl\rootkey-pair.pem --ikey .\pn20_secure_ssbl\loaderkey-pair.pem --sign_target cert-img --ver 0


compare the two .pubkey files. If the same rootkey-pair.pem and loader key-pair.pem are used during the generation process, the resulting .pubkey files should be exactly identical and is the same with the sec_boot_image_led_xspi0.pubkey in the 6526 example project.

Get-FileHash ".\pn20_secure_ssbl_xspi0.pubkey"
Get-FileHash ".\pn20_secure_app_package.pubkey"

![alt text](image-9.png)


Flash and OTP Programming Sequence
First, switch the board to SCI Boot Mode, launch the RZ/N2L Device Setup Utility, and verify both the communication status and OTP status:

PS C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\pn20_secure_boot_image>
$COM = "COMx"

python .\secure_device_setup.py start --port $COM --boot_mode sci -i .\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec

If the OTP has already been programmed, this step can be skipped.(has been wroten in the "r01an6526ej0310-rzt2-n2-security-secureboot" example)

In this build, both the SSBL and the Application Package are signed only and are not encrypted. Therefore:

- secureboot_common_key.bin is not required.
- keyinfo is not required.
- injectkey is not required.
- No AES common key OTP programming is required.

## 11.3 Print information in the secure project:
As the UART Print function is added in the PN2.0 secure SSBL project, during the debug processing the boot processing could be observed in the serial terminal.
![alt text](image-10.png)