# RZ/N2L Secure Boot 与 PN2.0 Secure Boot 开发培训大纲

## 1. 培训定位

### 1.1 目标

本培训面向需要在 RZ/N2L 平台开发、集成、验证或维护 Secure Boot、Secure Firmware Update 和 PN2.0 PROFINET IRT 安全启动方案的工程师。完成培训后，学员应能够：

- 解释 Boot ROM、Secure SSBL、Secure App、OTP、RSIP、证书和密钥之间的信任链；
- 独立复现 Renesas R01AN6526 Secure Boot 与 R01AN6527 Secure Firmware Update 示例的非破坏性流程；
- 理解当前 PN2.0 Loader/App 分离工程的多段内存布局，以及它与官方单一连续镜像例程的差异；
- 生成、检查和签名 PN2.0 Secure App package，并分析启动、验签、解密、分段部署失败；
- 设计可验证、可回退、具备版本/反回滚约束的 Secure Firmware Update 与 Dual Bank 方案；
- 清楚区分开发验证操作与 OTP/调试口/安全启动等不可逆量产操作。

### 1.2 建议对象与先决条件

- 嵌入式软件、Bootloader、现场应用、测试与量产工程师；
- 熟悉 C、IAR EWARM、基本 ARM Cortex-R 启动过程、xSPI Flash；
- 建议预先阅读当前工程的 Loader/App 分离说明和 PN2.0 Secure SSBL 方案文档；
- 不要求学员在培训中写 OTP、启用 SBEN 或禁用 SCI/USB Boot。

### 1.3 建议形式

建议采用 5 天、每天 6 小时的“讲解 + 代码走读 + 主机侧实验 + 板端观察”形式。若仅面向架构/测试人员，可压缩为 3 天并去掉代码实现实验。

---

## 2. 资料、例程与工程地图

| 类别 | 主要资料/位置 | 在课程中的用途 |
| --- | --- | --- |
| 安全功能总览 | `r01uh1017ej0110-rzt2-rzn2-security.pdf` | OTP、Boot ROM、证书、RSIP、JTAG 认证、Secure Update 的硬件与产品边界。 |
| 加密库 | `r01an6547ej0300-rzt2-n2-security-crypto.pdf` | RSIP/密码库 API、wrapped key、AES/SHA/ECC/RSA 的使用边界。 |
| Secure Boot 例程 | `r01an6526ej0310-rzt2-n2-security-secureboot/` | 官方 Secure Boot 镜像、参数生成、签名、Flash 写入、启动验证。 |
| Secure FWUpdate 例程 | `r01an6527ej0310-rzt2-n2-security-fwupdate/` | 官方安全更新、bank/metadata 切换、版本控制和回退机制。 |
| Debug Authentication | `r01an7322ej0100-rzt2-n2-security-debug-authentication.pdf` | 量产调试口保护和开发调试策略。 |
| RSK 配套资料 | `6_RSK_for_SecuritySolution/RZN2L/` | 板卡、拨码、连接、Flash/调试准备。 |
| 初学者导读 | `RZN2L_Security_Beginner_Guide_CN.md` | 术语预习、信任链和 OTP 风险说明。 |
| 当前工程架构 | `README_loader_app_split.md` | PN2.0 分离 Loader/App、RZAP、内存分段和 App 启动入口。 |
| 当前 Secure Boot 验证流程 | `pn20_rzn2l_secure_boot_python_validation_cn.md` | 当前 R01AN6526 工具使用、签名、xSPI 写入、UART 验收。 |
| PN2.0 方案设计 | `pn20_rzn2l_secure_ssbl_fwupdate_proposal_cn.md` | 整包认证、分段部署、加密、双 Bank、反回滚策略。 |
| 方案对比与排障 | `pn20_rzn2l_secure_app_scheme_comparison_cn.md`、`pn20_secure_ssbl_freertos_fault_investigation_cn.md` | Scheme A/B 取舍、启动/FreeRTOS 故障分析。 |

**资料包根目录：**
`C:\RenesasDev\Temp\issue support\Googol\security issue\r01an6956ej0310-rzt2m-t2l-n2l_security_soultion_package`

---

## 3. 第 0 模块：培训环境和安全边界（0.5 天）

### 学习目标

- 建立实验环境、工具路径、工程与硬件版本的可追溯记录；
- 理解“可逆验证”与“不可逆 provisioning”的区别；
- 建立不误烧 OTP、不锁死调试口的实验纪律。

