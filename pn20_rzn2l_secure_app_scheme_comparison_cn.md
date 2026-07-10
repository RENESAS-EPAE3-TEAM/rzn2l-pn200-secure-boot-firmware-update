# PN2.0 RZ/N2L Secure App 打包方案 A / 方案 B 对比

## 1. 背景和目标

当前 PN2.0 App 不是单一连续加载后直接运行的简单镜像，而是通过 App 侧发布的 legacy `RZAP` manifest 描述多个搬移段：

```text
LDR_PRG_RBLOCK                -> LDR_PRG_WBLOCK
LDR_DATA_RBLOCK               -> LDR_DATA_WBLOCK
VECTOR_RBLOCK                 -> VECTOR_WBLOCK
USER_PRG_RBLOCK               -> USER_PRG_WBLOCK
USER_DATA_RBLOCK              -> USER_DATA_WBLOCK
SYSTEM_PRG_RBLOCK             -> SYSTEM_PRG_WBLOCK
SYSTEM_DATA_RBLOCK            -> SYSTEM_DATA_WBLOCK
NONCACHE_RBLOCK               -> NONCACHE_WBLOCK
SHARED_NONCACHE_BUFFER_RBLOCK -> SHARED_NONCACHE_BUFFER_WBLOCK
```

Secure SSBL 的目标是：

1. 使用 Renesas RSIP / Secure Boot 机制确认 App 没有被篡改。
2. 验证通过后，按 PN2.0 原有多段搬移模型启动 App。
3. 后续支持 OTA、多 bank、AES-128-CBC 加密、版本回滚控制和更精细的部署策略。

基于这个目标，目前讨论两个方向：

- **方案 A：RZSM 自定义 secure manifest 方案**
- **方案 B：整体 App 作为 Renesas 原生 secure image 的方案**

本文结合 Renesas `R01UH1017EJ` Security Features 资料重新梳理两种方案的优劣势和适用场景。

## 2. Renesas 原生 secure boot 机制要点

Renesas 文档中的 secure boot / secure update 模型主要围绕以下对象展开：

```text
Key Certificate
Code Certificate
Image
```

Code Certificate header 中包含：

```text
Flags
Load Addr
Dest Addr
Image Size
Image Version
Build Number
```

其中 `Flags bit0` 用于表示 image 是否启用加密/解密。

加密相关参数通过 TLV 描述，例如：

```text
IV TLV
Image cipher info TLV
  - Key selection
  - IV selection
  - Destination address
```

文档明确说明：

- 签名算法：ECDSA NIST P-256
- Hash 算法：SHA-256
- 加密/解密算法：AES-128-CBC
- Code Certificate 的 `Image Version` 可用于 anti-rollback
- OTP 中有 ARBC anti-rollback counter 区域
- RSIP driver 提供 `R_RSIP_SB_ManifestVerify()` 用于 manifest verification and code decryption

因此，如果 App 能被组织成一个 Renesas 原生 secure image，方案 B 可以高度复用官方证书格式、签名流程、加密流程和工具链。

## 3. 方案定义

### 3.1 方案 A：RZSM 自定义 secure manifest

方案 A 使用自定义 RZSM manifest 描述 PN2.0 App 的多段部署关系。

典型布局：

```text
App secure package
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+
| RZSM manifest               |
+-----------------------------+
| segment payload 0           |
+-----------------------------+
| segment payload 1           |
+-----------------------------+
| ...                         |
+-----------------------------+
```

Code Certificate 验签覆盖：

```text
RZSM manifest + all segment payloads
```

RZSM entry 可以描述：

```text
segment_id
flags
src_offset
dst_addr
file_size
mem_size
load_attr
hash
```

后续可扩展：

```text
cipher_size
plain_size
iv
key_id
segment_version
rollback_domain
bank_policy
compression
zero_fill
```

SSBL 流程：

```text
1. R_RSIP_SB_ManifestVerify() 验证 package body
2. 读取 RZSM manifest
3. 校验 header / package range / segment range / dst whitelist / entry point
4. 按 segment 搬移或解密
5. cache clean/invalidate
6. jump entry_point
```

