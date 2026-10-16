# 0.3.0 升级兼容性改动与测试

日期：2026-10-16。

- 移除 Zygisk Next 843 精确版本号判断。当前 Next 862、仅还原挂载策略识别通过。
- 无系统版本/构建指纹白名单；系统原厂音频文件哈希可以改变。
- 开机覆盖前从只读 ODM 保存内容寻址基线快照，记录指纹、哈希、权限和 SELinux 标签。指纹仅记录，不作为门禁。
- 新基线通过 XML/JSON 结构检查后，保留用户配置并重新生成变体；不沿用旧固件完整音效文件覆盖新系统。
- 本模块已有覆盖通过快照及哈希验证；其他无法识别的覆盖拒绝作为原厂输入。
- `volume` 只归一到 1.0；保留新系统包名、power_save、profile 编号及分段调音。
- Zygisk 子进程失败也写一次诊断日志；WebUI 区分服务侧挂载和实际读取证据。
- 保留 Myron 设备识别、配置接口 version=1、Zygisk API 合约、冲突检查和完整性验证。这些不代表外部系统或提供器的版本白名单。

## 已执行

arm64/arm32 编译、导出、WebUI 静态及脚本语法检查、ZIP 安全路径和条目 SHA-256 检查。

`tools/TEST-COMPATIBILITY.ps1` 在手机 /data/local/tmp 的独立目录执行新后端，不安装模块、不挂载覆盖；12 项验证通过：变更后的 XML 可归一、保留 power_save 和包名、关闭时字节还原、原厂 JSON 字节一致、默认回落、未知字段保留、profile 9 保留、包名唯一、Next 862 策略识别、错误结构拒绝、已有旧模块覆盖识别且不误采为原厂。

模拟更改的配置不是新固件实测。未知未来固件、其他 Zygisk 提供器和特殊播放路径不作保证。

## 当前设备部署

用户授权后安装 0.3.0 / versionCode 6 并完整重启。OS4.0.2.0.XPMCNXM / Next 1.5.1 (862) 上 bootReady=true、disabled=false、rebootRequired=false、unmountOnlyPolicyRecognized=true。当前只读 ODM 原厂配置已采集成 0444 基线快照，记录正确固件指纹和 SELinux 标签。原有 volumeUnity=true、effectMode=default、scope=all 保留。

用户播放正式 B 站 8.82.0 后，活跃 PID 17550 / UID 10189 的 Track 58、session 201 为 PCM16/44100Hz/双声道，左右轨道增益均为 0 dB；升级前同应用为 -7 dB。本次轨道额外衰减已消失。可见文件仍是原厂 XML（352c9818...），没有为应用恢复可见模块挂载。

日志中记录到 B 站 web 子进程 policy_unmount_only 与 hook_registered。读取时未保留到播放器初次 read_override 日志，不能将 web 进程 hook 注册等同于播放器实际读取日志；播放轨道的 0 dB 是本次独立验证证据。本轮未再进行禁用/重启恢复往返，也未做未来真实 OTA 试验。

安装 ZIP：myron-audio-control-v0.3.0-20261016-182157.zip；SHA-256：d51b0cac3a8b7a6af9fe1ee8b5194e079065de338eff7b53e3ee58238ac87603。

升级前完整模块归档保存在电脑 myron-audio-upgrade-20261016/myron_audio_control-before-0.3.0.tar（不属于公开源码仓库）。
