# Crimson Desert 婚姻条件实验补丁

为《Crimson Desert》提供可配置的恋爱与婚姻条件覆盖，支持继续与其他对象结婚、选择当前伴侣，以及实验性的全员召唤 / 收回。Windows x64 ASI 插件，配置和源码注释为中文。

**当前为实验版本。** 已在用户存档中确认同一角色保存四条已婚记录；多人同时出场、跟随、婚礼对象正确性、读档后的完整互动及离婚尚未完成实机验证。

[下载 v0.1.0](https://github.com/wumoin/CrimsonDesert/releases/tag/v0.1.0) · [所有版本](https://github.com/wumoin/CrimsonDesert/releases) · [默认配置](config/CohabitationGateExperiment.ini) · [反馈问题](https://github.com/wumoin/CrimsonDesert/issues)

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

这些开关各自处理不同环节。仅解除菜单条件，并不等于关系已经写入；保存多条婚姻记录，也不等于多人已经同时出场。插件没有新增伴侣列表界面或额外快捷键。

## 运行要求与兼容范围

- Windows x64，以及包含对应恋爱 / 婚姻系统的游戏内容。
- 兼容的 x64 ASI Loader。本项目使用过 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) 的 x64 `winmm.dll`。
- 本次分析与离线适配基于游戏 EXE **1.0.0.3462 / Steam build 26356046**；其他版本需要以实际定位日志为准。

插件不按精确游戏版本号拒绝运行，而是在启动时自动查找函数并检查结构。地址移动和部分虚表槽位变化可以自动处理；函数改名、逻辑或编译方式变化仍可能需要更新补丁。无法确认目标时停止对应功能，不强行写入未知位置。

## 下载与安装

### 1. 下载发布文件

