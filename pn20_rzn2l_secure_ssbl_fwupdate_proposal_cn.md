# RZN2L PN2.0 Secure SSBL + Secure App + FWUpdate 方案建议

本文档用于说明如何将 Renesas secure boot 和 secure firmware update 机制应用到 RZN2L PN2.0 的 Loader/App 拆分工程中，并给出一个分阶段、可落地、便于验证的工程方案。

当前 PN2.0 工程已经从原始 xSPI boot 单体工程拆分为两个 IAR 工程：

| 工程 | 角色 |
| --- | --- |
| `RZN2L_bsp_xspi0bootx1_loader` | 当前非安全 Loader。负责初始化必要硬件、读取 App manifest、把 App 各段复制到对应运行内存，并跳转到 App `system_init`。 |
| `RZN2L_bsp_xspi0bootx1_app` | PROFINET IRT App。负责发布固定格式的 App manifest，并运行在 ATCM、BTCM、System RAM、SDRAM/HRAM、non-cache 等多个内存区域。 |

安全化设计不建议把 PN2.0 App 强行套进 Renesas secure firmware update 示例中的“单一连续 RAM 镜像”模型。更合适的方向是保留当前多段 App manifest 思路，并逐步加入安全验签、可选加密、安全升级、dual-bank 管理和 anti-rollback。

## 1. 核心提议

采用 **整包验签，分段部署** 的方案。

也就是：

```text
xSPI Flash 中保存一个完整的 secure App package。
SSBL 先对整个 secure package 做完整性和真实性验证。
验签成功后，SSBL 解析已经被签名保护的 secure manifest。
SSBL 按 manifest 把每个 App 段解密/复制到对应目标内存。
SSBL 最后跳转到被签名保护的 App entry point，通常是 App system_init。
```

本文将这个方案称为 **方案 A：整包验签，分段搬运**。

关键点是：**认证对象是完整 secure App package，部署方式仍然遵循 PN2.0 App 的真实多内存布局**。

## 2. 为什么不能直接使用单镜像示例模型

Renesas secure update 示例在 RZN2L 上使用的是较简单的 UserApp 参数模型：

```text
src_addr
img_size
app_start_addr
signature
```

示例 SSBL 会把一个连续 image 复制到 `app_start_addr`，然后通过 secure boot certificate 流程进行验证。

这个模型适合 App 很小、并且运行在单一连续 RAM 区域的场景，例如整个 App 都在 ATCM 中运行。但 PN2.0 App 明显不是这种结构：

| PN2.0 App 区域 | 运行内存 |
| --- | --- |
| App startup / LDR code | BTCM |
| Vector table | ATCM |
| 主 App code/data | System RAM |
| PROFINET/system code/data | SDRAM 或 HRAM |
| Non-cache data | System RAM non-cache mirror |
| Shared buffers | Shared non-cache mirror |

因此，PN2.0 需要一个描述多段布局的 secure manifest，而不是只依赖一个 `app_start_addr` 和 `img_size`。

## 3. 建议的 Secure App Package 格式

一个实用的 secure App package 可以按如下结构组织：

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

签名保护范围至少应包含：

```text
Code Certificate
Secure App Manifest
所有 segment payload；启用 App 加密时，建议签名覆盖加密后的 payload
```

Secure manifest 需要包含足够信息，让 SSBL 可以安全部署 App：

| 字段 | 作用 |
| --- | --- |
| magic/version | 识别 secure manifest 格式。 |
| manifest_size | 支持后续格式扩展。 |
| package_version | App/package 版本，用于 anti-rollback。 |
| entry_point | 最终跳转目标，通常是 App `system_init`。 |
| segment_count | 段记录数量。 |
| segment records | 每段的源 offset/address、目标地址、大小、flags、内存类型、IV/key 信息等。 |
| total_package_size | 用于 package 边界检查。 |
| hash/signature binding | 确保段布局和 payload 不能被篡改。 |

每个 segment record 建议显式记录：

```text
segment_type
flash_cipher_offset 或 flash_plain_offset
dst_addr
plain_size
cipher_size
flags
iv 或 iv_index
alignment requirement
```

