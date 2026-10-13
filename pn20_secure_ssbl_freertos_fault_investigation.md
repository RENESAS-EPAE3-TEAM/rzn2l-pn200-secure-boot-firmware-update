# PN2.0 Secure SSBL FreeRTOS Task-Creation Fault Investigation Report

## 1. Purpose

This report summarizes the current fault, completed verification work, and items requiring product-line support for the RZ/N2L PN2.0 `Secure SSBL -> App` boot flow.

The objective is to identify the root cause of the following behavior: Secure SSBL completes Secure App verification, copying, and handoff; the App reaches `main()` and starts FreeRTOS static task creation, but subsequently enters an exception path / `Default_Handler` and cannot run reliably.

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
- A remaining `0x200` certificate-offset error in the current App body placement.

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

### 4.3 Evidence screenshot placeholders

No debugger screenshots are currently stored in this repository. Insert the actual captures below before circulating this report externally.

```text
Suggested path: images/secure_vs_nonsecure_app_body_compare.png
Required content: compare result showing App body equality beginning at 0x60100250.
```

![Secure and non-secure App body comparison](images/secure_vs_nonsecure_app_body_compare.png)

```text
Suggested path: images/secure_package_body_vs_raw_app.png
Required content: package offset 0x200 and raw App prefix match result.
```

![Secure package body and raw App comparison](images/secure_package_body_vs_raw_app.png)

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
| Vector base | App vector is copied to ATCM `0x00000000` | Same intended destination | Current highest-priority runtime-context item to verify |
| SDRAM/xSPI setup | Performed in SSBL before App handoff | Performed by Loader path | Verify runtime registers and memory contents are identical at the handoff point |

## 7. High-Priority Hypothesis: Exception Vector Base (VBAR)

The App vector table is copied by the manifest to:

```text
VECTOR_WBLOCK = 0x00000000 (ATCM)
```

In App `startup_core.c`, the `VBAR = __Vectors` setup is inside the `#if APP_STANDALONE_DEBUG` block. Integrated Loader + App execution uses:

```c
#define APP_STANDALONE_DEBUG (0)
```

Therefore the integrated App startup may inherit the VBAR value from SSBL/BootROM instead of explicitly setting VBAR to the App vector table.

This hypothesis explains the observed timing:

1. Direct execution can reach App `main()` without an exception.
2. FreeRTOS task startup/scheduling may use SVC, IRQ, FIQ, or reveal an Abort.
3. The CPU may then branch through a stale Loader vector table rather than the App vector table at `0x00000000`.
4. Execution appears in `Default_Handler` even though the underlying fault source may be elsewhere.

This is a hypothesis only. It must be confirmed by register capture, not assumed as root cause.

## 8. Debug Actions Already Attempted

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

## 9. Required Debug Captures for Joint Investigation

Please collect the following captures for both the secure SSBL path and the known-good non-secure path, at the same checkpoints whenever possible.

### 9.1 Checkpoint A: immediately before `app_prg()`

Capture:

- PC, SP, LR, CPSR.
- `VBAR`.
- `SCTLR`, MPU enable state, I-cache and D-cache bits.
- GIC distributor/CPU interface enable state and pending interrupts.
- Contents/CRC of all copied App runtime sections.

### 9.2 Checkpoint B: App `system_init` / `SystemInit`

Capture:

- PC and current CPU mode.
- `VBAR`; expected App vector base is `0x00000000`.
- ATCM contents at `0x00000000..0x00000043`; expected to contain App vector entries.
- Stack pointers for SVC, IRQ, Abort, Undefined, System, and FIQ modes after `stack_init()`.

### 9.3 Checkpoint C: before and after first static task creation

Capture:

- Calling function and source line.
- TCB address, task stack start/end, stack fill pattern.
- CPSR and SP before/after FreeRTOS API call.
- FreeRTOS scheduler state and current task pointer.
- Any SVC/IRQ/FIQ/Abort entry.

### 9.4 Checkpoint D: exception stop

Capture at the first exception, before continuing:

- PC, LR, SP, CPSR and active CPU mode.
- `VBAR`.
- `SPSR_abt`, `SPSR_und`, and `SPSR_irq` as applicable.
- Data-abort status/address: `DFSR`, `DFAR`.
- Prefetch-abort status/address: `IFSR`, `IFAR`.
- The vector entry and handler target used for the exception.
- Call stack/disassembly around the instruction that raised the exception.

### 9.5 Screenshot placeholders

```text
Suggested path: images/freertos_task_create_breakpoint.png
Required content: first static task creation, TCB/stack variables, PC and call stack.
```

![FreeRTOS static task creation](images/freertos_task_create_breakpoint.png)

```text
Suggested path: images/default_handler_exception_registers.png
Required content: first exception stop with PC, CPSR, VBAR, DFSR/DFAR and call stack.
```

![Exception register capture](images/default_handler_exception_registers.png)

```text
Suggested path: images/app_vector_atcm_vbar.png
Required content: VBAR register and memory window at 0x00000000 after the App vector copy.
```

![App vector and VBAR](images/app_vector_atcm_vbar.png)

## 10. Questions for Product Line / FSP / IAR Support

1. In the RZ/N2L Loader + App split architecture, should the App always explicitly program `VBAR` after the Loader has copied `VECTOR_WBLOCK` to ATCM?
2. Is it supported for `APP_STANDALONE_DEBUG = 0` to skip the `VBAR = __Vectors` operation when handoff is performed by a custom Secure SSBL rather than the original monolithic startup path?
3. What CPU state must a custom SSBL restore or preserve before jumping to an App `system_init` entry on Cortex-R52?
4. Are there required RSIP close/deinitialization, GIC, cache, MPU, or TCM barriers beyond `DSB`/`ISB` before handing off to a FreeRTOS App?
5. Does the IAR Cortex-R52/FreeRTOS port require a specific exception mode, VBAR state, stack setup, or debug setting at the initial App entry?
6. Which fault-status registers should be prioritized for a FreeRTOS task-creation failure that subsequently enters `Default_Handler`?

## 11. Current Status and Proposed Next Step

### Confirmed

- Secure SSBL boots and verifies the signed plaintext App package.
- Secure and non-secure Flash images have identical App body bytes.
- Current package/body/link address agreement is correct at `0x60100250`.
- App manifest deployment reaches the App startup path and `main()`.
- The fault occurs later, around FreeRTOS static task creation/scheduler-related execution.

### Not yet confirmed

- The exact exception type and instruction that first causes the fault.
- Whether App `VBAR` points to `0x00000000` after integrated Secure SSBL handoff.
- Whether all exception-mode stack pointers are correct after App `stack_init()`.
- Whether secure and non-secure CPU/GIC/MPU/cache context is identical at the first FreeRTOS task operation.

### Recommended immediate action

Run a paired secure/non-secure debug session with breakpoints at:

```text
1. Secure SSBL: immediately before app_prg()
2. App: system_init()
3. App: SystemInit()
4. App: first static task creation API call
5. App: Default_Handler / Abort_Handler / SVC_Handler / IRQ_Handler
```

At each point, record the register and memory fields in Section 9. The first divergence, especially in `VBAR`, exception mode, stack pointer, or fault-status register, should identify the next root-cause investigation direction.
