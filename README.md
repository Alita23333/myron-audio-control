# Myron Audio Control

KernelSU + Zygisk 音量归一与音效场景切换模块，带中文 WebUI。模块 ID：`myron_audio_control`。

> **适用设备为 `myron`。** v0.3.0 起无系统版本、构建指纹或 Zygisk Next 版本白名单，也不再要求新系统音频文件匹配旧固件哈希。按当前只读 ODM 配置的结构和实际隔离策略判断兼容性。设备识别、输入结构、快照完整性及覆盖冲突检查仍保留。

## 下载与安装

在 [Releases](https://github.com/Alita23333/myron-audio-control/releases) 下载模块 ZIP，并核对 SHA-256。通过 KernelSU 安装，完整重启后打开模块 WebUI。

新安装默认：**全部可注入应用、音量归一开启、保留原厂音效**。升级保留已有设置及范围；先前刻意使用小范围的配置不会被重置。

需要 KernelSU 和支持本模块 Zygisk API 的注入环境。Zygisk Next 按实际策略识别，不限定版本号；其他实现的普通可注入进程可尝试使用，未验证的排除策略不会被绕过。与旧的 Dolby/音量测试模块、Audio Lowpower App List Bypass 不要同时启用。

## 功能

- **解除应用音量衰减**：仅将原厂低功耗名单所有模板的 `volume` 改为 `1.0`，保留包名、分组与 `power_save`。不是强制最大音量，不修改系统音量、应用自身音量、音频焦点压低或扬声器保护。
- **保留原厂音效**：音效 JSON 与原厂字节一致。
- **统一默认音效**：清空 `app_profiles`，其余音效字段保持不变。
- **为应用单独设置**：直接搜索并选择已安装应用，无需填写包名；选择长视频、短视频、导航或默认音效。支持显示系统应用，每个应用只能添加一条规则。详见 [应用选择器](APP-SELECTOR.md)。
- **独立低音四档**：原厂、轻度、中度、较强；分别保留原厂衰减或减少 1/2/3 格，仅调整已验证的背部低音通道。默认原厂，升级保留旧设置。轻度已完成中低音量短时试听，中度/较强为实验档，不标注未经测量的 dB。详见 [低音控制说明](BASS-CONTROL.md)。

场景表示配置选择，不代表已经测得实际 Dolby profile 编号。`scenario_volume_flag`、`volume_levels`、分段扬声器调音保持原厂值。

所有切换均需**保存后完整重启**。页面不会自动重启，也不提供实时音效切换。

低音后台只在已识别功放、普通音频模式、stereo 外放路径及正常功放状态下申请设置，读回核对后才报告生效。观察到暂停/关闭、通话模式、其他路由、禁用/卸载时释放自己持有的衰减值；系统主动改值时让出控制，等下一次可观察的完整播放启动再尝试，不循环强制写回。独立看护进程在控制进程退出或失联后尝试回退。禁用/卸载后完整重启仍是最终恢复方式。

## 隔离与读取替换

开机早期显式 bind mount 覆盖两个配置文件，服务阶段只验证，不补挂。Zygisk 在应用 specialization 阶段通过固定 root companion 获取配置快照，仅对已加载 `libtinyxml2.so` 的 `fopen` 做精确路径、只读模式 PLT 替换，为 XML 解析器提供独立内存文件流。

不全局 hook libc，不修改 AudioTrack 正常音量接口，不修改包名，不授予应用 root。读取/注册失败退回原厂行为。

Next 的“仅还原挂载”按启用状态和 `denylist_enforce=2` 识别，不查询版本号；已对 1.5.1 / 862 的策略识别执行隔离测试。在此策略下，允许注入的进程保持原厂文件视图，通过原生替换获得归一 XML；不恢复可见挂载，不改变全局策略。强制排除和无法识别的策略仍保留原厂行为。

`hook_registered` 仅表示注册；`read_override` 表示实际提供配置。文件视图哈希与替换提供的哈希必须分别看待；效果仍需播放轨道或试听验证。

## 适配与验证范围

已经过实际播放与恢复验收的系统：

```text
OS4.0.0.25.XPMCNXM（Android 17）
OS4.0.0.30.XPMCNXM（Android 17）
OS4.0.2.0.XPMCNXM（Android 17；v0.3.0 实际播放，v0.4.1 安装与应用列表验证）
```

以上是历史实际验收版本，不是安装白名单。v0.3.0 会在安装及每次开机的早期覆盖前检查只读 ODM 原厂文件，按内容哈希保存只读基线快照、固件信息、权限及 SELinux 标签。新文件结构兼容时保留用户设置，按新基线重新生成配置，保留新增包名、原厂 profile 编号及其他调音字段。已挂载旧模块时只认可经完整验证的本模块生成文件，绝不将可见变体采集为原厂。

不同哈希不再导致拒绝。配置结构改变、所选场景被系统删除、源文件来自无法识别的覆盖层或注入 API 不兼容时，仍可能停止相应功能；取消版本白名单无法保证尚未发布系统的行为不变。升级后请检查实际读取与播放轨道，而非仅看“服务侧已载入”。普通外放同源 WAV、音效往返及禁用重启恢复仍是建议验收顺序。

已验证同源播放器普通外放 PCM16、48 kHz、双声道路径：名单额外 −7 dB 衰减消失；撤销应用挂载条件下仍实际读取归一 XML；音效原厂/默认回落可逆；禁用重启后原厂哈希、无覆盖挂载及 −7 dB 衰减恢复；扩大后重启回归通过。

arm64 已进行实际播放验证；arm32 已构建，未做实际播放验收。硬件 offload、直通、蓝牙/USB 等不保证。全范围是启用范围，不是对每个应用逐一认证。参见 [验证摘要](VALIDATION.md)。

## 恢复与救援

禁用或卸载模块后完整重启，hook 与挂载撤销，原厂只读文件自然恢复；不会向 ODM 写回。启动异常时使用 KernelSU 安全模式，必要时从 Recovery 删除：

```text
/data/adb/modules/myron_audio_control
```

只删除本模块，不删除其他模块。测试时临时修改的应用配置需单独恢复。

## 构建

Windows PowerShell、JDK 17 或以上、Android SDK Platform 35、Build-tools 35.0.1、Android NDK（开发验证使用 28.2.13676358）、Node.js、Git。依赖头文件与源码保留在 `third-party`。设置 `JAVA_HOME`、`ANDROID_HOME`、`ANDROID_NDK_HOME`，或传入以下参数；不依赖开发者本机工具中心目录。

```powershell
node tools/CHECK-WEBUI.mjs
.\BUILD.ps1 -NdkRoot "C:\Android\Sdk\ndk\28.2.13676358" -SdkRoot "C:\Android\Sdk" -JdkRoot $env:JAVA_HOME
```

输出在 `dist/`，包括 ZIP 与 SHA-256 清单。构建检查基线、ABI 导出、JS 语法、ZIP 安全路径及条目哈希。原厂基线禁用 Git 文本归一化以保留字节哈希。

设备只读诊断：`tools/VERIFY-DEVICE.ps1 -AdbPath adb`。本地模拟界面：`node tools/PREVIEW-WEBUI.cjs`，仅监听 127.0.0.1:8765，不执行手机或 shell 命令。

开发验证工具需显式传入自己的设备序列号：`tools/TEST-APPS.ps1 -Serial <设备序列号> -AdbPath <adb.exe路径>`；配置兼容性检查使用 `TEST-COMPATIBILITY.ps1`。`TEST-BASS.ps1` 默认仅自检/探测，增加 `-Live` 会临时修改低音、暂停/重播媒体并测试恢复，仅限已确认硬件及中低音量的受控测试。

## 依赖与授权说明

- Zygisk API v5：topjohnwu/zygisk-module-sample，授权见头文件。
- nlohmann/json 3.11.3：MIT，授权见 `json.hpp`。
- tinyxml2 10.0.0：zlib，授权见源文件。
- PicoSHA2：MIT，授权见头文件；以文件哈希识别所用快照。
- KernelSU WebUI API 3.0.2：Apache-2.0，来自官方 npm 包。

第三方通知保留。原厂配置来源于已审计固件，不宣称对它拥有版权或更改其授权。本仓库尚未另行指定原创代码的开源许可证；公开可见不自动赋予任意再分发授权。