目标地址、段大小、段类型必须包含在认证数据中。否则攻击者可能复用合法 payload，但把它重定向到非预期内存区域。

## 4. 与本方案相关的 RSIP 行为

基于 Renesas secure firmware update 示例中使用的 FSP secure boot library，可以确认：

- `R_RSIP_SB_ManifestVerify()` 接收 Key Certificate 和 Code Certificate 指针。
- 这个 API 不显式接收 image 指针。
- Image 地址来自 Code Certificate header 中的 `dest_addr` 字段。
- Image 长度来自 Code Certificate header 中的 `img_len` 字段。
- 对于加密 image，Image Cipher Info TLV 会提供 cipher 参数和解密目标地址。

这意味着，在满足以下条件时，可以把 xSPI Flash 作为验签源：

| 条件 | 说明 |
| --- | --- |
| xSPI 已经处于 memory mapped 可读状态 | SSBL 需要在验签前初始化 xSPI/protocol。 |
| Certificate 中的 image 地址指向 xSPI | `code_cert.dest_addr` 可以指向 xSPI memory space 中的 secure package payload。 |
| 地址满足对齐要求 | library 会检查 certificate/image 相关地址的 4-byte alignment。 |
| xSPI cache/MPU/bus 设置正确 | CPU/RSIP 读取到的 package 内容必须一致。 |

但是，`R_RSIP_SB_ManifestVerify()` 本质上仍然是面向连续 image 的 secure boot API，不是面向 PN2.0 多段 App 的 scatter-loader。

推荐理解方式：

```text
使用 RSIP secure boot/certificate verification 对整个 package 做认证。
使用 SSBL 自己的逻辑解析已认证的 secure manifest。
使用 RSIP AES API 或其它 RSIP 支持的解密路径，把每个 segment 从 xSPI 解密到对应 RAM/SDRAM 区域。
```

不要依赖一个 secure boot API 自动完成 PN2.0 的多段解密和 scatter-load。

## 5. 建议的 SSBL 启动流程

Secure SSBL 建议按以下流程执行：

```text
1. Boot ROM 验证并启动 Secure SSBL。
2. Secure SSBL 初始化最小运行环境。
3. Secure SSBL 初始化 xSPI memory-mapped read。
4. 如果 App 段需要部署到外部 RAM，Secure SSBL 先初始化 SDRAM/HRAM。
5. Secure SSBL 打开 RSIP。
6. Secure SSBL 选择 active App bank 或 single App package 地址。
7. Secure SSBL 读取 Key Certificate 和 Code Certificate。
8. Secure SSBL 对整个 secure App package 做验签。
9. Secure SSBL 解析已经通过认证的 secure App manifest。
10. Secure SSBL 使用固定内存白名单校验所有 segment 的地址范围。
11. Secure SSBL 将每个 segment 解密/复制到目标内存。
12. Secure SSBL 执行 cache clean/invalidate，并执行 DSB/ISB。
13. Secure SSBL 跳转到认证过的 `entry_point`，通常是 App `system_init`。
```

SSBL 不应该直接跳转到 App `main`。当前 PN2.0 拆分方案中，App 侧启动流程仍然由 App 工程自己负责，这是正确的。

## 6. Segment 校验规则

在复制或解密任何 segment 之前，SSBL 必须执行严格校验。

必要检查项：

| 检查项 | 原因 |
| --- | --- |
| `dst_addr + size` 不发生溢出 | 防止地址 wraparound。 |
| Segment 目标地址位于允许区域 | 防止写入 SSBL、RSIP buffer、stack 或寄存器区域。 |
| Segment 源地址位于 active package/bank 内部 | 防止越界读取 package 外的数据。 |
| 必要 segment 的 size 非零 | 防止 malformed entry。 |
| 对齐满足 RSIP 和 CPU 要求 | AES/CBC 通常要求 cipher length 为 16-byte 倍数；certificate 需要 4-byte alignment。 |
| Segment 之间不发生非法重叠 | 防止部署过程互相覆盖。 |
| Entry point 位于允许的 App LDR code 区域 | 防止跳转目标被重定向。 |
| Segment type 与目标白名单匹配 | 例如 VECTOR 只能写入 ATCM vector 区，SYSTEM_PRG 只能写入 SDRAM/HRAM code 区。 |