### 3.2 方案 B：整体 App 作为 Renesas secure image

方案 B 保留 PN2.0 legacy `RZAP` manifest，把它和 App raw image 作为一个整体 App，由 Renesas 原生 Code Certificate 直接签名或加密。

典型布局：

```text
App secure image
+-----------------------------+
| Key Certificate             |
+-----------------------------+
| Code Certificate            |
+-----------------------------+
| overall App                 |
|   - legacy RZAP manifest    |
|   - App raw image           |
+-----------------------------+
```

Code Certificate 的 image 指向：

```text
overall App = legacy manifest + App image
```

SSBL 流程：

```text
1. R_RSIP_SB_ManifestVerify() 验证 overall App
2. 验证成功后读取 overall App 内的 legacy RZAP manifest
3. 校验 manifest magic / entry_count / entry_point / dst whitelist
4. 按 legacy manifest 搬移
5. cache clean/invalidate
6. jump entry_point
```

如果启用 Renesas 原生 AES-128-CBC 加密，则整体 App 是一个连续 encrypted image。

## 4. 核心差异

### 4.1 信任边界

方案 A：

```text
Code Certificate 认证 RZSM manifest + segment payloads
SSBL 信任 RZSM，但仍做白名单和边界检查
```

方案 B：

```text
Code Certificate 认证 overall App
SSBL 验证通过后读取 legacy RZAP manifest
SSBL 仍要做白名单和边界检查
```

两者都必须遵守同一原则：

```text
在 R_RSIP_SB_ManifestVerify() 成功之前，不读取、不信任 manifest 中的地址、长度或 entry point。
```

### 4.2 部署模型

方案 A 天然面向多段部署：

```text
segment 0 -> dst0
segment 1 -> dst1
segment 2 -> dst2
```

方案 B 天然面向单个连续 image：

```text
overall App -> one verified/decrypted image
```

PN2.0 App 本身是多段搬移模型，因此方案 B 仍然需要 legacy manifest 承担多段部署语义。

### 4.3 地址表达

方案 A 推荐使用 package-relative offset：

```text
src_offset = payload 在 package body 内的偏移
```

优点：

- 与 App 实际烧录 bank 无关
- bank A / bank B 可共用同一套偏移模型
- 不依赖 App link base 等于 flash physical address

方案 B 的 legacy manifest 通常使用 link-time absolute source address：

```text
src = __section_begin("XXX_RBLOCK")
```

如果 App 被烧录到不同 bank，可能出现：

```text
manifest->src != actual flash source address
```

此时必须：

1. 每个 bank 单独链接/生成 manifest；或
2. SSBL 根据当前 bank 对 `src` 做重定位。

### 4.4 与 Renesas 官方工具/例程的复用度

方案 A：

- 可以复用 Key Certificate / Code Certificate / `R_RSIP_SB_ManifestVerify()`
- 但 RZSM manifest 生成、解析、策略校验需要自定义
- 与官方 sample loader 的 image 模型有一定偏离

方案 B：

- 高度贴合官方 secure boot image 模型
- 更容易复用 secureboot utility
- 更容易复用 Code Certificate 的 `Image Version` anti-rollback
- 更容易复用官方 AES-128-CBC image encryption
- 更接近 Secure Update sample 的整体 image 验证方式

## 5. AES-128-CBC 加密场景对比

### 5.1 方案 B：整体 App 加密

方案 B 如果完全使用 Renesas 原生 image encryption，流程是：

```text
plain overall App = legacy manifest + App image
encrypted overall App = AES-128-CBC(plain overall App)

SSBL:
1. 验证 encrypted overall App
2. 解密 encrypted overall App
3. 得到连续 decrypted overall App
4. 从 decrypted overall App 中读取 legacy manifest
5. 按 manifest 搬移各段
```

优点：

- 最贴近 Renesas 原生 secure image 设计
- Code Certificate 的加密 flag、IV TLV、Image cipher info TLV 可直接使用
- 签名目标可以覆盖 encrypted image
- 工具链和参考例程复用度最高

主要问题：

```text
必须先得到完整 decrypted overall App，才能读取 legacy manifest。
```

