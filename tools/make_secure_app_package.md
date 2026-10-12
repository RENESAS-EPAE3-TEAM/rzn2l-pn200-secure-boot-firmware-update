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
Package Body = Secure Manifest + padding + ordered enabled segment payloads
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
Secure Manifest + padding + ordered enabled segment payloads
```

RZSM v2 从输入 App 的 legacy RZAP 提取每个启用且长度非零的段，并按 RZAP entry 顺序紧凑写入 body。每个 RZSM entry 的 `src_offset` 是该段在新 body 中的权威位置；legacy RZAP 仅作为制包输入，不会保留在 RZSM body。可选加密一个已知段，或通过 `--encrypt-all` 加密全部有效段，不会使用完整 RAM staging buffer。

其中 padding 使用 `0xFF` 填充，直到 payload offset 对齐位置 [1]。

## 4.1 生成一个 ckey0 AES-CBC 加密段

`--aes-key` 是与已配置 OTP `ckey0` 相同的 16-byte AES-128 host-side key 文件。它只在制包主机上使用，不能复制到 Loader、App 或 Flash package。`--encrypt-segment` 接受已知段名称或 ID，原 `SYSTEM_PRG`/`SYSTEM_DATA` 单段命令继续有效；新增段需使用第 11 节调整后的 Loader。

加密段使用零字节补齐到下一个 16-byte AES block，且不使用 PKCS#7。entry 的 `file_size` 始终是原始明文长度，`cipher_size = align_up(file_size, 16)`；Loader 只写回 `file_size` 字节，因此 padding 不会覆盖相邻运行时内存。

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

每次运行都会为加密段生成新的随机 16-byte IV，并写入 authenticated RZSM entry。脚本在输出前会以同一 host key 解密密文，逐字节比较原始 App 段，并验证补齐部分均为零。

## 4.2 使用 6526/6527 工具签名 RZSM body

`rzsm_v2_manifest.bin` 是 body 的首部副本，供检查 RZSM ABI；它不是 Renesas 工具的签名输入。必须把完整的 `rzsm_v2_body.bin` 传给两个官方工具：该文件由 `RZSM manifest + padding + ordered segment payloads` 组成，已包含选中段的 AES-CBC 密文。

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
```

## 10. Scheme A: 加密 Secure SSBL 签名、烧录与单段加密验收

本节用于已经手动编译完成的 Loader Debug/Scheme A。
“单加密 SYSTEM_DATA”指完整 App 包仍保留全部 7 个启用段，仅 SYSTEM_DATA 加密；不是只包含 SYSTEM_DATA 的单段包。缺少 LDR_PRG、VECTOR 等必需段的包不能正常启动 App。

### 10.1 本次输入和安全前提

- Loader Debug 使用 `SSBL_CFG_SECURE_APP_SCHEME_RZSM`、`USE_HRAM`，验签、部署和 UART 日志均启用。
- 使用最新 Debug Loader `.bin`；本次已签名输入为 2026-10-09 16:16:34 的 51,924-byte 文件。
- 现有 `rzsm_v2_body.bin` 为 1,569,520 bytes，已通过离线段字节、CBC 和外层签名验证。
- 2026-10-09 已按作者指定的 `work_secureboot_rzn2l\secureboot_common_key.bin` 覆盖 host key 并重制 App 和 SSBL；不再使用 6526 默认 `Key Data` 的 AES key。Root/Image 签名密钥仍沿用 PN2.0 验证文档指定的 `pn20_secure_ssbl` 目录。
- SSBL 整镜像加密，由 BootROM 验签和解密后启动；App 外层不加密，由 SSBL 验签后仅解密 SYSTEM_DATA。两个加密层彼此独立，但本次使用同一 ckey0。
- 板卡已经配置对应 Root Public Key 和 OTP ckey0；host AES key 必须与该 ckey0 一致。主机回环解密不能证明 OTP key 一致。
- 本流程只更新 Flash，不写 OTP，不执行 `setboot --enable`，不禁用 SCI/USB Boot。不确定板卡 provisioning 状态时停止。
- 烧录会替换现有 Loader 和 App。保留已验证的旧镜像以备恢复，不覆盖其他 Flash 分区。