### 讲解内容

1. RZ/N2L、RSK/SOM、xSPI Flash、串口和调试器的实验连接；
2. IAR、RASC/FSP、Python、Renesas Secure Boot 工具的版本与目录确认；
3. 开发密钥、测试密钥、量产根密钥的隔离原则；
4. 不可逆操作清单：OTP CKEY/RPKEY 写入、SBEN、JTAG 认证/永久关闭、SCI/USB Boot disable；
5. 日志、Map 文件、签名输入/输出、SHA-256 的归档要求。

### 实验/交付物

- 填写一份实验环境检查表：板卡版本、Flash 分区、调试口、串口、工具版本、当前 Git commit；
- 对当前 Secure SSBL、Secure App、public key 文件计算 SHA-256 并归档；
- 不执行任何 OTP 编程、SBEN 使能或调试口永久关闭操作。

---

## 4. 第 1 模块：RZ/N2L 安全体系与信任链（0.5 天）

### 对应资料

- Security Features User's Manual；
- `RZN2L_Security_Beginner_Guide_CN.md`。

### 讲解内容

1. Normal Boot 与 Secure Boot 的差异；
2. Boot ROM -> Secure SSBL -> Secure App 的信任链；
3. OTP：UID、CKEY0~3、RPKEY、SBEN、ARBC、AUTHMODEJ、WORDLOCK；
4. Key Certificate、Code Certificate、Manifest 与 TLV；
5. ECDSA P-256/SHA-256 的认证职责，以及 AES-128-CBC 的保密职责；
6. secure boot common key 与 wrapped key 的含义；
7. 调试认证、Secure Update、反回滚在产品生命周期中的位置。

### 关键检查题

- 为什么“签名正确”不等于“部署地址一定安全”？
- 为什么 secure package 必须先验签，后解析自定义 manifest？
- 为什么 OTP 配置不能用普通软件调试思路处理？

---

## 5. 第 2 模块：RSIP 与加密库（0.5 天）

### 对应资料

- R01AN6547 Cryptographic Library；
- Loader 中的 RSIP/FSP 生成配置及 `secure_app_verify.c`、`secure_app_deploy.c`。

### 讲解内容

1. RSIP 的职责：验签、hash、AES、TRNG、密钥封装；
2. 通用 FSP AES API、wrapped key 与 OTP `ckey0` 的差异；
3. CIP driver 的镜像解密路径和 `R_CIP_DRV_PrcDecryptImage()`；
4. AES-CBC 的分组、IV、链式依赖与 16-byte 对齐；
5. 明文长度 `file_size`、Flash 密文长度 `cipher_size` 与尾部零填充；
6. 认证和解密的顺序：先认证完整 package，再部署/解密段；
7. RSIP Open -> ManifestVerify -> deploy -> Close 生命周期。

### 实验/交付物

- 从 RASC/FSP 配置确认 AES-128 与 CBC 功能已启用；
- 用主机侧工具完成一个 AES-CBC 加密/解密回环；
- 分析非 16-byte 对齐的 `SYSTEM_DATA` 为什么需要 `cipher_size > file_size`。

---

## 6. 第 3 模块：R01AN6526 官方 Secure Boot 例程（1 天）

### 对应资料

- `3_Secure_Boot_Sample_Program_Package/r01an6526ej0310-rzt2-n2-security-secureboot/`；
- R01AN6526 应用说明与其 `Secure device setup` 工具；
- 当前工程 `pn20_rzn2l_secure_boot_python_validation_cn.md`。

### 讲解内容

1. 6526 例程的目录、工程、密钥、参数、镜像和日志；
2. `parameter_generator.py` 与 `secureboot_utility.py` 的输入/输出关系；
3. loader parameter、Key Certificate、Code Certificate、signed image 的布局；
4. `src_addr`、`app_start_addr`、`dest_addr`、`img_len` 和 Flash 地址的关系；
5. 外层镜像加密的 Image Cipher Info 与明文 signed image；
6. 写入 Flash 前后如何比较 public key、hash、长度和地址；
7. UART 启动日志的成功/失败判据。

### 实验

1. 只在开发板/可擦除 Flash 环境中复现官方示例生成流程；
2. 解剖一份生成的 Key/Code Certificate，记录 image address、length、version、encryption flags；
3. 使用当前项目的已验证 Python 流程生成 Secure SSBL 和 Secure App；
4. 正常 xSPI 启动，收集 UART 日志并完成验收表。

