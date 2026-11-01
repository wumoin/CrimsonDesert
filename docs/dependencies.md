# 依赖与来源

## Mod 运行

- Windows x64 与当前适配的游戏版本。
- [ThirteenAG Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)，研究时使用官方发布的 x64 `winmm.dll`。本仓库不包含该 DLL 或其源码；安装时从官方来源获取并遵守其 MIT 许可证。

## 编译

- Visual Studio C++ x64 工具链、Windows SDK。
- 构建脚本可使用 Visual Studio 自带的 `vswhere` 查找工具链。
- `/MT` 静态链接 C/C++ 运行库；插件本身不要求另外安装 VC 运行库包。
- [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4，固定提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`。必要的 x64 源码位于 `vendor/minhook`，保持上游实现不变。用于提交资格函数的内联挂钩及指令解析，静态编译进插件，不需要额外运行时 DLL。
- MinHook 与所含 HDE 代码的 BSD 许可证保留于 `vendor/minhook/LICENSE.txt`；构建时复制为 `build/MinHook-LICENSE.txt`，分发二进制时应一并保留。

## 研究参考

- [dmm-parser 的 PAPGT 格式实现](https://github.com/exodiaprivate-eng/dmm-parser/blob/main/src/binary/papgt.rs)，用于识别当前签名索引格式；未复制其实现。
- [Crimson Desert Save Editor](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS)，本地用于解析公开测试存档。本仓库不含其源码或密钥。

游戏二进制、提取资源、第三方存档和原有研究工具克隆都不在本仓库中。插件源码为本次实验制作；测试用输入辅助程序已从当前版本删除。
