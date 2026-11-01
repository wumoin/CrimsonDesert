# Crimson Desert 婚姻条件实验补丁

Windows x64 ASI 插件，支持通过 INI 分别忽略好感等级、亲密度等级、种族、可结婚判断和已有同居对象限制，并在启动时自动定位对应函数。

另提供独立的婚姻提交资格覆盖：原版因当前角色已有其他已婚对象而拒绝时，用私有关系视图重新检查。该功能默认关闭，不强制把提交改成成功。

开启提交覆盖时还会跟踪实际写入路径的目标。后续公共伴侣查询只有找到该目标真实的已婚记录，才优先选择它，避免始终返回第一位伴侣；不会为 CG 凭空创建第二位伴侣。

另有默认关闭的 SpawnAllPartners：原版召唤 / 收回当前对象成功后，对其余真实已婚对象执行同样的原生动作，用于扩展出场层。四条婚姻记录并存已在用户存档确认，但新出场功能尚未实机确认全员可同时出现。

**当前仍是实验补丁，完整多配偶行为尚未验证。** 它覆盖条件判断，不直接增加配偶、好感数值或修改存档格式。

## 原理与配置范围

部分恋爱入口包含以下条件：

```text
人类 && 好感等级 >= 2 && 可结婚 && !HasLivingTogether()
```

插件先调用原评估函数，再按配置覆盖有效结果。引擎使用 **0=满足、1=不满足、2=上下文不可用**。

| INI 子开关 | 对应条件 | 开启时的覆盖 |
| --- | --- | --- |
| IgnoreFriendlyLevel | CheckFriendlyLevel | 使用虚拟等级 127 重新计算好感/信任等级比较 |
| IgnoreMarriageable | IsMarriageable | 1 → 0，让可结婚判断通过 |
| IgnoreLivingTogether | HasLivingTogether | 0 → 1，让外层“没有同居对象”条件通过 |
| IgnoreAffinityLevel | CheckAffinityLevel | 使用虚拟等级 127 重新计算亲密度等级比较 |
| IgnoreTribe | CheckTribe | 1 → 0，让种族条件通过，包括人类要求 |

返回值 2 和未知值始终保留。总开关关闭或对应子开关关闭时，原样返回。

等级覆盖会保留比较方向，例如“亲密度 >=3”成立、“亲密度 <3”不成立。旧版直接让所有等级条件通过，会让成功与拒绝分支同时成立；当前版本已修正。虚拟等级只用于条件计算，不写入存档，也不代表游戏正常可达到的等级。

**子开关作用于对应条件类型的所有调用，不仅是婚姻菜单。** 好感、亲密度和种族条件也可能用于其他任务或交互。开关只覆盖判断，不增加实际好感/亲密度，也不改变角色种族；剧情和婚姻阶段检查仍保留。绕过种族或可结婚检查不会自动为 NPC 补齐婚姻交互、任务或婚礼脚本。

## 自动定位与更新兼容性

插件不再保存类型、虚函数表、评估函数的固定 RVA，也不固定评估槽号：

1. 在主模块可读区域查找五个目标类的 RTTI 类型名称。
2. 根据 MSVC x64 RTTI 结构找到各自的主虚函数表。
3. 从虚函数分派代码解析实际评估槽号。
4. 检查目标唯一性、内存范围、函数边界与返回值相关的指令模式，再安装挂钩。

这样可以处理地址移动和已支持的分派模式下的槽号变化。**不能保证所有未来版本都自动兼容**：类型被移除/改名、编译器改变分派模式、条件语义改变时，仍需人工分析。目标缺失、不唯一或不兼容时拒绝安装全部挂钩。

## 构建

需要 Windows x64、Visual Studio C++ x64 工具链和 Windows SDK。在仓库目录运行：

```bat
build.cmd
```

输出位于 build，包含 CohabitationGateExperiment.asi 和默认关闭的 INI。脚本不会安装插件或启动游戏。编译使用 /utf-8，INI 注释为中文。

提交层挂钩所需的 MinHook 已静态编译进 .asi，不需要额外安装 DLL。构建同时输出 MinHook-LICENSE.txt，分发插件时请一起保留。

## 使用方法

### 1. 准备存档和加载器

完整退出游戏，备份存档目录：

```text
%LOCALAPPDATA%\Pearl Abyss\CD\save
```

构建插件，首次安装还需从 [Ultimate ASI Loader 官方发布页](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) 获取 x64 winmm.dll。已有加载器时先检查兼容性，避免直接覆盖。

### 2. 安装或更新

