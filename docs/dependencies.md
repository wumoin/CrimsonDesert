# 依赖与来源

## Mod 运行

- Windows x64 与当前适配的游戏版本。
- [ThirteenAG Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) v10.0.2。Release 附带官方 x64 包中的加载器，使用官方支持的 `winmm.dll` 名称，附带 `Ultimate-ASI-Loader-LICENSE.txt`（MIT）。源码仓库不跟踪该 DLL 或其源码，构建脚本不生成它。
- 加载器来自 [v10.0.2 官方发布](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2)的 `Ultimate-ASI-Loader_x64.zip`；附件与压缩包中的 `dinput8.dll` 内容一致，仅更改文件名。SHA-256：`fa2b9bd317bb597906bc2c4cc2008c1444e50bfdc367e648d18f36f015812e27`。

## 编译

- Visual Studio C++ x64 工具链、Windows SDK。
- Windows SDK 的 `rc.exe` 将默认 INI 嵌入 ASI，升级时无需额外默认配置文件。
- 构建脚本可使用 Visual Studio 自带的 `vswhere` 查找工具链。
- `/MT` 静态链接 C/C++ 运行库；插件本身不要求另外安装 VC 运行库包。
- [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4，固定提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`。必要的 x64 源码位于 `vendor/minhook`，保持上游实现不变。用于提交、对象查询、实例创建 / 出场、交互请求及离婚的内联挂钩和指令解析，静态编译进插件，不需要额外运行时 DLL。
- MinHook 与所含 HDE 代码的 BSD 许可证保留于 `vendor/minhook/LICENSE.txt`；构建时复制为 `build/MinHook-LICENSE.txt`，分发二进制时应一并保留。

## 研究参考

- [dmm-parser 的 PAPGT 格式实现](https://github.com/exodiaprivate-eng/dmm-parser/blob/main/src/binary/papgt.rs)，用于识别当前签名索引格式；未复制其实现。
- [Crimson Desert Save Editor](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS)，本地用于解析公开测试存档。本仓库不含其源码或密钥。

游戏二进制、提取资源、第三方存档和原有研究工具克隆都不在本仓库中。插件源码为本次实验制作；测试用输入辅助程序已从当前版本删除。
