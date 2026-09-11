# PN2.0 RZ/N2L Socket 板 Secure Boot Python 验证流程

## 1. 目的和适用范围

本文档记录 PN2.0 `Secure SSBL -> Secure App` 在 RZ/N2L Socket 板上的已验证 Python 工具流程。该流程使用 Renesas R01AN6526 的 Device Setup / Secure Boot 工具，将当前构建的 Secure SSBL 和整体签名 App 包写入 xSPI0 Flash，并通过正常 xSPI 启动验证 Secure Boot。

当前流程使用 Scheme B（整体 App 签名、明文部署）：

```text
BootROM
  -> 认证并启动 Secure SSBL
  -> Secure SSBL 使用 RSIP 验签整体 Secure App package
  -> Secure SSBL 解析受签名保护的 RZAP manifest 并分段部署 App
  -> 跳转至 App system_init
```

本文的日常验证流程适用于已完成安全配置的板卡。OTP 写入、`setboot --enable` 和禁用 SCI/USB Boot 是一次性或不可逆操作，不属于日常验证步骤。

## 2. 工具、工程和前置条件

| 项目 | 路径或要求 |
| --- | --- |
| 工程根目录 | `C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project` |
| Renesas 工具目录 | `C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl` |
| Device Setup S-record | `..\rzn2l\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec` |
| 连接端口 | RSK USB Serial Port 对应的 COM 端口，例如 `COM3` |
| Python | Python 3；在工具目录执行 `python` |
| 密钥 | `rootkey-pair.pem`、`loaderkey-pair.pem` 必须与已配置到板卡 OTP 的 Root Public Key 对应 |

开始前，先在 IAR 中编译当前 Loader 和 App 工程。本文不自动执行 IAR 编译。

Secure SSBL 工程必须满足以下条件：

```c
#define SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE (1u)
```

并且 `hal_entry.c` 中必须实际调用 `secure_app_verify_package()`。仅打印验签成功而跳过该函数，会使已验证 package body 长度保持为零，随后部署必然失败。

## 3. Flash 地址布局

`writeflash --addr` 参数使用 8 位十六进制字符串，不能带 `0x` 前缀。

| Flash 起始地址 | 文件 | 作用 |
| ---: | --- | --- |
| `60000000` | `pn20_ssbl_parameter_xspi0.bin` | BootROM Loader parameter（xSPI 配置、SSBL 源地址、长度、RAM 目标地址） |
| `60000050` | `pn20_secure_ssbl_xspi0.bin` | 认证后的 Secure SSBL image |
| `60100050` | `pn20_secure_app_package.bin` | Key Certificate + Code Certificate + 整体 App body |

Loader parameter 内的 `src_addr=60000050` 必须与 Secure SSBL image 的实际 Flash 地址一致。不要把 `pn20_secure_ssbl_xspi0.bin` 写到 `60000000`，否则会覆盖 BootROM 读取的 parameter。

对于 `--dest_addr 00102000`，Secure SSBL image 前部有 `0x800` 字节 Loader Certificate，实际 Loader 程序入口为 `0x00102800`。

## 4. 生成 Secure SSBL

在 PowerShell 中进入工具目录并定义本次构建输入文件：

```powershell
Set-Location "C:\RenesasDev\r01an6526ej0310-rzt2-n2-security-secureboot\Secure device setup\pn20_secure_ssbl"

$raw = "C:\Users\MyPC\Desktop\Renesas_PROFINET_IRT_DEVKIT_V2.0.0\iar_project\RZN2L_bsp_xspi0bootx1_loader\Debug\Exe\RZN2L_bsp_xspi0bootx1_loader.bin"
$padded = ".\pn20_ssbl_padded.bin"
```

### 4.1 按当前 Loader 大小填充

`parameter_generator.py loader` 要求输入 Loader 长度为 `0x200` 的整数倍。必须根据本次构建的 `$raw` 实际大小向上取整，以 `0xFF` 填充；不要使用历史镜像的固定长度。



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

### 4.2 生成 Loader parameter 和 Secure SSBL image

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

生成后至少确认下列文件存在：

```powershell
Get-Item .\pn20_ssbl_parameter_xspi0.bin, .\pn20_secure_ssbl_xspi0.bin
Get-FileHash .\pn20_secure_ssbl_xspi0.pubkey
```

## 5. 生成整体签名 Secure App package

