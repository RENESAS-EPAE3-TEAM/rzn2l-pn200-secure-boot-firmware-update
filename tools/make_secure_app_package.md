加密段制包需要 Python 包 `pycryptodome`。当前工程的 Python 3.12 环境已经提供该包；若在另一台制包机执行，请先安装：

```powershell
python -m pip install pycryptodome
```


# make_secure_app_package.py 使用说明

## 1. 脚本用途

`make_secure_app_package.py` 用于创建 PN2.0 Secure App 相关文件，包括：

- Legacy RZAP manifest copy 或 Secure Manifest
- Package Body
- Signed Package

脚本会读取 PN2.0 App raw binary，并解析其中已有的 legacy `RZAP app_manifest_t`。

当前默认模式为方案 A：`--scheme rzsm`。该模式生成与 Loader 一致的 RZSM v2 ABI（64-byte header、96-byte entry），使用 package-relative source offset，并为每个有效 segment 计算 SHA-256 [1]。

如果需要走方案 B，可显式指定 `--scheme overall-app`。该模式不生成自定义 RZSM package body，而是把 PN2.0 App raw binary 作为一个连续 overall App body，由 Renesas Key Certificate + Code Certificate 对整体 App 验签。

方案 B 生成的数据结构如下：

```text
Package Body = PN2.0 App raw binary
             = legacy RZAP manifest + App raw image
```

方案 A/RZSM 生成的数据结构如下：

```text
Package Body = Secure Manifest + padding + PN2.0 App raw binary
```

完整 Signed Package 结构如下：

```text
Signed Package = Key Certificate + Code Certificate + Package Body
```

其中 Signed Package 是 SSBL 消费的完整 flash/update 对象 [1]。

---

## 2. 运行环境

该脚本是 Python 3 脚本，文件头为：

```python
#!/usr/bin/env python3
```

推荐使用 Python 3 执行：

```bash
python3 make_secure_app_package.py [参数]
```

Windows 下也可以使用：

```powershell
python make_secure_app_package.py [参数]
```

---

## 3. 最小用法：方案 A/RZSM body

最小必需参数包括：

- `--app-bin`
- `--manifest-out`

示例：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin
```

说明：

| 参数 | 必需 | 说明 |
|---|---|---|
| `--app-bin` | 是 | 输入 PN2.0 App raw binary |
| `--manifest-out` | 是 | 输出 Secure Manifest binary |
| `--body-out` | 否 | 输出方案 A/RZSM package body |

执行后会生成：

```text
secure_manifest.bin
package_body.bin
```

脚本会打印 scheme、segment 数量和 entry point 等信息 [1]。

如需生成方案 B 整体 App body，使用：

```bash
python3 make_secure_app_package.py \
  --scheme overall-app \
  --app-bin app_raw.bin \
  --body-out package_body.bin
```

---

## 4. 生成 Package Body

如果需要生成 Package Body，可以增加 `--body-out` 参数：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin
```

生成的 `package_body.bin` 结构为：

```text
Secure Manifest + padding + PN2.0 App raw binary
```

RZSM v2 可选将一个 `SYSTEM_PRG` 或 `SYSTEM_DATA` 段替换为 AES-128-CBC 密文。其余 App payload 保持原位置和原始字节，不会使用完整 RAM staging buffer。

其中 padding 使用 `0xFF` 填充，直到 payload offset 对齐位置 [1]。

## 4.1 生成一个 ckey0 AES-CBC 加密段

`--aes-key` 是与已配置 OTP `ckey0` 相同的 16-byte AES-128 host-side key 文件。它只在制包主机上使用，不能复制到 Loader、App 或 Flash package。`SYSTEM_PRG` 和 `SYSTEM_DATA` 是唯一允许的初始加密目标。

被加密的原始段长度必须是非零的 16-byte 整数倍。当前 Loader 不支持 PKCS#7 padding，因此长度不满足时脚本会停止，不生成不可部署的包。

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