示例白名单：

| Segment type | 允许目标区域 |
| --- | --- |
| LDR_PRG / LDR_DATA | App BTCM offset 区域，不能覆盖 SSBL BTCM 区域。 |
| VECTOR | ATCM vector 区。 |
| USER_PRG / USER_DATA | App System RAM 区。 |
| SYSTEM_PRG / SYSTEM_DATA | SDRAM 或 HRAM App 区。 |
| NONCACHE | App non-cache init/runtime 区。 |
| SHARED_NONCACHE | Shared non-cache buffer 区。 |

这些校验和密码学验签同样重要。即使 image 签名正确，如果 manifest 对部署地址限制太宽，也会带来安全风险。

## 7. App 加密策略

在整包认证流程跑通后，再加入 App 加密。

推荐加密模型：

```text
Flash 中保存加密后的 segment payload。
Secure manifest 记录每个加密 segment 的 offset、cipher size、plain size、目标地址、IV 和 flags。
SSBL 先验证完整 package。
验签成功后，SSBL 从 xSPI Flash 直接把每个 segment 解密到目标 RAM/SDRAM。
```

推荐顺序：

```text
先验证 encrypted package。
验证成功后再执行解密。
```

这样可以避免部署未认证的 plaintext，也可以避免使用大型临时 RAM buffer。

实现注意事项：

- AES-CBC 要求 16-byte block alignment。Package 生成工具需要对每个加密 segment 做 padding。
- Manifest 应同时记录 `plain_size` 和 `cipher_size`，便于 SSBL 正确处理 padding。
- 每个 segment 应有明确 IV 策略。相比多个离散段共用一个 IV，per-segment IV 更容易分析和维护。
- SSBL 工程需要打开所需的 RSIP AES 功能，而不仅仅是 secure boot verification。
- 如果直接使用 Renesas secure boot Image Cipher Info，要注意它是面向连续 image 的，不是面向多目标 scatter image 的。

## 8. FWUpdate 策略

Secure FWUpdate 应写入一个完整的新 secure App package，并且只有在验证成功后才切换 boot 状态。

推荐升级流程：

```text
1. 当前运行 App 或 update mode 接收新的 secure App package。
2. FWUpdate 将 package 写入 inactive App bank 或 staging area。
3. FWUpdate 验证 package 结构和密码学真实性。
4. 只有在 image 写入成功后，FWUpdate 才写入新的 App 参数/metadata。
5. FWUpdate 将 flash management 状态切换到新 bank。
6. 设备复位。
7. SSBL 启动时再次验证被选择的 bank。
8. 如果 boot 验证失败，并且启用了 dual-bank，SSBL 回退到上一个有效 bank。
```

FWUpdate 绝不能在完整 package 写入并验证成功前切换 active 状态。

## 9. Dual-Bank 策略

对于工业 PROFINET 产品，强烈建议使用 dual-bank。

一种可能的 xSPI 布局如下：

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

最终地址需要根据真实 flash 容量、erase block size、最大 App package size 和 alignment 要求重新计算。Bank 边界应按 erase block 对齐，并且不要和 SSBL/key/parameter 区共用同一个 erase block。

Dual-bank boot policy：

| 状态 | 行为 |
| --- | --- |
| Active bank 有效 | 启动该 bank。 |
| Active bank 无效，但 previous bank 有效 | 回退启动 previous bank。 |
| 两个 bank 都无效 | 停留在 recovery/update mode。 |
| Update 中断 | 继续使用 previous bank。 |

## 10. Anti-Rollback 策略

Anti-rollback 建议在 secure boot、加密、FWUpdate、bank switching 稳定后再加入。

推荐设计：