### 验收标准

- 能说明 Boot ROM 认证的是哪一层；
- 能说明 Secure SSBL 再认证 App package 的哪一层；
- 不执行 OTP provisioning 的情况下完成可重复的签名/Flash/启动验证。

---

## 7. 第 4 模块：R01AN6527 官方 Secure Firmware Update 例程（1 天）

### 对应资料

- `4_Secure_Firmware_Update_Sample_Program_Package/r01an6527ej0310-rzt2-n2-security-fwupdate/`；
- Security Features User's Manual 的 Secure Update/ARBC 章节；
- 当前工程 `pn20_rzn2l_secure_ssbl_fwupdate_proposal_cn.md`。

### 讲解内容

1. 6527 例程的角色划分：当前镜像、更新镜像、更新控制信息与启动选择；
2. 安全更新与 Secure Boot 的共同点和差异；
3. inactive bank/staging、原子切换、掉电恢复、回退；
4. image version、ARBC 与 anti-rollback 的设计约束；
5. 为什么“写完 package”不能等同于“允许切换 active bank”；
6. 更新包验签、结构校验、写入校验和 boot-time 二次验证；
7. 单 Bank、双 Bank 与 staging 的适用范围。

### 实验

1. 梳理 6527 示例中的 Flash 区域、metadata、状态迁移和版本字段；
2. 设计一次中断点：下载中断、写入校验失败、切换前掉电、切换后启动验签失败；
3. 为 PN2.0 工程输出一张状态机图和 bank 分区草案；
4. 在不写 OTP 的情况下做 host-side package/metadata 模拟测试。

### 验收标准

- 能给出“只在完整 package 写入并验证成功后才切换”的状态机；
- 能解释失败时如何保持原 bank 可启动；
- 能列出 anti-rollback 所需的版本来源、持久化位置和验证点。

---

## 8. 第 5 模块：PN2.0 Loader/App 分离架构（0.5 天）

### 对应资料

- `README_loader_app_split.md`；
- App `app_manifest.c`、链接脚本与 Loader 部署逻辑。

### 讲解内容

1. 当前两个 IAR 工程：`RZN2L_bsp_xspi0bootx1_loader` 与 `RZN2L_bsp_xspi0bootx1_app`；
2. legacy RZAP 的 entry point、source、destination、size、enable flag；
3. App 实际运行于 BTCM、ATCM、System RAM、SDRAM/HRAM、non-cache/shared buffer 等多块内存；
4. 为什么不能直接把 6526/6527 的“单一连续 RAM image”模型套到 PN2.0；
5. App 真实入口是 `system_init`，不能直接跳转 `main`；
6. ICF、RZAP、Loader 白名单和 package 生成必须同时一致。

### 实验/交付物

- 从真实 App raw binary 解析 RZAP，制作“段 ID -> Flash source -> runtime destination -> size”的表；
- 将该表与 ICF section/map 文件逐项核对；
- 标记每一个段的内存属性、cache 属性及是否允许加密。

---

## 9. 第 6 模块：PN2.0 Secure App 设计与当前实现（1 天）

### 对应资料

- `pn20_rzn2l_secure_app_scheme_comparison_cn.md`；
- `pn20_rzn2l_secure_ssbl_fwupdate_proposal_cn.md`；
- `secure_app_manifest.h`、`secure_app_verify.c`、`secure_app_deploy.c`；
- `tools/make_secure_app_package.py`。

### 讲解内容

1. Scheme B：整体 legacy App body 签名、legacy RZAP 分段部署；
2. Scheme A/RZSM：完整 body 外层认证、RZSM metadata、紧凑段 payload、分段部署；
3. RZSM v2 header（64 bytes）与 entry（96 bytes）的字段、版本和 CRC；
4. `src_offset` 为什么是紧凑 body 中的权威 Flash 源地址；
5. `file_size`、`mem_size`、`cipher_size`、IV、key selection、SHA-256 的安全语义；
6. 段白名单、范围检查、算术溢出、对齐、目标重叠、entry point 限制；
7. outer Code Certificate 的认证范围与“先验签后解析 RZSM”的边界；
8. `SYSTEM_PRG`/`SYSTEM_DATA` 的单段 AES-CBC 加密策略；
9. CBC 尾块：前缀直接写目标地址，最后一个 block 使用 16-byte 临时缓冲，仅复制有效明文；
10. RZSM 与 legacy RZAP 的关系：RZAP 是主机制包输入，运行时以 RZSM entry 为准。

