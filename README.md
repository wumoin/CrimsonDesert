# 红色沙漠 后宫插件

为《红色沙漠》提供可配置的恋爱与婚姻条件覆盖，支持继续与其他对象结婚、管理已有伴侣，以及全员召唤 / 收回与读档同行恢复。Windows x64 ASI 插件。

[下载 v0.1.3](https://github.com/wumoin/CrimsonDesert/releases/tag/v0.1.3) · [所有版本](https://github.com/wumoin/CrimsonDesert/releases) · [默认配置](config/CohabitationGateExperiment.ini) · [反馈问题](https://github.com/wumoin/CrimsonDesert/issues)

当前版本为 **v0.1.3 实验预发布版**。进入存档后按 **F8**，即可选择优先伴侣、修改或重置名字；伴侣美容可单独选择对象。两个弹窗支持中文 / English 切换，所有设置保存在同一个 INI 中。



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
| F8 伴侣管理 | 按 F8 列出当前角色的已婚对象，设置优先伴侣、恢复自动选择、保存或重置各自的名字 |
| 伴侣美容选择 | 点击伴侣美容后，在插件窗口中选择已婚对象，再进入原版美容；取消时不打开编辑器 |
| 弹窗中英文切换 | 两个弹窗顶部共用“语言 / Language”选择，立即切换文案并记住设置 |
| 全员召唤 / 收回 | 在召唤 / 收回结婚对象时，同步召唤 / 收回所有已婚对象 |
| 读档恢复同行 | 分别读取各位已婚伴侣保存的同行状态，只恢复已选择同行的对象 |

## F8 伴侣管理

进入存档后，在游戏窗口中按 **F8**，选择列表中的一位已婚对象：

- **设为优先伴侣**：立即写入插件 INI 的 `PreferredPartnerKey`，无需等待配置刷新。
- **恢复自动选择**：把 `PreferredPartnerKey` 恢复为 `0`。
- **保存名字**：修改名字后点击保存，支持中文，最长 30 个 UTF-16 字符单位。
- **重置名字**：删除该对象的自定义名字，并恢复当前实例改名前的原生名字。
- **关闭 / Escape**：关闭窗口；未点击保存的名字输入不会提交。

窗口需要 `Enabled=1`；设置优先伴侣还需要 `IgnoreExistingMarriageOnCommit=1`。自定义名字也会显示在伴侣美容选择列表中。游戏中的名字采用客户端实例显示名，已打开的交互界面可能需要关闭后重新打开才能刷新；对象尚未出场时，会在召唤后应用。

名字统一保存在插件旁的 `CohabitationGateExperiment.ini` 的 `[PartnerNames]` 分区，不新增配置文件。按当前可操作角色、NPC 保存键和角色模板编号区分，重启、读档后自动恢复；相同三项编号的其他存档也会沿用该名字。名字不写入原版存档；关闭总开关会还原当前实例的显示名，但保留自定义配置，重新开启后继续应用。

窗口打开后如果切换角色、读档或婚姻列表发生变化，会禁用操作并提示重新打开。游戏更新后无法核对名字接口时，改名按钮会禁用，优先选择功能仍可使用。

## 弹窗语言

伴侣管理和伴侣美容选择窗口顶部都有“语言 / Language”下拉框，可以选择“简体中文”或“English”。两个窗口共用设置，切换后立即更新标题、按钮和提示，并保留选中对象、列表位置及尚未保存的名字输入。切换只影响插件界面，伴侣原名、自定义名字和游戏语言保持原样。

语言保存在现有 `CohabitationGateExperiment.ini`，不新增配置文件：

```ini
[UI]
; zh-CN 简体中文；en-US English。
Language=zh-CN
```

旧 INI 没有此项或值无效时使用中文，无需替换配置。手动修改此项会随约 5 秒的配置刷新更新窗口。保存失败时保持原语言并显示错误；焦点位于语言下拉框时，Enter 不会保存名字或确认进入美容。


## 运行要求与兼容范围

- Windows x64，以及包含对应恋爱 / 婚姻系统的游戏内容。
- 兼容的 x64 ASI Loader。本项目使用过 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) 的 x64 `winmm.dll`。
- v0.1.3 发布检查使用游戏 EXE **1.0.0.3527 / Steam build 26423880**；此前版本的分析基于 1.0.0.3462。其他版本需要以实际定位日志为准。

插件在启动时自动查找函数并检查结构。地址移动和部分虚表槽位变化可以自动处理；函数改名、逻辑或编译方式变化仍可能需要更新补丁。无法确认目标时停止对应功能，不强行写入未知位置。

## 下载与安装

### 1. 下载发布文件