每次运行都会为加密段生成新的随机 16-byte IV，并写入 authenticated RZSM entry；`cipher_size` 与 plaintext `file_size` 相等。脚本在输出前会以同一 host key 解密密文并与原始 App 段逐字节比较。

## 4.2 使用 6526/6527 工具签名 RZSM body

`rzsm_v2_manifest.bin` 是 body 的首部副本，供检查 RZSM ABI；它不是 Renesas 工具的签名输入。必须把完整的 `rzsm_v2_body.bin` 传给两个官方工具：该文件由 `RZSM manifest + padding + App payload` 组成，已包含选中段的 AES-CBC 密文。

生成器会将 body 用 `0x00` 补齐为 16-byte 倍数，并把补齐后的完整长度写入 `RZSM package_size`。因此官方工具不会再改变 body 长度，且 Code Certificate 的 `img_len` 可与 Loader 的 Scheme A 校验严格匹配。

在 6526 的 `pn20_secure_ssbl` 工具目录中执行。6527 使用同一 `parameter_generator.py userapp` 与 `secureboot_utility.py image` 机制，参数相同：

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

不要给这条外层 `image` 命令加 `--enckey`。`--enckey` 会把整个 RZSM body 再加密，导致 Secure SSBL 在认证后无法读取 RZSM header；段级 AES-CBC 已由本脚本完成。`--keyselect ckey0` 也不需要用于外层明文 body；仅在使用外层 image encryption 时才会写入 Code Certificate 的 Image Cipher Info。

最后可用本脚本确认完整包的外层长度和 RZSM body：

```powershell
python .\make_secure_app_package.py `
  --scheme rzsm `
  --inspect-signed-package $signed `
  --summary-out .\pn20_rzsm_signed_summary.json
```

---

## 5. 使用旧参数 `--package-out`

脚本中 `--package-out` 是 `--body-out` 的 legacy alias，即旧版别名 [1]。

下面两种写法等价：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin
```

或：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --package-out package_body.bin
```

注意：

如果同时指定 `--body-out` 和 `--package-out`，并且两个路径不同，脚本会报错 [1]。

---

## 6. 生成完整 Signed Package

如果需要生成完整 signed package，需要同时提供以下三个参数：

- `--key-cert`
- `--code-cert`
- `--signed-package-out`

示例：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --key-cert key_cert.bin \
  --code-cert code_cert.bin \
  --signed-package-out signed_package.bin
```

输出文件包括：

```text
secure_manifest.bin
package_body.bin
signed_package.bin
```

`signed_package.bin` 的结构为：

```text
Key Certificate + Code Certificate + Package Body
```

脚本要求 `--key-cert`、`--code-cert` 和 `--signed-package-out` 必须一起提供，否则会报错 [1]。

---

## 7. 证书大小要求

脚本中固定定义了证书大小：

| 证书 | 大小 |
|---|---:|
| Key Certificate | `0xE0` bytes |
| Code Certificate | `0x120` bytes |

如果输入证书大小不匹配，脚本会报错 [1]。

---

## 8. Code Certificate 布局检查

默认情况下，脚本会检查 Code Certificate 中的布局信息是否与生成的 body 匹配，包括：

- `dest_addr`
- `img_size`

其中 `dest_addr` 需要等于：

```text
package_base + KEY_CERT_SIZE + CODE_CERT_SIZE
```

如果不匹配，脚本会报错 [1]。

如果不想执行该检查，可以添加：

```bash
--no-cert-layout-check
```

示例：

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

## 9. 生成 JSON Summary

可以使用 `--summary-out` 生成 JSON 摘要文件：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --summary-out summary.json
```

如果同时生成 signed package：

```bash
python3 make_secure_app_package.py \
  --app-bin app_raw.bin \
  --manifest-out secure_manifest.bin \
  --body-out package_body.bin \
  --key-cert key_cert.bin \
  --code-cert code_cert.bin \
  --signed-package-out signed_package.bin \
  --summary-out summary.json
