# Crimson Desert 婚姻条件实验补丁

为《Crimson Desert》提供可配置的恋爱与婚姻条件覆盖，支持继续与其他对象结婚、选择当前伴侣，以及实验性的全员召唤 / 收回。Windows x64 ASI 插件，配置和源码注释为中文。

**当前为实验版本。** 已确认多条婚姻可以保存；v0.1.1 实机回归确认两位伴侣同时出场，以及一次指定对象离婚后其他关系保留。完整婚礼、跟随、剧情和所有 NPC 的交互仍未完成验证。

[下载 v0.1.1](https://github.com/wumoin/CrimsonDesert/releases/tag/v0.1.1) · [所有版本](https://github.com/wumoin/CrimsonDesert/releases) · [默认配置](config/CohabitationGateExperiment.ini) · [反馈问题](https://github.com/wumoin/CrimsonDesert/issues)

当前源码为 **v0.1.1 修复版**，新增实例复用、重复实例收回及按交互目标离婚保护。旧 v0.1.0 附件不包含这些修复，使用时以加载日志首行版本为准。修复的范围与验证步骤见[本轮修复说明](docs/fixes-v0.1.1.md)。

## 功能

| 功能 | 开启后的作用 |
| --- | --- |
| 好感度覆盖 | 好感度不足也能满足通常的好感度门槛 |
| 亲密度覆盖 | 亲密度不足也能满足通常的亲密度门槛 |
| 结婚资格覆盖 | 取消对方的可结婚资格限制 |
| 同居限制覆盖 | 已有同居对象也能继续触发相关恋爱交互 |
| 种族条件覆盖 | 种族不符合要求也能通过对应检查 |
| 婚姻提交覆盖 | 提交新婚姻时，排除自己已有其他已婚对象这一阻碍 |
| 当前伴侣选择 | 自动优先选择新婚对象，或手动指定一位已有伴侣 |
| 全员召唤 / 收回 | 对其他真实已婚对象执行与当前伴侣相同的召唤 / 收回动作 |


## 运行要求与兼容范围

- Windows x64，以及包含对应恋爱 / 婚姻系统的游戏内容。
- 兼容的 x64 ASI Loader。本项目使用过 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) 的 x64 `winmm.dll`。
- 本次分析与离线适配基于游戏 EXE **1.0.0.3462 / Steam build 26356046**；其他版本需要以实际定位日志为准。

插件在启动时自动查找函数并检查结构。地址移动和部分虚表槽位变化可以自动处理；函数改名、逻辑或编译方式变化仍可能需要更新补丁。无法确认目标时停止对应功能，不强行写入未知位置。

## 下载与安装

### 1. 下载发布文件