将 build 中的 .asi 和 .ini 放到游戏 EXE 所在的 bin64。本机安装路径：

```text
D:\gametool\steam\steamapps\common\Crimson Desert\bin64\
├── CrimsonDesert.exe
├── winmm.dll
├── CohabitationGateExperiment.asi
└── CohabitationGateExperiment.ini
```

使用旧版插件的用户需要完整退出游戏后替换 .asi；只改 INI 不会让旧插件获得自动定位或新增的子开关。仓库里的构建输出不会自动覆盖游戏目录。

### 3. 配置要忽略的条件

编辑**游戏 bin64 中**的 CohabitationGateExperiment.ini。例如，要忽略五项条件：

```ini
[Experiment]
; 总开关：1 为启用，0 为关闭全部覆盖。
Enabled=1

[Conditions]
; 忽略好感/信任等级判断，不增加实际数值。
IgnoreFriendlyLevel=1
; 忽略可结婚判断。
IgnoreMarriageable=1
; 忽略已有同居对象限制。
IgnoreLivingTogether=1
; 忽略亲密度等级条件，不增加实际亲密度。
IgnoreAffinityLevel=1
; 忽略种族条件（包括人类要求），不改变角色种族。
IgnoreTribe=1
```

每项可独立设为 0 或 1。总开关必须为 1 才执行覆盖。

仓库默认总开关关闭，只有 IgnoreLivingTogether 默认为 1，其余四个子开关默认均为 0。旧 INI 未提供子开关时也使用这些默认值，以保持原来的同居条件覆盖行为。

只想新增亲密度与种族覆盖时，在现有 [Conditions] 中增加 IgnoreAffinityLevel=1 和 IgnoreTribe=1，并确保 [Experiment] 的 Enabled=1。需要更新 .asi，旧三条件版不会识别这两个新开关。

如果已经能触发成功交互，但原伴侣还在、新 NPC 没有加入，可单独启用提交资格覆盖。在现有 INI 追加：

```ini
[Relationships]
; 只在原版因现有婚姻而失败后，用私有关系视图重新检查。
IgnoreExistingMarriageOnCommit=1
; 0：新婚时跟随本次真正写入路径的目标。
PreferredPartnerKey=0
```

总开关 Enabled 必须为 1；通常还需启用 IgnoreLivingTogether，才能放开交互入口。新键默认为 0，旧版 .asi 不支持它，需要完整退出后更新插件。该开关按原版规则继续拒绝同一已婚目标、目标已婚于其他可操作角色等情况，不直接增加伴侣或写存档。仍可能被后续检查拒绝。

**新婚场景自动选人时保持 PreferredPartnerKey=0。** 需要在已经真实结婚的对象之间切换时，可以将它设为日志中的 selectedKey。该选择只接受当前可操作角色下状态为已婚的记录。非 0 的指定值优先于自动目标，再次求婚前应改回 0。

自动目标偏好只保留在本进程内，不写存档；重启或读档后若需要固定某位已有伴侣，可在 INI 指定其记录键。它控制所有使用原版公共伴侣查询的流程，包括部分场景、同居/召唤等，不会删除其他伴侣的关系。完整列表 UI 和离婚交互尚未完成验证。

如果家中 / 同居 / 召唤仍只出现最新一人，在现有 [Relationships] 中增加：

```ini
; 原版主对象动作成功后，对所有其他真实已婚对象执行同样动作。
SpawnAllPartners=1
```

总开关 Enabled 需为 1，并在日志中确认 SpawnAllActive=1。它可与 IgnoreExistingMarriageOnCommit 分别启用：只召唤已保存的伴侣，不需要同时放开新的婚姻提交。

**只修改 INI 不会立即在场景里生成其他人。** 更新 .asi 后，重新进入会触发原版出场流程的场景，或执行一次原版收回 / 召唤。如果先前只有一人已出场，可先收回再召唤，以触发扩展入口。

主对象动作失败时不扩展，保留原版场景限制。其他对象单独失败会记录原生错误并继续处理其余对象，不会把主动作改成成功或失败。位置、碰撞、跟随、场景人数限制和读档后出场仍需实机验证；完整伴侣列表 UI 没有新增。

先在婚礼 CG 结束后的同居 / 召唤场景测试此项。该原生入口可能也被剧情复用，目前没有按剧情来源进一步区分；婚礼或其他剧情期间可先关闭这项出场扩展，关系记录不会因此被删除。

### 4. 在游戏中使用

正常通过 Steam 启动并载入实验存档，按原版方式与 NPC 交互，尝试恋爱或求婚流程。插件会自动参与条件评估，不需要输入命令或按额外快捷键。