### 10.2 定义路径

打开一个 PowerShell 会话，后续命令在同一会话逐条执行。每条 Python 命令执行后，若 `$LASTEXITCODE` 非零，停止并处理错误。

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

官方工具的 `.log` 和 `.pubkey` 输出到命令工作目录，不是 `-o` 所在目录。本节固定工作目录为工程根目录。

host AES key 必须从指定的自定义实验目录复制，不能仅凭同名文件选择默认例程 key；不输出密钥内容：

```powershell
if ([System.IO.File]::ReadAllBytes("$keyDir\secureboot_common_key.bin").Length -ne 16) { throw "Expected 16-byte AES key" }
[System.IO.File]::Copy("$keyDir\secureboot_common_key.bin", $aesKey, $true)
```

### 10.3 Loader 签名的完整复现命令

本次已生成下列 SSBL 文件。如果 Loader 未再次编译，可以直接使用；如再次编译，必须重新执行填充、参数生成和签名，不能复用旧 SSBL。

按本次实际长度补齐到 512-byte 倍数，使用 `0xFF`，不要写死长度：

```powershell
$rawBytes = [System.IO.File]::ReadAllBytes($raw)
$paddedSize = [int]([Math]::Ceiling($rawBytes.Length / 512.0) * 512)
$paddedBytes = [byte[]]::new($paddedSize)
for ($index = 0; $index -lt $paddedSize; $index++) { $paddedBytes[$index] = 0xFF }
[Array]::Copy($rawBytes, $paddedBytes, $rawBytes.Length)
[System.IO.File]::WriteAllBytes($padded, $paddedBytes)
"Raw={0} Padded={1}" -f $rawBytes.Length, $paddedSize
```

本次预期：Raw=51924，Padded=52224，填充 300 bytes。

```powershell
python "$toolDir\parameter_generator.py" loader `
  --mpu rzn2l --target_cpu cr52 `
  --src_addr 60000050 --dest_addr 00102000 `
  --mode xspi0 --secureboot -i $padded -o $loaderParam
```

工具提示实际 Loader 程序地址应为 `dest_addr + 0x800 = 0x00102800`，这是正常提示；必须与本次 Loader 链接布局一致。

```powershell
python "$toolDir\secureboot_utility.py" image `
  -i $padded -o $ssbl --param $loaderParam `
  --rkey "$toolDir\rootkey-pair.pem" `
  --ikey "$toolDir\loaderkey-pair.pem" `
  --enckey $aesKey --keyselect ckey0 `
  --sign_target cert-img --ver 0
```

SSBL 使用 `--enckey` 开启 AES 加密，并以默认 `enc-sign` 顺序对证书和密文签名。单独指定 `--keyselect ckey0` 不会开启加密。本次完整 SSBL 长度为 `52224 + 0x800 = 54272` bytes。离线验证已确认 SSBL 解密结果与当前 Debug Loader 及全部 `0xFF` 填充逐字节相同。

### 10.4 完整 RZSM App 外层签名与烧录前检查

现有 App 包已使用新的 host key 重制并签名。如果仅重编译 Loader，没有改变 App 或 host key，不必重制 App body。更换 host key 时必须重新生成 SYSTEM_DATA 密文及 manifest，然后重新签名，不能只对旧 body 重签名。完整复现命令如下：

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

不传 `--enckey`。SYSTEM_DATA 已在 body 内加密，外层必须保持可直接读取的 RZSM。

```powershell
python "$tools\make_secure_app_package.py" --scheme rzsm `
  --inspect-signed-package $appPackage `
  --summary-out "$tools\pn20_rzsm_signed_summary.json"
```

应得到 body=1569520、segments=7、entry=0x0011A000。此命令是结构检查，不是密码学验签。查看 summary，确保只有 id=6 的 flags 为 `0x0000000B`，其他段为 `0x00000009`。

```powershell
Get-Content "$workspace\pn20_scheme_a_secure_ssbl_xspi0.log"
Get-Content "$workspace\pn20_secure_rzsm_app_package.log"
Get-FileHash "$workspace\pn20_scheme_a_secure_ssbl_xspi0.pubkey", "$workspace\pn20_secure_rzsm_app_package.pubkey"
Get-Item $loaderParam, $ssbl, $appPackage
```