打开 [v0.1.3 Release](https://github.com/wumoin/CrimsonDesert/releases/tag/v0.1.3)，下载附件：

- [CohabitationGateExperiment.asi](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.3/CohabitationGateExperiment.asi)：插件。
- [CohabitationGateExperiment.ini](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.3/CohabitationGateExperiment.ini)：首次安装使用的默认配置。
- [winmm.dll](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.3/winmm.dll)：首次安装使用的 x64 ASI Loader，来自 Ultimate ASI Loader v10.0.2；已有兼容加载器可继续使用。
- 附件中的 `MinHook-LICENSE.txt` 和 `Ultimate-ASI-Loader-LICENSE.txt` 为第三方许可证，再分发时一并保留。


### 2. 退出游戏并备份存档

完整退出游戏，复制保存以下目录中的存档：

```text
%LOCALAPPDATA%\Pearl Abyss\CD\save
```

在副本或实验进度上使用。关闭补丁不会撤销游戏已经保存的关系变化。

### 3. 安装加载器和补丁


首次安装时，把 `.asi`、`.dll` 和 `.ini` 放到游戏的 `bin64` 目录。盘符和 Steam 库路径以自己的安装位置为准：

```text
Crimson Desert/
└── bin64/
    ├── CrimsonDesert.exe
    ├── winmm.dll
    ├── CohabitationGateExperiment.asi
    └── CohabitationGateExperiment.ini
```

随后启动游戏并读取存档，按 F8 打开伴侣管理。如需开启或关闭其他功能，可修改 `CohabitationGateExperiment.ini`。


## 配置说明

所有开关使用 `1` 开启、`0` 关闭；`PreferredPartnerKey` 是对象编号，`Language` 是语言代码。`Enabled` 控制全部功能。开关、优先编号和语言约每 **5 秒**重新读取；在弹窗内操作会立即生效并保存。手动编辑 `[PartnerNames]` 后需重启插件，建议通过 F8 修改名字。

### 完整配置示例

发布配置中所有开关均为 `1`，`PreferredPartnerKey=0`，`Language=zh-CN`，自定义名字初始为空。

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
; 0 自动选择当前伴侣；也可在游戏中按 F8 设置已婚对象。
PreferredPartnerKey=0
; 点击伴侣美容时先选择已婚对象。
BeautyPartnerPicker=1
; 全员召唤或收回；读档时恢复已设为同行的伴侣。
SpawnAllPartners=1

[UI]
; zh-CN 简体中文；en-US English。
Language=zh-CN

[PartnerNames]
; 自定义名字由 F8 窗口保存；重置会删除对应项。
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
| `Relationships` | `PreferredPartnerKey` | `0` | `0` 自动选择；非零值指定当前角色的一位真实已婚对象；F8 窗口可直接设置 |
| `Relationships` | `BeautyPartnerPicker` | `1` | 点击伴侣美容后显示插件选择窗口，支持双击、确定和取消 |
| `UI` | `Language` | `zh-CN` | 两个弹窗共用界面语言，可设置为 `zh-CN` 或 `en-US` |
| `Relationships` | `SpawnAllPartners` | `1` | 全员召唤 / 收回，并在读档时分别恢复已保存为同行的伴侣 |

多人同行恢复需要 `Enabled=1`、`SpawnAllPartners=1`，并在加载日志中看到 `PARTNER RESTORE HOOK INSTALLED` 和 `PartnerRestoreActive=1`。先对需要跟随的伴侣分别选择“结伴同行”并保存，再读档；选择“等待”的对象保持原状态。读档后日志 `PARTNER RESTORE` 中的 `followRequests` 表示原版请求恢复同行的人数，实际出场和跟随仍需游戏内确认。入口无法兼容时会记录 `PARTNER RESTORE UNAVAILABLE` 并保留原版恢复行为。游戏已读档后才开启开关，需要重新读档才能触发。

表中是发布 INI 的默认值。删除配置文件或键时，插件仍使用关闭兜底（仅同居子开关缺省为 `1`），请保留完整配置。

五项条件覆盖作用于对应条件的所有调用，其他任务或互动也可能受到影响。它们不会跳过所有剧情或关系阶段，也不会为缺少婚礼内容的 NPC 自动补齐脚本。


## 实现原理

补丁处理以下环节，实际婚姻状态仍由游戏原版流程写入：

1. **条件判断**：通过 MSVC RTTI 查找五类条件的虚函数表，解析实际评估槽，再替换进程内入口。保留上下文不可用和未知返回值。
2. **婚姻提交与当前对象**：用 MinHook 挂钩原资格检查。仅在原检查失败、且开启对应选项时，构造排除当前角色已有婚姻的私有副本重新检查；不改原关系数组。跟踪真实写入路径，并且只选择已存在的真实已婚记录。
3. **出场与离婚保护**：读取当前玩家的角色注册表，按对象键、所属角色和实例核对出场。在原游戏调用线程中修复运行时实例关联，调用原版动作并保留原生回执；离婚使用明确交互目标及独立记录快照。实例关联字段会改变，婚姻状态仍交给原版写入。
4. **读档恢复同行**：临时逐一指定真实已婚对象，重用原版恢复函数读取各自保存的同行标志；不改写同行标志，不强制等待中的对象跟随。
5. **管理窗口与显示名**：F8 窗口只列出真实已婚对象；设置优先编号重用现有路由，名字覆盖应用于客户端实例。美容选择只在原版美容查询作用域内临时指定对象；中英文切换仅改变插件文案。

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

`build` 还会包含编译中间文件，不纳入 Git。脚本只构建，不启动游戏、不安装插件；需要按安装章节手动复制发布文件。回归测试见 [tests/README.md](tests/README.md)，可运行 `tests\run.cmd`；传入游戏 EXE 路径时额外执行离线定位检查，不执行游戏入口。测试程序不随 Release 分发。

## 项目目录与依赖

```text
src/          插件源码、自动定位和关系处理
config/       带中文注释的默认配置
docs/         实现原理、验证记录和依赖来源
tests/        独立原生窗口、配置持久化和离线定位回归
evidence/     历史插件日志摘录
vendor/       MinHook 必要源码与上游许可证
build.cmd     Windows x64 构建脚本
```

- [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4 静态编译进插件，上游及 HDE 许可证见 [vendor/minhook/LICENSE.txt](vendor/minhook/LICENSE.txt)。二进制分发需保留 `MinHook-LICENSE.txt`。
- Release 附带 Ultimate ASI Loader v10.0.2 的 x64 `winmm.dll` 和 MIT 许可证，源码仓库不跟踪该二进制，`build.cmd` 也不会生成它。
- 本仓库不包含游戏资源、游戏二进制、第三方存档、存档密钥或私有研究工具。

更多来源与版本信息见 [依赖说明](docs/dependencies.md)。