```text
每个 secure App package 有单调递增版本号。
SSBL 启动前检查 package version 是否满足 OTP/version counter 要求。
FWUpdate 只接受比当前 committed version 更新的 package。
Version counter 只在正确的 commit point 后推进。
```

重要注意事项：

- OTP/version counter 是不可逆资源。
- 在完整 update 和 recovery 路径验证完成前，不建议烧写 anti-rollback 状态。
- 建议准备 development mode：编译进 version check，但暂时禁用 OTP update。

## 11. 分阶段实施计划

不要一次性实现所有安全功能。推荐按以下阶段推进。

### Phase 1：SSBL 安全化基础

目标：把当前 Loader 改造为 Secure SSBL，但 App 暂时保持 plaintext。

任务：

```text
1. 从 Renesas SecureSSBL 示例移植 RSIP 初始化和 certificate verification。
2. 定义 secure App package 地址和 certificate 布局。
3. 生成 signed plaintext App package。
4. 直接从 xSPI Flash 验证 package。
5. 保留当前 App manifest copy flow，但只有 package 验证成功后才信任 manifest。
6. 增加 segment 目标地址白名单校验。
```

退出标准：

```text
Secure SSBL 可以从 xSPI 验证 signed plaintext App package，并成功启动 App system_init。
```

### Phase 2：APP 加密

目标：让 App payload 在 xSPI Flash 中保持加密状态。

任务：

```text
1. 扩展 secure manifest，加入 per-segment encryption metadata。
2. 在 SSBL 工程中打开所需 RSIP AES 功能。
3. 更新 package 生成工具，对 segment 加密并按 block size padding。
4. 先验证 encrypted package，再执行 segment decrypt/copy。
5. 确认 xSPI App bank 中不再保留 plaintext App，允许存在的 metadata/certificate 除外。
```

退出标准：

```text
xSPI 保存加密 App segments，SSBL 将其解密到 RAM/SDRAM，App 正常启动。
```

### Phase 3：Secure FWUpdate

目标：加入 secure App package 的认证升级能力。

任务：

```text
1. 移植或适配 Renesas FWUpdate program，使其支持 PN2.0 package 格式。
2. 通过选定升级通道接收 secure App package。
3. 将 package 写入 staging area 或 inactive bank。
4. 在标记为 bootable 前验证 package。
5. 保证写入失败或掉电时仍有恢复路径。
```

退出标准：

```text
无需外部烧写工具即可安装新的 signed package，非法 package 会被拒绝。
```

### Phase 4：Dual-Bank Boot And Update

目标：让升级对中断和错误 image 更鲁棒。

任务：

```text
1. 定义 App Bank A 和 App Bank B 布局。
2. 增加 active/pending/previous bank 的 flash management 状态。
3. FWUpdate 只写 inactive bank。
4. SSBL 启动前验证 selected bank。
5. 当 selected bank 验证失败时执行 fallback。
```

退出标准：

```text
升级过程中掉电不会导致设备变砖，SSBL 可以回退到 previous valid bank。
```

### Phase 5：Anti-Rollback

目标：阻止启动或安装旧版本但签名合法的固件。

任务：

```text
1. 在 secure manifest/certificate policy 中定义 version 字段。
2. 在 SSBL 中加入 OTP/version counter 检查。
3. 在 FWUpdate 中加入版本接受策略。
4. 只在正确 commit point 更新 version counter。
5. 验证 rollback rejection 和 recovery 行为。
```

退出标准：

```text
旧版本 signed package 会被拒绝，并且 update/fallback 场景中的版本状态保持一致。
```

## 12. 可能限制和风险

### 12.1 RSIP Secure Boot API 面向连续 image

Renesas secure boot API 验证的是 Code Certificate 描述的一个 image。它原生不理解 PN2.0 的多段 App manifest。SSBL 需要自己增加 secure manifest 解析和 scatter-load 逻辑。

### 12.2 单个 API 不提供自动 Decrypt-And-Scatter

Renesas SB library 可以基于 Image Cipher Info 解密加密 image，但内置模型是连续源地址和连续目标地址。PN2.0 的多段部署需要 SSBL 自己控制 per-segment decrypt/copy。