<span style="color:red">这意味着方案 B 在整体加密场景下，需要一个足够大的连续 RAM/SDRAM staging buffer。</span>

<span style="color:red">对 PN2.0 App 来说，如果 App 体积较大，这个 staging buffer 可能很难安排，是方案 B 的关键限制。</span>

如果为了避免大 buffer，把 manifest 保持明文，只加密 payload，则方案 B 会变成半自定义布局，复杂度会向方案 A 靠拢。

### 5.2 方案 A：分段加密

方案 A 可以让 RZSM entry 描述每个 segment 的加密信息：

```text
segment_id
flags = encrypted
src_offset
cipher_size
plain_size
dst_addr
iv
key_id
```

SSBL 流程：

```text
1. R_RSIP_SB_ManifestVerify() 认证整个 package body
2. 解析 RZSM
3. 对每个 encrypted segment：
   - 从 flash package_body + src_offset 读取密文
   - AES-CBC 解密
   - 输出到 dst_addr
4. 对每个 plain segment：
   - 直接 copy 到 dst_addr
5. jump entry_point
```

优点：

- 不需要完整 App 级别的大缓存
- 可以从 Flash 地址逐段解密到目标 RAM 地址
- 只需要小块工作缓冲，甚至在 API 支持时可直接 input flash / output RAM
- 适合 PN2.0 多段搬移结构
- 支持不同 segment 使用不同策略

限制：

- SSBL 代码复杂
- 打包工具复杂
- 需要明确 AES-CBC padding、IV、key selection、plain/cipher size
- 如果不使用 Renesas 原生 image decryption，而是手动调用 AES API，则实现责任更大

### 5.3 AES-CBC 下的关键结论

如果目标是：

```text
整个 App 作为一个连续 image 加密和解密
```

方案 B 更优。

如果目标是：

```text
多个 segment 分别加密/明文，直接部署到最终运行地址，不需要大 RAM staging buffer
```

方案 A 更优。

## 6. 多 bank / OTA / rollback 对比

### 6.1 多 bank

方案 A：

- 使用 `src_offset`，天然适配 bank A / bank B
- package 放到不同 bank 时，不需要修改 segment source offset
- SSBL 只需要知道当前 package body base

方案 B：

- legacy manifest 中 `src` 通常是 absolute address
- 多 bank 时要么重链接，要么运行时重定位
- 如果忘记重定位，可能从错误 flash 地址搬移

结论：

```text
多 bank 场景下，方案 A 更自然。
```

### 6.2 OTA 策略

方案 A：

- RZSM header 可放 package-level policy
- RZSM entry 可放 segment-level policy
- 适合表达分段更新、可选资源段、不同段不同权限

方案 B：

- 更适合整体 App 更新
- 分段策略需要扩展 legacy manifest 或引入额外约定
- 策略复杂后会逐渐变成另一个自定义 manifest

结论：

```text
简单整包 OTA：方案 B 更简单。
复杂分段 OTA：方案 A 更清晰。
```

### 6.3 Anti-rollback

方案 A：

- 可在 RZSM header 中定义 `package_version`、`rollback_counter_id` 等字段
- 灵活，但需要自定义规则并实现 OTP ARBC 比较/更新策略

方案 B：

- Code Certificate header 已有 `Image Version`
- Renesas 文档明确该字段可用于 anti-rollback
- 更贴近官方 secure boot 设计

结论：

```text
使用官方单 image rollback：方案 B 更顺。
多个模块/多个 rollback domain：方案 A 更灵活。
```

## 7. 安全性对比

### 7.1 共同安全要求

无论方案 A 或方案 B，都必须满足：

1. manifest 必须位于签名覆盖范围内。
2. 验签成功前不能信任 manifest。
3. 验签成功后仍要校验目标地址白名单。
4. 必须校验 source range、destination range、size overflow、entry point。
5. 必须处理失败路径，不能继续启动未验证 App。
6. 如果启用加密，CBC 只提供保密性，完整性必须依赖签名/认证。
7. 生产阶段应考虑 OTP 中 root public key hash、secure boot common key、SBEN、ARBC、JTAG authentication 等配置。

### 7.2 方案 A 安全风险