**示爱与求婚不是同一个交互。** 数据中分别存在 Affinity_Start 和 Propose 的成功/失败分支，前者使用好感等级比较，后者使用亲密度等级比较，还检查不同的关系阶段。补丁覆盖这些流程共用的条件，不会直接把关系状态改成“已交往”或“已婚”。

如果使用旧版五条件插件时出现“可以选择求婚，但对方拒绝并扣好感”，需要完整退出后替换修正比较逻辑的新版 .asi，现有 INI 键可以继续使用。新版本首条加载日志包含 comparison-preserving level overrides。该修正针对已确认的等级分支冲突，尚未实机确认能解决所有 NPC 的求婚失败。

若要观察已有同居对象的限制是否解除，需要已同居进度以及另一个测试对象。若要观察好感条件覆盖，可在好感不达标的实验进度中对照开关前后的交互。

游戏运行时约每 5 秒重新读取 INI，包括新增提交层开关。修改后退出当前 NPC 交互并再次交互，可重新触发检查。未启用覆盖的条件、剧情、恋爱阶段以及其他婚姻写入逻辑仍可能阻止后续流程。

### 5. 查看加载结果

查看 bin64 中的 CohabitationGateExperiment.log。当前版本会输出每个条件的自动定位结果以及进程 ID：

```text
[pid=...] RESOLVED: CheckFriendlyLevel ... slot=... evaluator RVA=...
[pid=...] RESOLVED: IsMarriageable ... slot=... evaluator RVA=...
[pid=...] RESOLVED: HasLivingTogether ... slot=... evaluator RVA=...
[pid=...] RESOLVED: CheckAffinityLevel ... slot=... evaluator RVA=...
[pid=...] RESOLVED: CheckTribe ... slot=... evaluator RVA=...
[pid=...] PASS: all five unique RTTI/vtable/dispatch targets and evaluator compatibility checks.
[pid=...] INSTALLED: five condition evaluator wrappers. INI reloads every 5 seconds.
[pid=...] STATUS Enabled=1 IgnoreFriendlyLevel=1 IgnoreMarriageable=1 IgnoreLivingTogether=1 IgnoreAffinityLevel=1 IgnoreTribe=1
```

五个条件全部定位成功才安装。任一槽安装失败会尝试撤回之前的槽，覆盖保持关闭。启动器或其他子进程可能没有这些类型，需根据同一 PID 的记录判断主游戏是否成功安装。

INSTALLED 和 Enabled=1 只能证明挂钩及开关状态，不代表第二次结婚已经验证成功。没有日志时先检查安装目录与 ASI Loader；REFUSED 会写明定位失败的具体原因。

提交层挂钩独立于上面五项。需要看到以下记录，才能确认新开关真正启用：

```text
[pid=...] COMMIT RESOLVED: eligibility RVA=... arrays=... stride=... state=... playable=...
[pid=...] COMMIT HOOK INSTALLED: original eligibility plus private-view recheck; override defaults off.
[pid=...] PARTNER ROUTE RESOLVED: lookup RVA=... writer RVA=... key offsets=...
[pid=...] PARTNER ROUTE INSTALLED: native write target is preferred only after its married record exists.
[pid=...] STATUS ... IgnoreExistingMarriageOnCommit=1 CommitOverrideActive=1 PreferredPartnerKey=0
```

若出现 COMMIT HOOK UNAVAILABLE，提交覆盖不启用，五项条件开关仍可独立运行。CommitOverrideActive=0 表示提交覆盖未实际开启，即使 INI 请求为 1 也不会强行放行。

COMMIT RECHECK 只表示原资格函数接受了重新检查的私有视图，不代表后续状态写入和保存已经成功。目标路由日志进一步区分：

- COMMIT PREFLIGHT：菜单 / 预检接受目标，没有改当前伴侣选择。
- COMMIT TARGET：真正的写入调用路径接受这个目标，但仍等待原版把它写成已婚。
- COMMIT TARGET REJECTED：该写入调用路径仍被原资格检查拒绝。
- PARTNER ROUTE：已找到目标的真实已婚缓存记录；nativeKey 是原版首选键，selectedKey 是本次选择键，marriedCandidates 是已观察到的当前角色已婚记录数。
- PARTNER ROUTE WAIT：目标不是唯一可验证的已婚记录，保持原版选择，不伪造成功。

即使看到 marriedCandidates=2，也只证明当时缓存有两条已婚记录；最终仍应以 CG 结束后的新存档及保存重载后两者都保留为准。完整伴侣列表 UI 尚未新增。