打开 [v0.1.0 Release](https://github.com/wumoin/CrimsonDesert/releases/tag/v0.1.0)，下载附件：

- [CohabitationGateExperiment.asi](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.0/CohabitationGateExperiment.asi)：插件。
- [CohabitationGateExperiment.ini](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.0/CohabitationGateExperiment.ini)：配置。
- [MinHook-LICENSE.txt](https://github.com/wumoin/CrimsonDesert/releases/download/v0.1.0/MinHook-LICENSE.txt)：依赖许可证，分发插件时需保留。

GitHub 自动生成的 `Source code` 压缩包只包含源码，不能代替 `.asi` 附件。当前版本标记为预发布，请从上面的版本页面或所有版本页面下载。

### 2. 退出游戏并备份存档

完整退出游戏，复制保存以下目录中的存档：

```text
%LOCALAPPDATA%\Pearl Abyss\CD\save
```

在副本或实验进度上使用。关闭补丁不会撤销游戏已经保存的关系变化。

### 3. 安装加载器和补丁

首次安装时，从 [Ultimate ASI Loader 官方 Releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) 获取 **x64 `winmm.dll`**，放到 `CrimsonDesert.exe` 同目录。加载器的安装方式参见[官方说明](https://github.com/ThirteenAG/Ultimate-ASI-Loader#installation)。已有加载器时确认它能加载本插件，保留原有配置，避免直接覆盖其他 Mod 所需的文件。

把本补丁的三个附件放到游戏的 `bin64` 目录。常见布局如下，盘符和 Steam 库路径以自己的安装位置为准：

```text
Crimson Desert/
└── bin64/
    ├── CrimsonDesert.exe
    ├── winmm.dll
    ├── CohabitationGateExperiment.asi
    ├── CohabitationGateExperiment.ini
    └── MinHook-LICENSE.txt
```

本文使用上述同目录布局。插件总是读取**与自身 `.asi` 同目录、同名的 `.ini`**，日志也写在这里；修改源码仓库里的配置不会自动影响已安装的插件。

### 4. 开启需要的功能

发布附件默认 `Enabled=0`，安装后需要编辑游戏目录中的 INI，将其设为 `1`，再按下节选择功能。

更新旧版本时，完整退出游戏后替换 `.asi`，并在现有 INI 中补齐新增选项。只修改 INI 无法让旧 `.asi` 获得新功能；直接覆盖 INI 则会重置自己的配置。

## 配置说明

所有开关使用 `1` 开启、`0` 关闭；`PreferredPartnerKey` 是对象编号，不是开关。`Enabled` 控制全部功能。运行期间约每 **5 秒**重新读取配置，不需要重启游戏；更新 `.asi` 本身则必须退出游戏。

### 完整配置示例

以下示例开启五项条件覆盖和多次婚姻提交；全员召唤保持关闭，可在婚礼 CG 结束后另行开启。这是实验用配置，**不是附件默认值**：

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
SpawnAllPartners=0
```

在现有对应分区中修改键值，不要重复创建同名分区或同名键。

### 所有选项与默认值

| 分区 | 配置项 | 附件默认值 | 开启或设置后的作用 |
| --- | --- | --- | --- |
| `Experiment` | `Enabled` | `0` | 启用下方所选功能；设为 `0` 关闭全部覆盖 |
| `Conditions` | `IgnoreFriendlyLevel` | `0` | 按虚拟高等级判断好感度门槛，实际好感度不变 |
| `Conditions` | `IgnoreMarriageable` | `0` | 绕过对方是否可结婚的判断 |
| `Conditions` | `IgnoreLivingTogether` | `1` | 绕过已有同居对象的入口限制 |
| `Conditions` | `IgnoreAffinityLevel` | `0` | 按虚拟高等级判断亲密度门槛，实际亲密度不变 |
| `Conditions` | `IgnoreTribe` | `0` | 绕过种族条件，实际种族不变 |
| `Relationships` | `IgnoreExistingMarriageOnCommit` | `0` | 提交婚姻时排除自己现有婚姻的阻碍，并启用当前伴侣选择 |
| `Relationships` | `PreferredPartnerKey` | `0` | `0` 自动选择；非零值指定当前角色的一位真实已婚对象 |
| `Relationships` | `SpawnAllPartners` | `0` | 触发原版召唤 / 收回时，把同样动作扩展到其他真实已婚对象 |

总开关默认关闭，所以 `IgnoreLivingTogether=1` 不会在刚安装时自行生效。缺少配置键时也使用表中的默认值。

五项条件覆盖作用于对应条件的所有调用，其他任务或互动也可能受到影响。它们不会跳过所有剧情或关系阶段，也不会为缺少婚礼内容的 NPC 自动补齐脚本。

### 只想解除已有伴侣限制

保留其他条件开关为自己的设置，至少开启：

```ini
[Experiment]
Enabled=1

[Conditions]
IgnoreLivingTogether=1

[Relationships]
IgnoreExistingMarriageOnCommit=1
PreferredPartnerKey=0
```

这两项分别处理交互入口和实际提交环节。仍需满足没有被其他开关覆盖的好感、亲密度、剧情与关系阶段要求；重复与同一已婚对象结婚、目标已婚于其他可操作角色等检查仍保留。

### 切换当前伴侣

需要 `Enabled=1`、`IgnoreExistingMarriageOnCommit=1`，并且日志显示 `CommitOverrideActive=1`，手动选择才会生效。

- `PreferredPartnerKey=0`：本进程成功进入婚姻写入路径后，优先选择该目标；必须已经出现它真实的已婚记录。
- 非零值：填写日志中该已婚对象的 `npcSaveKey` / `selectedKey`，例如 `PreferredPartnerKey=1172`。该数字仅为格式示例，需换成自己存档中的编号。
- 编号必须属于当前可操作角色的真实已婚记录。它不是 NPC 名称、角色模板编号或存档槽号；无效、未婚或其他角色的对象不会被强制选择。

手动值优先于自动目标，**再次求婚前改回 `0`**，避免场景仍优先选择手动指定的旧伴侣。自动偏好不写入存档，也不与读档内容绑定；重启后不会恢复，读档或切换进度后不要依赖旧偏好，必要时指定已确认的对象编号。关闭提交覆盖也会清除自动偏好。

该选项影响使用原版公共伴侣查询的场景；不会删除其他关系，也不会新增完整伴侣列表 UI。

### 让其他伴侣一起召唤 / 收回

在现有 `[Relationships]` 中设置：

```ini
SpawnAllPartners=1
```

保持 `Enabled=1`，并确认日志显示 `SpawnAllActive=1`。此项可独立于 `IgnoreExistingMarriageOnCommit` 开启，只处理已存在的婚姻记录。

修改后不会立即生成其他人。先在婚礼 CG 结束后的同居 / 召唤场景，执行一次原版收回后再召唤，或重新进入会触发原版出场的场景。当前对象动作成功后，插件才会对其余已婚对象逐个执行同样动作；不会为未婚对象创建关系。

位置、碰撞、跟随、场景人数限制和实际渲染尚未完成实机验证。该入口可能被剧情复用，婚礼或其他剧情期间可先关闭此项。关闭选项本身不会立即收回已经出场的角色；如需收回全员，应在仍开启时先执行原版收回。

## 在游戏中使用

1. 配置并保存 INI，正常通过 Steam 启动游戏，载入实验进度。
2. 按原版方式与 NPC 互动、示爱和求婚。插件自动参与条件判断，无需控制台命令或快捷键。
3. 运行中修改配置后等待约 5 秒，退出当前交互并重新进入，让游戏再次检查条件。
4. 求婚和 CG 结束后保存进度，再核对关系是否保留。多人出场需要另行开启 `SpawnAllPartners` 并触发召唤流程。

**示爱和求婚是不同流程。** 已分析的示爱分支使用好感等级，求婚分支使用亲密度等级，另外还有关系阶段要求。开启条件覆盖不会直接把“未交往”改为“已交往”，也不会直接把某个 NPC 写成已婚。

成功对话、播放 CG、关系保存和实际出场分别发生在不同环节，不能只凭其中一个现象认定完整多伴侣流程已经成功。

## 日志与排错

日志文件为插件旁边的 `CohabitationGateExperiment.log`，持续追加，包含时间和进程 ID（PID）。多个进程加载 ASI 时可能写入同一文件，应按**同一 PID 的本次启动记录**判断结果。

### 判断功能是否实际启用

| 日志 | 含义 |
| --- | --- |
| `RESOLVED`、`PASS` | 五种条件的目标已定位并通过兼容检查 |
| `INSTALLED: five condition evaluator wrappers` | 五项条件挂钩安装完成，是否覆盖仍取决于配置 |
| `STATUS Enabled=1 ...` | 已读取总开关和各子开关；状态通常只在启动或设置变化时输出 |
| `CommitOverrideActive=1` | 婚姻提交覆盖及当前伴侣选择已实际启用 |
| `SpawnAllActive=1` | 全员召唤 / 收回扩展已实际启用 |
| `REFUSED` | 条件定位或安装失败，该进程没有启用条件覆盖；原因见同一行 |
| `COMMIT HOOK UNAVAILABLE` | 提交与伴侣选择挂钩不可用；已安装的五项条件覆盖仍可运行 |
| `PARTNER SPAWN HOOK UNAVAILABLE` | 出场扩展不可用；其他已安装功能仍可运行 |

INI 中请求 `1` 不等于已经成功安装。五项条件必须全部定位、安装成功，之后才尝试提交与出场挂钩；出场挂钩单独失败不会关闭已安装的其他功能。单纯修改配置不会重新尝试失败的定位，更新兼容版本后需重启游戏。

### 判断婚姻提交和对象选择

| 日志 | 含义 |
| --- | --- |
| `COMMIT RECHECK` | 原资格函数接受了排除现有伴侣后的私有视图，后续仍由游戏写入 |
| `COMMIT PREFLIGHT` | 菜单 / 预检接受目标，尚未改变伴侣选择 |
| `COMMIT TARGET` | 真正写入路径接受目标，日志给出 `npcSaveKey`，仍等待原版已婚记录 |
| `COMMIT TARGET REJECTED` | 写入路径仍被原资格检查拒绝 |
| `PARTNER ROUTE` | 已找到所选目标真实的已婚记录；`selectedKey` 为所选编号，`marriedCandidates` 为已观察到的当前角色已婚记录数 |
| `PARTNER ROUTE WAIT` | 未找到唯一、有效的目标已婚记录，保留原版选择 |

`COMMIT TARGET` 不是最终保存成功的证明，`marriedCandidates` 也只反映当时缓存。确认保存需要进一步核对 CG 后的新存档及重新载入后的关系。

### 判断全员召唤

```text
[pid=...] STATUS ... SpawnAllPartners=1 SpawnAllActive=1
[pid=...] PARTNER ACTION: spawn=1 ... marriedRecords=4 extrasAttempted=3 extrasAccepted=3; primary result preserved.
```

- `spawn=1` 为召唤，`spawn=0` 为收回。
- `marriedRecords` 是当次获取的真实已婚记录数。
- `extrasAttempted` 是除当前对象外尝试处理的数量。
- `extrasAccepted` 只表示额外原版调用返回成功，不保证角色已渲染或所有后续回调完成。
- `PARTNER ACTION EXTRA FAILED` 提供失败对象编号和原版结果码。额外对象失败时继续处理其余对象，保留当前对象原本的结果。

### 常见问题

| 现象 | 检查方法 |
| --- | --- |
| 完全没有插件日志 | 检查是否下载了 `.asi`、加载器是否为 x64、是否放在 EXE 同目录及目录是否可写 |
| 修改 INI 没反应 | 确认改的是已安装 `.asi` 旁的同名文件，`Enabled=1`；等待 5 秒后重新触发交互 |
| 主游戏成功安装，另一个 PID 报类型不存在 | 可能是启动器或子进程不含对应类型，以主游戏 PID 的完整记录为准 |
| 可以求婚但被拒绝 | 检查亲密度开关、是否完成示爱及关系阶段；其他剧情和 NPC 内容要求仍可能阻止成功 |
| 能反复播放交互，但新对象没有成为伴侣 | 检查 `IgnoreExistingMarriageOnCommit`、`CommitOverrideActive`、`COMMIT TARGET` 和后续真实关系记录 |
| CG 或当前伴侣仍是旧对象 | 再次求婚前将 `PreferredPartnerKey` 改回 `0`，检查新对象是否出现 `PARTNER ROUTE`；婚礼对象完整行为仍待验证 |
| 存档有多条婚姻，家中仍只出现一人 | 检查 `SpawnAllActive=1`，重新触发召唤并查看 `PARTNER ACTION`；现有关系记录不等于全员出场已成功 |
| 更新游戏后出现 `REFUSED` | 保留同一 PID 的完整日志和游戏版本信息，到 [Issues](https://github.com/wumoin/CrimsonDesert/issues) 反馈；不应通过删除兼容检查强行运行 |

反馈时附上使用的 Release / 提交版本、游戏版本、相关配置、问题发生前后的完整日志，以及是否已婚、是否完成示爱、在哪个场景触发。通常无需先公开上传个人存档。

## 关闭、更新与卸载

- **临时关闭**：将 `Enabled=0`，等待约 5 秒后恢复原版判断、伴侣选择和后续召唤行为。进程内挂钩仍保留到退出游戏，不撤销已经完成的动作。
- **单独关闭**：把对应功能设为 `0`。`IgnoreExistingMarriageOnCommit=0` 同时停用当前伴侣选择，`SpawnAllPartners` 可独立保留。
- **更新插件**：完整退出游戏后替换 `.asi`，合并新增 INI 选项。不要在游戏运行时替换或尝试卸载模块。
- **卸载**：完整退出后移走 `.asi` 和 `.ini`，日志可自行删除。只有确认没有其他 Mod 使用加载器时，才一起移走 `winmm.dll`。

以上操作不会自动解除婚姻、恢复之前的数值或撤销保存的关系变化；需要还原进度时使用备份存档。

## 实现原理

补丁处理三个环节，实际婚姻状态仍由游戏原版流程写入：

1. **条件判断**：通过 MSVC RTTI 查找五类条件的虚函数表，解析实际评估槽，再替换进程内入口。保留上下文不可用和未知返回值。
2. **婚姻提交与当前对象**：用 MinHook 挂钩原资格检查。仅在原检查失败、且开启对应选项时，构造排除当前角色已有婚姻的私有副本重新检查；不改原关系数组。跟踪真实写入路径，并且只选择已存在的真实已婚记录。
3. **出场扩展**：当前对象的原版召唤 / 收回成功后，临时逐个选择其他真实已婚对象，在原游戏线程中调用同一原版动作，保留主动作的结果。

好感 / 亲密度覆盖使用虚拟等级 `127` 重新计算原来的比较，保留 `>=`、`<` 等方向，避免成功与拒绝分支同时成立。它不会增加实际数值，也不是让所有比较无条件成立，例如“等级恰好等于 3”仍不成立。

插件修改运行中的内存入口，不修改磁盘上的游戏 EXE、资源包或存档格式。游玩触发的原版关系更新和出场动作仍可能改变正常保存的数据。自动定位检查结构与语义模式，不保证未来所有游戏更新均无需适配。

详细说明见 [实现说明](docs/design.md)。

## 已验证与未验证

| 范围 | 当前证据 |
| --- | --- |
| 自动定位与条件覆盖逻辑 | 在 1.0.0.3462 上完成离线映射；虚表移动 / 槽位变化、缺失 / 重复目标、等级比较与配置组合均有离线验证 |
| 运行中加载与条件挂钩 | 用户日志确认五项条件安装，以及提交 / 对象选择路径运行 |
| 多条婚姻写入并保存 | 用户进度的同一角色同时保存四条状态为已婚的记录，原伴侣记录保留 |
| 全员召唤 / 收回实现 | 离线模型验证逐对象调用、关系过滤、递归抑制及失败处理；尚未实机确认全员同时出现 |
| 完整多伴侣玩法 | 婚礼对象、位置、碰撞、跟随、场景人数限制、读档后交互与离婚仍需验证 |

256 种总开关 / 条件 / 提交 / 出场配置组合已通过离线验证。离线模型与真实保存证据的具体范围见 [验证记录](docs/validation.md)；不能把挂钩安装成功或原生调用返回成功当作完整玩法已经验证。

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
- ASI Loader 需从上游单独获取，本仓库和 Release 不包含加载器。
- 本仓库不包含游戏资源、游戏二进制、第三方存档、存档密钥或私有研究工具。

更多来源与版本信息见 [依赖说明](docs/dependencies.md)。