以下命令使用当前 App raw binary 生成 Scheme B 的整体签名包。`--app_start_addr 60100250` 是证书后的 App body 地址，`60100050` 是完整 package 的 Flash 起始地址。

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

当前 Scheme B 中，`pn20_app_param.bin` 用于生成 Code Certificate；写入 Flash 的对象是完整的 `pn20_secure_app_package.bin`，不单独写入 App parameter。

确认 Loader 和 App package 用同一 Root Key 生成：

```powershell
Get-FileHash .\pn20_secure_ssbl_xspi0.pubkey, .\pn20_secure_app_package.pubkey
Get-Item .\pn20_secure_app_package.bin
```

两个 `.pubkey` 文件的 SHA-256 必须一致。若不一致，停止烧录并检查两个生成命令使用的 `rootkey-pair.pem`。

## 6. 下载并启动 Device Setup Program

1. 关闭占用 COM 口的 Tera Term、IAR 串口窗口或其他串口工具。
2. 将 Socket 板切换到 SCI Boot Mode。
3. 复位或重新上电，使 BootROM 等待 SCI 下载。
4. 在同一个 PowerShell 会话执行：

```powershell
$COM = "COM3" # 改为设备管理器中实际的 RSK USB Serial Port
$setupSrec = "..\rzn2l\RZN2L_RSK_SecureDeviceSetup_SCI_qspi.out.srec"

python .\secure_device_setup.py start `
  --port $COM `
  --boot_mode sci `
  -i $setupSrec
```

仅当 `start` 成功后再执行 Flash 写入。`Failed to send the program file.` 表示 BootROM 传输阶段失败，尚未写入 Flash 或 OTP；重新检查 SCI Boot Mode、端口占用、COM 号和复位时序。

## 7. 写入 Secure SSBL 和 Secure App

Device Setup Program 启动成功后，在同一终端执行以下三条命令：

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

按上述顺序完成后，退出 Device Setup Program，将板卡切换到正常 xSPI Startup Mode，再断电重上电或复位。

## 8. 验证判定与故障定位

通过 UART 观察启动日志。一次成功验证至少应满足：

1. BootROM 成功认证并跳转 Secure SSBL。
2. Secure SSBL 的 RSIP package verification 输出成功。
3. Secure SSBL 成功解析 RZAP manifest 并完成 App 各段部署。
4. Secure SSBL 跳转至 App `system_init`，App 进入 `main()` 和正常业务任务。

| 现象 | 优先检查项 |
| --- | --- |
| `start` 传输失败 | SCI Boot Mode、COM 口占用、端口号、复位时序、`SCI_qspi.out.srec` 路径 |
| BootROM 不启动 SSBL | `60000000` 是否写入 Loader parameter；parameter 中的 `src_addr` 是否为 `60000050`；SSBL image 是否误写到 `60000000` |
| SSBL 验签失败 | OTP Root Public Key 与 `.pubkey` 是否匹配；package 是否完整；是否使用了同一 root key |
| SSBL 部署失败 | `SSBL_CFG_RSIP_PACKAGE_VERIFY_ENABLE` 是否为 `1u`；是否实际调用 `secure_app_verify_package()`；App package 是否写在 `60100050` |
| HyperRAM 初始化卡住 | 保持已验证顺序：先执行 `hram_init()`，后执行 `bsp_qspi_quad_enable()`；QSPI Flash 和 HyperRAM 共用 XSPI0 控制器 |

## 9. 首次 Provisioning 的边界

只有全新、尚未配置 Secure Boot 的板卡才需要 OTP Root Key 写入和 secure boot enable。执行前必须核对 `.pubkey` 与计划写入 OTP 的 Root Public Key Hash；写入后不可按日常测试方式回退。

日常验证不要重复执行以下类型操作：

```text
setboot --enable
写入 OTP Root Public Key Hash
setsciboot --disable
setusbboot --disable
```

尤其不要为了排查启动问题禁用 SCI Boot。保留 SCI Boot 可以在后续重新下载 Device Setup Program 并更新 Flash 内容。

## 10. 参考资料

- Renesas `r01an6526ej0310-rzt2-n2-security-secureboot.pdf`，2.2.7 “Program to Flash”。
- [pn20_secure_ssbl_freertos_fault_investigation_cn.md](pn20_secure_ssbl_freertos_fault_investigation_cn.md)
- [README_loader_app_split.md](README_loader_app_split.md)
- [pn20_rzn2l_secure_app_scheme_comparison_cn.md](pn20_rzn2l_secure_app_scheme_comparison_cn.md)