### 实验

1. 使用 `make_secure_app_package.py` 生成未加密 RZSM body；
2. 生成单段加密 `SYSTEM_DATA` 的 RZSM body；
3. 检查 summary：确认 `cipher_size = align_up(file_size, 16)`、各段 `src_offset` 不重叠；
4. 运行 `--inspect-body` 完成结构和范围检查；
5. 对一处 manifest 字段做受控篡改，确认检查器/Loader 拒绝该 package；
6. 走读 Loader 的验证顺序和失败状态码。

### 验收标准

- 能解释真实 `SYSTEM_DATA` 非 16-byte 对齐时为什么不能原地替换；
- 能解释为什么不可将完整 RZSM body 再使用外层 `--enckey` 加密；
- 能说明当前实现没有 full App staging buffer。

---

## 10. 第 7 模块：PN2.0 Secure Firmware Update 与 Dual Bank 设计（0.5 天）

### 讲解内容

1. Secure App package 作为更新最小单元；
2. active/inactive bank 的 package、certificate、metadata、version 绑定；
3. SSBL 的 bank 选择、启动前二次验签、失败回退；
4. update writer 的 chunk 写入、hash/CRC、断电安全和切换顺序；
5. `package_version`、Code Certificate image version、ARBC 的职责划分；
6. recovery path、工厂返修与诊断日志策略；
7. PN2.0 业务停机窗口与 PROFINET 配置/设备身份保持的产品约束。

### 设计作业

每组输出：

- Flash 分区图；
- update 状态机；
- metadata 数据结构草案；
- power-loss 处理表；
- rollback/anti-rollback 规则；
- 安全测试矩阵。

---

## 11. 第 8 模块：量产安全、调试认证与安全验证（0.5 天）

### 对应资料

- Debug Authentication 应用说明；
- Security Features User's Manual 的 OTP、JTAG、SCI/USB Boot、生命周期章节。

### 讲解内容

1. 开发、测试、试产、量产的密钥与权限分离；
2. JTAG Authentication level 1/2、永久禁止的影响；
3. MDD pin、调试连接认证与故障恢复策略；
4. OTP provisioning checklist、双人复核和证据归档；
5. 防篡改、防回滚、降级攻击、地址重定向、IV 重用、未认证解密等威胁；
6. 软件测试、故障注入、升级中断、签名错误、版本错误、bank 损坏的验证方案。

### 验收标准

- 输出一份适用于当前 PN2.0 项目的量产前安全检查表；
- 明确列出必须由授权人员在受控工站执行的 OTP 操作；
- 列出所有可在开发板上安全重复执行的测试。

---

## 12. 建议最终综合实战（可追加 1 天）

### 实战题目

“为当前 PN2.0 Loader/App 分离工程交付一个可审查的 Secure Boot + 单段 AES-CBC 加密 + 双 Bank 更新设计包。”

### 最低交付物

1. 当前 Git commit、工具版本和板卡版本记录；
2. 当前 RZAP 段表、RZSM 段表及运行内存白名单；
3. 可重复执行的主机制包命令和 artifact SHA-256；
4. Secure SSBL 与 Secure App package 的签名、Flash 布局说明；
5. 启动 UART 日志和负向测试日志；
6. Dual Bank/update 状态机、metadata 和回退设计；
7. OTP/JTAG/量产操作边界与风险签核清单。

### 通过标准

- 签名正确的 package 可以被验证、部署并跳转到 App `system_init`；
- 篡改 certificate、manifest、payload、segment destination、IV 或 package version 的样本都被拒绝；
- 对齐不足的加密段能通过 padding 规则正确部署，且不会覆盖段后的运行时内存；
- 更新失败或新 bank 验签失败时，系统能保留/回退至旧的有效 package；
- 不依赖培训中的不可逆 OTP 操作来证明软件流程正确。

---

## 13. 讲师准备清单

- 提前准备一块未 provisioning 的开发板和可恢复 Flash；
- 准备已知测试密钥，禁止在课堂中使用量产 root key；
- 为 6526、6527、PN2.0 三条流程准备独立目录与 artifact 命名规则；
- 准备成功、签名错误、长度错误、地址越界、IV 重用、版本回滚、掉电中断等样例；
- 课前确认 IAR/RSIP/FSP 环境可用；不在课堂中自动修改 IAR linker/debug/project 配置；
- 将所有 Flash 写入和 provisioning 操作安排为单独、可审计的环节。