全员出场还需要以下记录：

```text
[pid=...] PARTNER SPAWN RESOLVED: control RVA=...
[pid=...] PARTNER SPAWN HOOK INSTALLED: native primary action plus optional married-partner fan-out; default off.
[pid=...] STATUS ... SpawnAllPartners=1 SpawnAllActive=1
[pid=...] PARTNER ACTION: spawn=1 ... marriedRecords=4 extrasAttempted=3 extrasAccepted=3; primary result preserved.
```

spawn=1 为召唤，spawn=0 为收回。extrasAccepted 只表示额外原生调用返回成功，不保证演员已渲染或全部后续回调完成；仍需观察实际场景。PARTNER ACTION EXTRA FAILED 会提供失败对象键和原生结果码。若出场入口不能兼容定位或安装，SpawnAllActive 保持 0，既有条件与提交功能仍可运行。

### 6. 关闭或卸载

运行期间将总开关改为 Enabled=0 并保存，约 5 秒后恢复原版判断；这也关闭提交层覆盖。单独将 IgnoreExistingMarriageOnCommit 改为 0 只关闭提交资格重新检查。内存挂钩在进程退出前仍保留，但关闭后使用原版结果。

卸载时完整退出游戏，再移走插件的 .asi 和 .ini。winmm.dll 仅在确认没有其他 Mod 依赖时一起移走。关闭/卸载不会撤销游玩时已经写入存档的关系变化，需要恢复进度时使用事先备份。

## 验证范围

- 旧诊断版本曾带插件进入游戏场景，并记录有效的“没有同居对象”返回。
- 三条件版和五条件版均已有用户提供的运行日志确认自动定位和挂钩安装成功，这不等于婚姻玩法验证。
- 当前五条件版在本机 EXE 1.0.0.3462 / Steam build 26356046 上通过了**离线映射验证**，五种评估函数均找到。
- 模拟虚表移动和槽号变化后可重新定位；类型缺失、关键指令模式变化、重复候选和非游戏宿主均拒绝定位。
- 中文 INI 读取、64 种总开关/子开关组合、五种条件的结果映射已离线验证。
- 等级覆盖已增加成功/拒绝互斥的回归验证，并从当前 EXE 自动恢复等级条件字段与六种比较运算；没有固定字段偏移或运算编号表。
- 新增提交资格函数与关系布局已离线自动定位；私有视图模型验证了现有伴侣排除、重复目标拒绝、其他角色关系保留和源数据不变。MinHook 在私有映像上安装 / 撤回并恢复了入口字节，没有执行游戏函数。
- 包括提交开关在内的 128 种总开关 / 子开关组合已离线验证。
- 对象路由模型验证了 B 未婚时保留 A、A/B 均已婚时返回真实 B 缓存记录、跨组件按标识选人、非法或其他角色的对象键回退，以及两条记录不被改写。
- **五项条件覆盖的完整实际行为、婚礼对象正确性、多人关系读档后的交互及离婚仍未完成验证。** 多条婚姻实际写入与保存已有下述用户进度证据。
- 用户已提供真实挂钩 / 重新检查及对象路由日志；路由日志与保存记录相符，但不能据此宣称婚礼、同居和全部互动均已支持多个伴侣。
- 用户后续的运行日志与 15:54 slot108 已确认同一角色同时保存四条已婚记录，原 A 保留。**同居 / 召唤仍只显示当前一人，不能把记录并存当作多人同时出场已经实现。** 具体对比见验证记录。
- 全员出场入口已离线定位，模拟用例验证了逐对象召唤 / 收回、角色与婚姻过滤、递归抑制、上下文内选人恢复，以及主失败 / 额外失败处理；新第三个原生挂钩的入口字节恢复也已验证。**尚未在游戏中执行全员出场。**

目前只构建了新版输出，没有自动替换游戏目录的插件或继续实机测试。验证程序不随插件运行，也未加入仓库。

详见 [实现说明](docs/design.md)、[验证记录](docs/validation.md) 和 [依赖来源](docs/dependencies.md)。

## 目录

```text
src/          插件、自动定位器源码及中文注释
config/       中文注释的默认 INI
build.cmd     构建入口
docs/         原理、兼容性与验证说明
evidence/     历史插件日志摘录
vendor/       提交层挂钩依赖 MinHook 的必要 x64 源码与许可证
```

仓库不包含游戏资源、第三方存档、存档密钥、研究工具克隆或 ASI Loader 二进制。vendor 仅保留构建提交挂钩所需的 MinHook 源码。