两个日志必须显示 `cert-img`；SSBL 日志必须显示 `Image encryption: Enable` 和 `Key select: ckey0`，App 日志必须显示 `Image encryption: Disable`。两个 `.pubkey` 文件必须相同。它们保存的是 Root Public Key hash，不能仅凭互相相同就证明它们匹配 OTP；须沿用已验证的板卡密钥链。

### 10.5 SCI 下载 Device Setup Program

先关闭所有占用烧录 COM 口的串口窗口。按板卡手册切换 SCI Boot Mode，复位/重新上电后执行。不要凭历史记录假定 COM 号或拨码位置。

```powershell
$COM = "COM3" # 必须换成设备管理器中实际端口
$setupSrec = "$toolDir\..\rzn2l\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec"
python "$toolDir\secure_device_setup.py" start `
  --port $COM --boot_mode sci -i $setupSrec
```

只有 `start` 成功才继续。若提示 `Failed to send the program file.`，检查启动模式、端口占用和复位时序，不要进行 provisioning。

### 10.6 按顺序写入三个 Flash 对象

逐条执行，每条成功后才继续。地址是 8 位十六进制，不加 `0x`。

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

只烧录完整签名包，不烧录独立 RZSM manifest/body，不将 App parameter 单独写入 Flash。不将 SSBL 写到 `60000000`。Flash 写入成功只是第一道门槛，不代表 BootROM/SSBL 验证成功。

### 10.7 正常启动与 UART 验收

切回板卡正常 xSPI Startup Mode。启动串口日志捕获，Loader UART 为 115200 baud；使用对应调试 UART，按工程设置采用 8N1、无流控。重新上电，避免仅在 IAR 中执行下载后复位代替真实 Flash 启动。

日志中的其他信息允许穿插，以下关键标志必须按顺序出现：

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

大小写/数值前导零以实际 printf 输出为准。不得出现 `[FAIL]`、`RSIP CLOSE FAIL` 或 deploy disabled。
当前 `jump app` 日志位于 UART 关闭、RSIP 关闭和 `app_prg()` 调用之前，因此它只证明“准备跳转”。还必须观察 App 原有启动日志/业务运行，或在 App `system_init`、`main` 设置断点确认实际到达。

### 10.8 SYSTEM_DATA 解密运行值验收

仅凭 DEPLOY OK 不能证明 OTP ckey0 与 host key 相同，必须检查明文字节。推荐分两次启动：第一次捕获完整 UART/业务启动，第二次用调试器检查 App 启动前的内存。

1. IAR 使用现有合适的 attach/debug 会话，只加载当前 Loader `.out` 符号，不下载程序、不执行 Flash loader，不让调试器初始化 RAM 或重写 App。若当前调试配置会下载/初始化，先停止并单独确认配置，不直接使用 Download and Debug。
2. 在 `hal_entry.c` 的 `app_prg();` 调用前设置硬件断点。复位运行并停住，确认部署成功、RSIP 关闭无失败，App 尚未开始修改 SYSTEM_DATA。使用硬件断点避免软件断点修改已部署代码。
3. 此时 Loader 已执行缓存 clean/invalidate 和 DSB/ISB。在 Memory 窗口检查 SYSTEM_DATA `[0x64000000,0x6403EF5C)`；实际地址/长度以本次 summary 为准。
4. 将这段内存保存为纯 binary，共 257884 bytes，保存为 `tools\system_data_runtime.bin`。不要包含地址头、ASCII 文本或最后 4-byte CBC padding。若优化后的断点定位不可靠，在部署完成且缓存维护之后的等效指令地址暂停。
5. 运行下面的完整段字节比较。它根据当前 RZAP 的 id=6 自动提取参考明文，不依赖写死的原始 Flash 偏移：

```powershell
python -c "from pathlib import Path; import struct,hashlib; t=Path(r'$tools'); a=(t/'RZN2L_bsp_xspi0bootx1_app.bin').read_bytes(); h=struct.unpack_from('<16I',(t/'rzsm_v2_body.bin').read_bytes()); r=h[12]; assert struct.unpack_from('<I',a,r)[0]==0x50415A52; src,dst,size,flags=struct.unpack_from('<4I',a,r+16+6*16); off=src-h[11]; assert flags&1 and size>0 and 0<=off and off+size<=len(a); expected=a[off:off+size]; actual=(t/'system_data_runtime.bin').read_bytes(); assert len(actual)==size, 'runtime dump length mismatch'; assert actual==expected, 'SYSTEM_DATA plaintext mismatch'; print('SYSTEM_DATA runtime bytes PASS',size,'SHA256',hashlib.sha256(actual).hexdigest())"
```

当前 SYSTEM_DATA 结束地址也是 SYSTEM_PRG 起点 `0x6403EF5C`。检查 SYSTEM_PRG 最前 16 bytes 与原 App 对应段相同，可额外证明尾部 padding 没有越界覆盖：

本次原始 App 的 SYSTEM_DATA 参考值如下；重新编译 App 后不得沿用，应重新计算：

```text
SHA-256: e408275d85f4636820b90b9acfe75bf42a9bd9734c206389fcae7bafc5c249cf
0x64000000 first16: 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0x6403EF4C last16:  44 00 65 00 00 00 00 00 ff ff ff ff 00 00 00 00
0x6403EF5C SYSTEM_PRG first16: 00 20 01 49 08 60 70 47 ac e8 03 64 38 b5 04 00
```

```powershell
python -c "from pathlib import Path; import struct; t=Path(r'$tools'); a=(t/'RZN2L_bsp_xspi0bootx1_app.bin').read_bytes(); h=struct.unpack_from('<16I',(t/'rzsm_v2_body.bin').read_bytes()); src,dst,size,flags=struct.unpack_from('<4I',a,h[12]+16+5*16); print('SYSTEM_PRG start',hex(dst),'expected first16',a[src-h[11]:src-h[11]+16].hex(' '))"
```

继续运行，确认 App 实际到达 `system_init`/`main` 并正常工作。如果只检查首尾少量字节，这只是抽样检查，不能替代完整段比较。如果系统禁用安全调试附加，应停止此方法，另行审批添加运行时哈希校验日志，不能未经验证直接宣称运行值正确。

### 10.9 最终通过条件和失败定位

必须同时满足：BootROM 从 Flash 启动本次 SSBL；外层验签成功；RZSM 7 段部署成功；RSIP 关闭成功并真正进入 App；SYSTEM_DATA 全段运行内存与原 App 完全相同；App 正常运行。请记录 UART 日志和内存比较结果。

| 失败位置 | 下一步 |
|---|---|
| 没有 SSBL UART | 检查正常启动模式、三个写入地址、串口、BootROM 认证及 SSBL 链接入口；不要重新写 OTP |
| VERIFY FAIL | 检查完整包、Root key/OTP、Flash 内容和错误码 |
| 仍打印 RZAP | 使用了旧 Scheme B Loader；重签并写入实际 Scheme A Debug 编译产物 |
| SEG6 CRYPTO/DEPLOY FAIL | 检查 CIP 错误码、RSIP 配置、地址对齐、HRAM 初始化、ckey0 可用性 |
| DEPLOY OK 但明文不同 | 优先核对 OTP ckey0 与 host key、CIP IV 字节序及尾块链式 IV；停止跳转测试 |
| jump 日志后卡住 | jump 日志不证明真正进入 App；检查 RSIP close、入口断点、App startup 和部署数据 |

本节的签名与离线检查可在主机完成；没有板端 UART/内存证据前，不将“指导步骤已生成”报告为“板级验证已成功”。

## 11. 全部有效段加密调整记录（2026-10-12）

作者已确认单段 SYSTEM_DATA 加密部署成功，并于 2026-10-12 确认全部七个有效段加密方案也已通过上板验证，App 在 RZN2L RSK socket board 上正常运行。板级结论来自作者反馈。

### 11.1 源码修改与兼容性

- `make_secure_app_package.py` 新增 `--encrypt-all`，选择 RZAP 中启用且长度非零的全部段，包括 ID 0。与 `--encrypt-segment` 互斥，两者均要求 `--aes-key`；inspect 模式和 `overall-app` 模式拒绝加密选项。
- 原 `--encrypt-segment SYSTEM_DATA`、`SYSTEM_PRG` 及未加密命令保持有效。单段选项扩展为所有已知、启用且非空的段，便于逐段排查；生成函数新增的 `encrypt_all=False` 参数位于末尾，兼容已有位置参数调用。
- 每段独立零补齐到 16-byte 倍数，使用现有 ckey0，并生成非零且不重复的随机 IV。结构检查新增加密目标四字节对齐、已知 ID 和重复 IV 检查；完整离线验证逐段比较解密明文、SHA-256 和零 padding。
- `secure_app_deploy.c` 的 `encrypted_segment_destination_allowed()` 改为复用现有 `segment_destination_allowed()`。支持所有已知段类型，但保留按段 ID 划分的目标地址白名单、范围/重叠/对齐检查、CBC metadata 检查和 IV 检查；尾块只写回有效明文的算法不变。
- RZSM v2 header/entry ABI、Flash 地址、App 入口和原始 App 内存布局均不改变；FSP/RSIP 驱动、IAR 工程和链接配置不改动。检查现有 `rsip_validate_addr()` 确认其只验证对齐，不拒绝 VECTOR 的零地址目标；包含 VECTOR/ATCM 的七段方案已由作者确认上板运行成功，但未独立逐字节回读 ATCM。
- 外层仍为明文 RZSM body 的 `cert-img` 签名，不添加 App 外层 `--enckey`；AES host key 不进入包。NONCACHE/SHARED_NONCACHE_BUFFER 当前没有有效 payload，今后启用且非空时也会被 `--encrypt-all` 选中。

### 11.2 独立产物与生成命令

保留第 10 节已验证的单段产物。以下全加密产物使用独立名称，不能交给仍仅允许 SYSTEM_PRG/SYSTEM_DATA 加密的旧 Loader。

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

当前 Debug App 的七段为 LDR_PRG、LDR_DATA、VECTOR、USER_PRG、USER_DATA、SYSTEM_PRG、SYSTEM_DATA。段 ID/包内顺序不是 RAM 地址排序；SYSTEM_DATA 仍在 SYSTEM_PRG 的低地址侧。实际长度和偏移以新 summary 为准。

本次已生成的 body 为 1,569,424 bytes，完整签名包为 1,569,936 bytes，入口仍为 `0x0011A000`。完整包 SHA-256 为 `3b55f47ce83d1a66b375bf046b471e640dc1d41b6dfdf2c3ae9050520bb4bc6a`；再次生成会因随机 IV/签名而改变哈希。

真实 App 七段解密明文、SHA-256、零 padding、目标范围/不重叠和尾块链式 IV 软件模拟均通过；Root/Image 两级 ECDSA、image key binding、App parameter hash、外层明文标志/地址/长度均通过。inspect 结果分别记录在 `rzsm_all_v2_inspect_summary.json` 和 `pn20_rzsm_all_signed_summary.json`。与已有 SSBL 的签名公钥一致，但这不代表旧 SSBL 二进制具备全段部署能力。

第一阶段已逐一比较原单段 manifest/body/parameter/package 和原 Loader padded bin/parameter/secure SSBL 的生成前后哈希，七个原产物均未改变。当时新 Loader 尚未编译或重新签名；随后已使用最新 Debug bin 生成独立 SSBL，见第 11.4 节。

### 11.3 验证范围与后续板级步骤

新增 `test_make_secure_app_package.py`，使用合成 RZAP 和非生产测试密钥。6 项测试覆盖原单段/无加密兼容、全部有效段选择、禁用/零长度段排除、IV 重试与重复拒绝、无 key/互斥选项/不兼容模式拒绝，以及 CBC 尾块软件模拟。运行：

```powershell
& 'C:\Program Files\Python312\python.exe' -m unittest discover -s tools -p test_make_secure_app_package.py -v
```

主机测试不执行真实 RSIP。当前 VS Code 缺少 C 标准头文件配置，且无可用的主机 C 编译器；助手不执行 IAR 编译。最新 Debug Loader 产物已用于第 11.4 节制包，作者已确认硬件解密与全段方案上板运行成功。以下步骤保留作为复现与进一步 RAM 回读检查流程。

1. 作者在 IAR 手动重新编译修改后的 Loader；无须因制包选项重新编译未改变的 App。
2. 使用新 Loader raw bin 按第 10 节重新 padding、生成 Loader parameter、签名并加密 SSBL；不得把旧 Debug bin 重新签名后当作新 Loader。
3. 完成离线验签与 key identity 检查后，按原地址更新 Loader parameter `0x60000000`、新 SSBL `0x60000050`、完整全加密 App package `0x60100050`；不写 raw App 或仅写 body，不修改 OTP。
4. 确认外层验签、七段解密部署、RSIP close、实际 App 入口和应用运行。各段 RAM 在 App 启动修改数据之前与原 App 对应明文完整比较，特别检查 VECTOR/ATCM、LDR/BTCM、USER/System RAM 和 HRAM，并验证 CBC padding 不覆盖相邻段。

助手仅修改源码/文档并进行离线制包与测试，不自动运行 IAR、不写 Flash、不进行 OTP provisioning；作者自行完成上板验证并确认全段方案成功。

### 11.4 新 Loader 的加密 SSBL 生成记录（2026-10-12）

输入为 `RZN2L_bsp_xspi0bootx1_loader/Debug/Exe/RZN2L_bsp_xspi0bootx1_loader.bin`，修改时间 2026-10-12 15:32:10，晚于 `secure_app_deploy.c` 的 15:22:04。raw 为 51,916 bytes，SHA-256 为 `1f8611d455a3ca46b6870db8d052cb06e28250dffea96c3a3ba9e20cabd92ca8`，内容与旧 SSBL 的输入不同。

按第 10.3 节的 512-byte/`0xFF` 规则生成独立 padded bin，为 52,224 bytes，尾部 padding 为 308 bytes。旧单段 SSBL 文件不覆盖。

| 文件（位于 tools） | 长度（bytes） | 用途/Flash 地址 |
| --- | ---: | --- |
| pn20_scheme_a_all_ssbl_padded.bin | 52,224 | 中间制包输入，不单独烧录 |
| pn20_scheme_a_all_ssbl_parameter_xspi0.bin | 76 | Loader parameter，`0x60000000` |
| pn20_scheme_a_all_secure_ssbl_xspi0.bin | 54,272 | 新加密 SSBL，`0x60000050` |
| pn20_secure_rzsm_all_app_package.bin | 1,569,936 | 第 11.2 节完整全加密 App 包，`0x60100050` |

以下命令从 workspace 根目录执行。先按第 10.3 节重新 padding，但使用本节独立的 `$padded` 路径，再生成参数和加密 SSBL：

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

已确认 `cert-img`、加密 Enable、ckey0，Root/Image 两级 ECDSA、密文签名、image key binding 和 Loader parameter hash 均通过。完整 AES-CBC 解密结果逐字节等于新 raw Loader 加全部 `0xFF` padding；与第 11.2 节全加密 App 的 Root/Image 公钥及 `.pubkey` 均一致。完整 SSBL SHA-256 为 `906b7ec7c55b69e96457a4caa854ab1e585ecdb47f6146e6e09eaf9fc06e5667`。

SSBL Code Certificate 的 `write_addr`/`dest_addr` 都是 RAM 程序地址 `0x00102800`，不是 Flash 地址。Loader parameter 中才是 Flash 来源 `0x60000050`、包含两份证书的 SSBL 长度 `0xD400`、RAM 加载基址 `0x00102000`；证书 `img_len=0xCC00` 对应 52,224-byte 密文。不要套用 App Code Certificate 的地址预期。

官方工具在 workspace 根目录输出 `pn20_scheme_a_all_secure_ssbl_xspi0.log` 与同名 `.pubkey`。旧 Loader 三个产物、原单段 App 包和现有全加密 App 包的生成前后哈希均未改变。烧录时使用上表的新 Loader parameter、新 SSBL 与全加密 App 包，不混用旧单段 SSBL；AI助手未执行任何 Flash/OTP 操作，作者已确认全段板级验证成功。
