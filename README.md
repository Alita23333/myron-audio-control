# Myron Audio Control

KernelSU + Zygisk 音量归一与音效场景切换模块，带中文 WebUI。模块 ID：`myron_audio_control`。

> **设备专用，不是通用音量解锁模块。** 安装器要求设备代号为 `myron`，并校验随模块保存的原厂音频配置及手机当前目标文件；不再锁定单一系统构建指纹。原厂文件不匹配时会拒绝安装/启用，不要删除校验强行安装。

## 下载与安装

在 [Releases](https://github.com/Alita23333/myron-audio-control/releases) 下载模块 ZIP，并核对 SHA-256。通过 KernelSU 安装，完整重启后打开模块 WebUI。

新安装默认：**全部可注入应用、音量归一开启、保留原厂音效**。升级保留已有设置及范围；先前刻意使用小范围的配置不会被重置。

需要 KernelSU 和已启用的 Zygisk Next。与旧的 Dolby/音量测试模块、Audio Lowpower App List Bypass 不要同时启用，安装器会拒绝已定位的覆盖冲突。

## 功能

- **解除应用音量衰减**：仅将原厂低功耗名单所有模板的 `volume` 改为 `1.0`，保留包名、分组与 `power_save`。不是强制最大音量，不修改系统音量、应用自身音量、音频焦点压低或扬声器保护。
- **保留原厂音效**：音效 JSON 与原厂字节一致。
- **统一默认音效**：清空 `app_profiles`，其余音效字段保持不变。
- **为应用单独设置**：以原厂配置为基础，为指定包名选择长视频、短视频、导航或默认音效；每个包名最多一个分组。

场景表示配置选择，不代表已经测得实际 Dolby profile 编号。`scenario_volume_flag`、`volume_levels`、分段扬声器调音保持原厂值。

所有切换均需**保存后完整重启**。页面不会自动重启，也不提供实时音效切换。

## 隔离与读取替换

开机早期显式 bind mount 覆盖两个配置文件，服务阶段只验证，不补挂。Zygisk 在应用 specialization 阶段通过固定 root companion 获取配置快照，仅对已加载 `libtinyxml2.so` 的 `fopen` 做精确路径、只读模式 PLT 替换，为 XML 解析器提供独立内存文件流。

不全局 hook libc，不修改 AudioTrack 正常音量接口，不修改包名，不授予应用 root。读取/注册失败退回原厂行为。

本机已审计 Zygisk Next **1.5.0 / versionCode 843**。在“仅还原挂载”策略下，允许注入的进程可以保持原厂文件视图，同时通过原生替换获得归一 XML；不恢复可见挂载，不改变全局策略。强制禁止注入、未知版本/异常策略不绕过。

`hook_registered` 仅表示注册；`read_override` 表示实际提供配置。文件视图哈希与替换提供的哈希必须分别看待；效果仍需播放轨道或试听验证。

## 适配与验证范围

已经过实际播放与恢复验收的系统：

```text
OS4.0.0.25.XPMCNXM（Android 17）
OS4.0.0.30.XPMCNXM（Android 17）
```

模块不再把上述版本作为安装白名单；列表仅表示已测试范围。系统更新后建议先确认 `/odm/etc/audio/audio_lowpower_app_list.xml` 与 `/odm/etc/audio/audio_effects_config.json` 的哈希，再进行同一 WAV 的播放、音效往返和禁用恢复测试。原厂文件哈希不一致时模块会自动拒绝加载，需重新审计并制作相应基线。

已验证同源播放器普通外放 PCM16、48 kHz、双声道路径：名单额外 −7 dB 衰减消失；撤销应用挂载条件下仍实际读取归一 XML；音效原厂/默认回落可逆；禁用重启后原厂哈希、无覆盖挂载及 −7 dB 衰减恢复；扩大后重启回归通过。

arm64 已进行实际播放验证；arm32 已构建，未做实际播放验收。硬件 offload、直通、蓝牙/USB 等不保证。全范围是启用范围，不是对每个应用逐一认证。参见 [验证摘要](VALIDATION.md)。

## 恢复与救援

禁用或卸载模块后完整重启，hook 与挂载撤销，原厂只读文件自然恢复；不会向 ODM 写回。启动异常时使用 KernelSU 安全模式，必要时从 Recovery 删除：

```text
/data/adb/modules/myron_audio_control
```

只删除本模块，不删除其他模块。测试时临时修改的应用配置需单独恢复。

## 构建

Windows PowerShell、Android NDK（开发验证使用 28.2.13676358）、Node.js、Git。依赖头文件与源码保留在 `third-party`。

```powershell
node tools/CHECK-WEBUI.mjs
.\BUILD.ps1 -NdkRoot "C:\Android\Sdk\ndk\28.2.13676358"
```

输出在 `dist/`，包括 ZIP 与 SHA-256 清单。构建检查基线、ABI 导出、JS 语法、ZIP 安全路径及条目哈希。原厂基线禁用 Git 文本归一化以保留字节哈希。

设备只读诊断：`tools/VERIFY-DEVICE.ps1 -AdbPath adb`。本地模拟界面：`node tools/PREVIEW-WEBUI.cjs`，仅监听 127.0.0.1:8765，不执行手机或 shell 命令。

## 依赖与授权说明

- Zygisk API v5：topjohnwu/zygisk-module-sample，授权见头文件。
- nlohmann/json 3.11.3：MIT，授权见 `json.hpp`。
- tinyxml2 10.0.0：zlib，授权见源文件。
- PicoSHA2：MIT，授权见头文件；以文件哈希识别所用快照。
- KernelSU WebUI API 3.0.2：Apache-2.0，来自官方 npm 包。

第三方通知保留。原厂配置来源于已审计固件，不宣称对它拥有版权或更改其授权。本仓库尚未另行指定原创代码的开源许可证；公开可见不自动赋予任意再分发授权。