### 12.3 xSPI 直接验签依赖 Memory-Mapped Read 稳定性

直接从 xSPI Flash 验证要求 xSPI protocol mode、bus timing、MPU/cache 设置和 4-byte alignment 都正确。任何不一致都可能导致验签失败或 hard fault。

### 12.4 SDRAM/HRAM 必须在部署前可用

如果 App 段需要部署到 SDRAM 或 HRAM，SSBL 必须先初始化对应外部 RAM。这会增加 SSBL 职责，并且要避免和 App 侧初始化冲突。

### 12.5 AES Padding 和 Section Size 需要严格一致

加密 segment size 必须满足 cipher mode 要求。Package 生成工具和 SSBL 必须对 `plain_size`、`cipher_size`、padding 行为保持一致。

### 12.6 Key Management 必须提前规划

需要明确 image encryption key 来自 OTP secure boot common key、flash 中 wrapped key，还是其它 RSIP 支持机制。Key injection 和 key update policy 必须与 FWUpdate 对齐。

### 12.7 加密后调试难度增加

一旦 App payload 在 xSPI 中加密，基于 map 文件和 flash 内容的调试会更困难。建议在 secure boot 和 section deployment 稳定前保留 plaintext signed bring-up mode。

### 12.8 Dual-Bank 需要足够 Flash 容量

PN2.0 App 可能较大。Dual-bank 需要同时容纳两个完整 secure package，以及 SSBL、FWUpdate、key、parameter 和 management 区。

### 12.9 Anti-Rollback 可能永久阻止测试镜像

OTP-based version counter 是不可逆的。Anti-rollback 应在较后阶段启用，并先使用 development policy 做充分验证。

### 12.10 RASC 重新生成风险

PN2.0 工程已经包含自定义 loader/app split 修改。RASC 重新生成可能覆盖 ICF、工程设置、生成配置或 App/Loader handoff 约定。生成后必须 review diff，再决定是否编译。

## 13. PN2.0 实施建议：整包验签 + 分段部署

本节把“方案 A：整包验签，分段搬运”落到 PN2.0 Loader/App 拆分工程的实施层面。核心目标是：**不要把 PN2.0 App 改造成 Renesas 例程那种单一连续 RAM image，而是在安全认证边界内保留 PN2.0 现有多段部署模型**。

### 13.1 PN2.0 方案与 Renesas 例程方案的差异

| 项目 | Renesas SecureSSBL / SecureFWUpdate 例程 | PN2.0 建议方案 |
| --- | --- | --- |
| App 形态 | 一个连续 secure boot image。 | 一个完整 secure package，内部包含 secure manifest 和多个 segment。 |
| 部署方式 | SSBL 将连续 image 放到一个运行地址。 | SSBL 按 manifest 将多个段部署到 ATCM/BTCM/System RAM/SDRAM/non-cache 等区域。 |
| 验签对象 | Code Certificate 描述的单一连续 image。 | Code Certificate 描述整个 secure package，manifest 和所有 segment 都在认证范围内。 |
| 解密模型 | Secure boot library 可对连续 image 做解密。 | 整包先验签；验签后由 PN2.0 SSBL 按段调用公开 RSIP AES API 或等效解密路径。 |
| Manifest | UserApp parameter 只描述 `src_addr/img_size/app_start_addr` 等少量信息。 | Secure manifest 显式描述每段 offset、目标地址、plain/cipher size、flags、IV/key 信息和 entry point。 |
| FWUpdate 写入内容 | 写入一个示例格式的 UserApp secure boot image。 | 写入一个完整 PN2.0 secure App package 到 inactive bank 或 staging area。 |
| 安全边界 | 适合小型连续镜像。 | 适合大 App、多 RAM 区域和复杂 scatter loading。 |

因此 PN2.0 的实现重点不是修改 `R_RSIP_SB_ManifestVerify()` 让它理解多段，而是把它用于**认证完整 package**，再由 SSBL 自己完成安全的 manifest 解析和分段部署。

### 13.2 Secure package 的认证边界

推荐把 Code Certificate 的 image 区域设置为一个连续 package body：

