

# make_secure_app_package.py 使用说明

## 1. 脚本用途

`make_secure_app_package.py` 用于创建 PN2.0 Secure App 相关文件，包括：

- Legacy RZAP manifest copy 或 Secure Manifest
- Package Body
- Signed Package

脚本会读取 PN2.0 App raw binary，并解析其中已有的 legacy `RZAP app_manifest_t`。

当前默认模式保留方案 A：`--scheme rzsm`。该模式会把原始 manifest 转换为使用 package-relative source offset 的 secure manifest，并为每个有效 segment 计算 SHA-256 [1]。

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

其中 padding 使用 `0xFF` 填充，直到 payload offset 对齐位置 [1]。

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