- 自定义 manifest 格式可能设计不完整
- 自定义打包工具可能漏签关键字段
- 自定义 AES-CBC 解密流程容易引入 padding/IV/key handling 错误
- 与官方参考流程偏离后，验证成本增加

### 7.3 方案 B 安全风险

- legacy manifest 表达能力有限
- multi-bank 下 absolute `src` 地址可能错误
- <span style="color:red">整体加密需要 decrypted image staging buffer，buffer 管理不当可能留下明文残留</span>
- 若 manifest 明文、payload 加密，需要额外定义并验证 payload 布局
- 如果过度扩展 legacy manifest，兼容性和维护复杂度会上升

## 8. 优劣势汇总

| 维度 | 方案 A：RZSM 自定义 manifest | 方案 B：整体 App secure image |
|---|---|---|
| Renesas 官方工具复用 | 中等 | 高 |
| 官方 AES-128-CBC image encryption 复用 | 较弱或需额外设计 | 强 |
| PN2.0 多段搬移适配 | 强 | 依赖 legacy manifest |
| 是否需要完整 decrypted App 大缓存 | 不需要 | <span style="color:red">整体加密时通常需要</span> |
| 分段加密 | 强 | 弱 |
| 多 bank | 强 | 需要重定位或重链接 |
| 简单整包 OTA | 可以，但较复杂 | 强 |
| 复杂分段 OTA | 强 | 弱 |
| Anti-rollback | 灵活，自定义 | 可复用 Code Certificate Image Version |
| manifest/policy 扩展 | 强 | 受 legacy manifest 限制 |
| 实现复杂度 | 高 | 低到中等 |
| 与参考例程一致性 | 中等 | 高 |
| 长期产品化灵活性 | 强 | 中等 |

## 9. 推荐路线

### 9.1 短期调试推荐

短期为了快速打通 Secure SSBL 链路，建议优先使用方案 B 做验证：

```text
Key Certificate + Code Certificate + overall App
```

其中：

```text
overall App = legacy RZAP manifest + App raw image
```

目标：

- 验证 RSIP manifest verify
- 验证 Renesas certificate / signature 流程
- 验证 App 搬移和启动
- 后续再验证官方 AES-128-CBC image encryption

### 9.2 长期产品化推荐

如果目标包含：

```text
多 bank
复杂 OTA
分段加密
不同 segment 不同策略
不使用大 RAM staging buffer
多个 rollback domain
```

建议保留并继续演进方案 A。

方案 A 更适合作为 PN2.0 的最终 secure package 格式。

### 9.3 折中路线

可以采用两阶段策略：

第一阶段：方案 B

```text
整体 App 签名/可选整体加密
快速复用 Renesas 工具和参考例程
打通 secure chain
```

第二阶段：方案 A

```text
RZSM manifest + segment payloads
支持多 bank、分段加密、复杂 OTA policy
```

也可以保留外层 Renesas Code Certificate 不变，内部 package body 使用 RZSM：

```text
Key Certificate
Code Certificate
RZSM manifest
segment payloads
```

这样外层仍由 Renesas RSIP 验证，内层由 PN2.0 Secure SSBL 执行精细部署策略。

## 10. 最终结论

方案 B 的核心价值是：

```text
最大程度复用 Renesas Secure Boot / Secure Update 官方模型。
```

它适合：

```text
整包签名
整包 AES-128-CBC 加密
单 bank 或地址固定
简单 OTA
快速调通 secure boot chain
```

方案 A 的核心价值是：

```text
最大程度适配 PN2.0 多段搬移和未来复杂 OTA 策略。
```

它适合：

```text
多 bank
分段加密
无大 RAM staging buffer
复杂部署策略
长期产品化扩展
```

因此当前建议是：

```text
短期联调优先方案 B。
长期产品化保留并演进方案 A。
```

如果 AES-128-CBC 只是整体 App 加密，方案 B 更自然。<span style="color:red">但需要重点评估是否有足够的连续 RAM/SDRAM staging buffer 存放完整 decrypted overall App。</span>

如果 AES-128-CBC 要做到按 segment 解密并直接部署到最终 RAM 地址，方案 A 更合适。