打开 [v0.1.1 Release](https://github.com/wumoin/CrimsonDesert/releases/tag/v0.1.1)，下载附件：

- [CohabitationGateExperiment.asi](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.1/CohabitationGateExperiment.asi)：插件。
- [CohabitationGateExperiment.ini](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.1/CohabitationGateExperiment.ini)：配置。
- [winmm.dll](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.1/winmm.dll)：首次安装使用的 x64 ASI Loader，来自 Ultimate ASI Loader v10.0.2。


### 2. 退出游戏并备份存档

完整退出游戏，复制保存以下目录中的存档：

```text
%LOCALAPPDATA%\Pearl Abyss\CD\save
```

在副本或实验进度上使用。关闭补丁不会撤销游戏已经保存的关系变化。

### 3. 安装加载器和补丁


把`.asi` `.dll`和 `.ini` 放到游戏的 `bin64` 目录。首次安装所需文件如下，盘符和 Steam 库路径以自己的安装位置为准：

```text
Crimson Desert/
└── bin64/
    ├── CrimsonDesert.exe
    ├── winmm.dll
    ├── CohabitationGateExperiment.asi
    └── CohabitationGateExperiment.ini
```

随后启动游戏就行了，如需开启或关闭某些功能，可查看配置说明对`CohabitationGateExperiment.ini`进行修改

## 配置说明

所有开关使用 `1` 开启、`0` 关闭；`PreferredPartnerKey` 是对象编号，不是开关。`Enabled` 控制全部功能。运行期间约每 **5 秒**重新读取配置，不需要重启游戏；

### 完整配置示例

以下为发布附件的完整默认配置：除 `PreferredPartnerKey=0` 外，其余开关均为 `1`。

```ini
[Experiment]
; 开启补丁。
Enabled=1

[Conditions]
; 好感度不足也能满足好感度要求。
IgnoreFriendlyLevel=1
; 取消对方的结婚资格限制。
IgnoreMarriageable=1
; 已有同居对象也能继续示爱或求婚。
IgnoreLivingTogether=1
; 亲密度不足也能满足亲密度要求。
IgnoreAffinityLevel=1
; 种族不符合要求也能通过。
IgnoreTribe=1

[Relationships]
; 已经结婚也能与其他对象再次结婚。
IgnoreExistingMarriageOnCommit=1
; 0 自动选择当前伴侣；其他数值指定已婚对象编号。
PreferredPartnerKey=0
; 设为 1 后，召唤或收回当前伴侣时，也处理其他已婚伴侣。
SpawnAllPartners=1
```

在现有对应分区中修改键值，不要重复创建同名分区或同名键。

### 所有选项与默认值

| 分区 | 配置项 | 附件默认值 | 开启或设置后的作用 |
| --- | --- | --- | --- |
| `Experiment` | `Enabled` | `1` | 启用下方所选功能；设为 `0` 关闭全部覆盖 |
| `Conditions` | `IgnoreFriendlyLevel` | `1` | 按虚拟高等级判断好感度门槛，实际好感度不变 |
| `Conditions` | `IgnoreMarriageable` | `1` | 绕过对方是否可结婚的判断 |
| `Conditions` | `IgnoreLivingTogether` | `1` | 绕过已有同居对象的入口限制 |
| `Conditions` | `IgnoreAffinityLevel` | `1` | 按虚拟高等级判断亲密度门槛，实际亲密度不变 |
| `Conditions` | `IgnoreTribe` | `1` | 绕过种族条件，实际种族不变 |
| `Relationships` | `IgnoreExistingMarriageOnCommit` | `1` | 提交婚姻时排除自己现有婚姻的阻碍，并启用当前伴侣选择 |
| `Relationships` | `PreferredPartnerKey` | `0` | `0` 自动选择；非零值指定当前角色的一位真实已婚对象 |
| `Relationships` | `SpawnAllPartners` | `1` | 触发原版召唤 / 收回时，把同样动作扩展到其他真实已婚对象 |

表中是发布 INI 的默认值。删除配置文件或键时，插件仍使用关闭兜底（仅同居子开关缺省为 `1`），请保留完整配置。

五项条件覆盖作用于对应条件的所有调用，其他任务或互动也可能受到影响。它们不会跳过所有剧情或关系阶段，也不会为缺少婚礼内容的 NPC 自动补齐脚本。


## 实现原理

补丁处理三个环节，实际婚姻状态仍由游戏原版流程写入：

1. **条件判断**：通过 MSVC RTTI 查找五类条件的虚函数表，解析实际评估槽，再替换进程内入口。保留上下文不可用和未知返回值。
2. **婚姻提交与当前对象**：用 MinHook 挂钩原资格检查。仅在原检查失败、且开启对应选项时，构造排除当前角色已有婚姻的私有副本重新检查；不改原关系数组。跟踪真实写入路径，并且只选择已存在的真实已婚记录。
3. **出场与离婚保护**：读取当前玩家的角色注册表，按对象键、所属角色和实例核对出场。在原游戏调用线程中修复运行时实例关联，调用原版动作并保留原生回执；离婚使用明确交互目标及独立记录快照。实例关联字段会改变，婚姻状态仍交给原版写入。

好感 / 亲密度覆盖使用虚拟等级 `127` 重新计算原来的比较，保留 `>=`、`<` 等方向，避免成功与拒绝分支同时成立。它不会增加实际数值，也不是让所有比较无条件成立，例如“等级恰好等于 3”仍不成立。

插件修改运行中的内存入口，不修改磁盘上的游戏 EXE、资源包或存档格式。游玩触发的原版关系更新和出场动作仍可能改变正常保存的数据。自动定位检查结构与语义模式，不保证未来所有游戏更新均无需适配。

详细说明见 [实现说明](docs/design.md)。


## 从源码构建

需要 Windows x64、Visual Studio / Build Tools 的 C++ x64 工具链及 Windows SDK。MinHook 必要源码已包含，无需另外下载运行时 DLL。

```powershell
git clone https://github.com/wumoin/CrimsonDesert.git
cd CrimsonDesert
.\build.cmd
```

构建脚本优先使用当前工具链环境，否则通过 `vswhere` 查找已安装的 Visual Studio。编译使用 C++17、UTF-8、`/O2` 和静态运行库 `/MT`，输出：

```text
build/
├── CohabitationGateExperiment.asi
├── CohabitationGateExperiment.ini
└── MinHook-LICENSE.txt
```

`build` 还会包含编译中间文件，不纳入 Git。脚本只构建，不启动游戏、不安装插件；需要按安装章节手动复制发布文件。研究验证工具不随插件运行，也未加入仓库。

## 项目目录与依赖

```text
src/          插件源码、自动定位和关系处理
config/       带中文注释的默认配置
docs/         实现原理、验证记录和依赖来源
evidence/     历史插件日志摘录
vendor/       MinHook 必要源码与上游许可证
build.cmd     Windows x64 构建脚本
```

- [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4 静态编译进插件，上游及 HDE 许可证见 [vendor/minhook/LICENSE.txt](vendor/minhook/LICENSE.txt)。二进制分发需保留 `MinHook-LICENSE.txt`。
- Release 附带 Ultimate ASI Loader v10.0.2 的 x64 `winmm.dll` 和 MIT 许可证，源码仓库不跟踪该二进制，`build.cmd` 也不会生成它。
- 本仓库不包含游戏资源、游戏二进制、第三方存档、存档密钥或私有研究工具。

更多来源与版本信息见 [依赖说明](docs/dependencies.md)。