```text
package_body = secure_manifest + all_segment_payloads
```

这样 `R_RSIP_SB_ManifestVerify()` 看到的仍然是一个连续 image，符合 Renesas secure boot library 的模型；但该连续 image 的内部由 PN2.0 自定义 manifest 描述多个部署段。

推荐 Flash 中的实际布局为：

```text
App Bank N
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+ <- Code Certificate dest_addr 指向这里
| Secure Manifest             |
+-----------------------------+
| Segment 0 payload           |
+-----------------------------+
| Segment 1 payload           |
+-----------------------------+
| ...                         |
+-----------------------------+
```

验签范围建议覆盖：

```text
Secure Manifest
所有 segment payload
所有会影响部署行为的字段：目标地址、长度、类型、flags、entry point、version
```

关键原则：**SSBL 在 `R_RSIP_SB_ManifestVerify()` 成功返回之前，不能信任 secure manifest 中的任何地址、长度或 entry point 字段。**

### 13.3 SSBL 主流程伪代码

下面伪代码保留 PN2.0 当前 Loader 的职责，但在使用 manifest 和搬运 segment 前增加整包认证和白名单校验。

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

### 13.4 Manifest 与 segment 校验伪代码

Manifest 校验必须在部署前集中完成，避免一边解析一边写内存时才发现后续字段非法。

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

白名单建议在 SSBL 中使用固定表，而不是完全依赖 manifest 提供：

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

### 13.5 分段解密/复制伪代码

对于 PN2.0，推荐把 secure boot library 的解密能力理解为“连续 image 解密能力”，不要用它来承担多段 scatter-load。分段解密建议由 SSBL 自己按 segment record 驱动。

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

实际 RSIP API 名称、key 类型和 wrapped key 存储方式需要按当前 FSP/RSIP 配置确认。实施时应先做 signed plaintext package，验证整包认证和分段复制正确，再启用 AES-CBC 分段解密。

### 13.6 FWUpdate 侧实施伪代码

PN2.0 FWUpdate 不建议只写某个裸 binary，而应写入完整 secure App package。写入 inactive bank 后，先做 package 级验证，再切换 boot metadata。

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

`mark_bank_pending()` 必须是最后阶段动作。掉电发生在 package 写入过程中时，旧 active bank 仍应保持有效；掉电发生在 pending 写入之后，SSBL 下次启动仍必须重新验签 pending bank，失败则回退 previous bank。

### 13.7 推荐实施顺序

为了降低风险，建议按下面顺序编码和验证：

```text
1. 保留当前 PN2.0 non-secure segment manifest，先抽象出固定 ABI。
2. PC 工具生成 signed plaintext secure package，payload 仍为明文 segment。
3. SSBL 从 xSPI 对 package body 做整包验签。
4. 验签通过后解析 secure manifest，并用白名单校验所有 segment。
5. SSBL 按段复制明文 payload，确认 App 可启动。
6. PC 工具增加 per-segment AES-CBC 加密和 padding。
7. SSBL 增加 per-segment RSIP AES decrypt-to-destination。
8. FWUpdate 改为写完整 secure package 到 inactive bank。
9. 增加 dual-bank pending/active/previous 状态机。
10. 最后加入 anti-rollback version policy。
```

这个顺序可以保证每一步都有清晰退出标准，也能避免在整包验签、分段部署、解密、升级状态机同时变化时难以定位问题。

## 14. 推荐的近期动作

1. 固化当前 non-secure App manifest ABI，并记录精确 section address 规则。
2. 定义 secure manifest 结构和 package generator 输出格式。
3. 先生成 signed plaintext package。
4. 修改 SSBL，使其在使用 manifest 前先从 xSPI 验证 package。
5. 增加严格的 segment 目标地址白名单校验。
6. 之后再启用 segment encryption 和 RSIP AES decrypt-copy。

核心工程原则：

```text
先认证完整 package。
只信任已经认证的 manifest 字段。
只部署到固定白名单内存区域。
把加密、升级、dual-bank、anti-rollback 分阶段加入。
```
