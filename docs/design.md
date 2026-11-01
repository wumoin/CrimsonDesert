# 实现说明

## 条件覆盖

原版部分恋爱入口包含：

```text
CheckTribe(Human) && CheckFriendlyLevel(<Me>)>=2 && IsMarriageable() && !HasLivingTogether()
```

当前分别挂钩以下条件类型的评估入口：

| 类型 | 配置键 | 有效结果覆盖 |
| --- | --- | --- |
| pa::ConditionData_CheckFriendlyLevel | IgnoreFriendlyLevel | 按虚拟等级 127 重算比较 |
| pa::ConditionData_IsMarriageable | IgnoreMarriageable | 1 → 0 |
| pa::ConditionData_HasLivingTogether | IgnoreLivingTogether | 0 → 1 |
| pa::ConditionData_CheckAffinityLevel | IgnoreAffinityLevel | 按虚拟等级 127 重算比较 |
| pa::ConditionData_CheckTribe | IgnoreTribe | 1 → 0 |

引擎三态返回为 0=条件满足、1=不满足、2=上下文不可用。每个包装函数都先调用原函数，再根据总开关及子开关覆盖有效真/假结果；2 和未知值原样返回。此处没有增加好感或直接写入婚姻数据。

CheckFriendlyLevel 和 CheckAffinityLevel 支持等于、不等于、大小比较等多种运算。子开关覆盖对应类型的结果，不只覆盖婚姻入口的 >=2 / >=3；CheckTribe 子开关也不限于 Human 参数。其他任务中的这些条件同样可能受影响。开关不改变好感、亲密度或角色种族，剧情与 Single/Dating/Married 阶段控制仍保留。

### 等级覆盖必须保持成功与失败互斥

已提取的交互数据中，Affinity_Start_Success 使用好感 >=2，Affinity_Start_Fail 使用好感 <2；Propose_Success / Propose_Shop 使用亲密度 >=3，Propose_Fail / Propose_Shop_Fail 使用亲密度 <3。它们分别是独立的正向比较条件，不是共用一个比较结果后再统一取反。

旧实现将等级条件的“不满足”统一改成“满足”，因此可能同时放行“>=3 成功”和“<3 拒绝”。当前实现按同一个虚拟等级 127 重新求值，使这两个比较保持互斥。实际等级不变，原结果为 2 或未知值时也不重算。

自动定位器会解析等级评估器的运算字段读取、六分支 switch 表和阈值字段读取，再从各分支的 setcc / 布尔取反指令恢复比较语义。当前两个类型的字段偏移均解析为 0x1a（运算）、0x1b（阈值），六个编号分别对应 ==、!=、<=、>=、<、>；这些值没有硬编码进覆盖函数。解析失败时拒绝安装挂钩。

虚拟值 127 是当前有符号单字节等级字段可表达的最高值，不是实际游戏等级上限。它可以影响其他非婚姻等级分支，例如“等级恰好等于 3”将不成立，因此仍需考虑全局条件覆盖的影响。

### 示爱与求婚是不同流程

Affinity_Start 与 Propose 在交互表中各有成功和失败记录，且所用等级类型及关系阶段条件不同。普通求婚成功/失败记录都包含参数值为 1 的 CheckMaritalState；示爱记录还含参数值为 0、2 的关系阶段判断。这里只确认它们不是同一交互或同一关系阶段检查，没有把所有阶段条件统一移除。

已触发拒绝对话、好感降低，说明玩家进入了失败结果，但不能仅凭此确定所有底层限制都已满足。修正比较方向不等于直接写入交往或婚姻状态。

当前 CheckTribe 实现的匹配路径返回 0，不匹配或未能解析出有效对象的路径返回 1，没有独立的返回 2 分支。因此启用种族覆盖后，这两种返回 1 的情况都会被覆盖，无法仅凭返回值区分。其他类型若返回 2，则仍原样保留。

## 自动定位流程

实现位于 ConditionTarget.cpp，不依赖节名称或固定游戏 RVA：

1. 检查宿主是可读的 PE64 模块，按节属性和 VirtualQuery 枚举可读范围；跳过未提交、不可读和保护页。
2. 搜索完整的 MSVC 修饰类型名，从名称前的两个 64 位字段回溯 TypeDescriptor。
3. 搜索 x64 Complete Object Locator，检查 signature=1、主对象偏移为 0、self 指回自身、类型 RVA 以及类层次结构的首个基类引用。
4. 查找指向 Locator 的绝对指针，将紧随其后的指针数组视为候选虚表；只接受指向宿主可执行区的入口。
5. 在候选虚表各入口中识别条件分派函数，从条件对象自身虚表的间接尾跳转中读取评估槽偏移，除以指针大小得到实际槽号。不固定分派槽或评估槽。
6. 使用 PE x64 异常目录确定评估函数边界，再检查上下文读取、不可用返回和对应的布尔结果转换模式。
7. 对两个等级类型，再从函数中恢复比较字段、六分支跳转表及每个分支的比较运算。
8. 要求每个条件只有一个兼容候选。全部五个条件定位完成后才进入挂钩安装阶段。

分派指令支持已识别的 disp8 / disp32 间接跳转，以及可选的 REX.W 前缀。忽略相对调用地址、上下文字段偏移、栈布局和跳转距离等构建细节；保留结构与返回结果模式检查。

这些检查只是兼容性依据，不能证明未来版本的业务语义必然不变。编译器改变指令生成模式时也可能拒绝定位，需要人工适配。

## 挂钩与配置线程

- 原函数指针在包装函数发布前全部保存。
- 安装时覆盖位掩码保持 0，包装函数暂时返回原版结果。
- 用 VirtualProtect 临时开放写权限，用 InterlockedCompareExchangePointer 比较并替换虚函数槽，再恢复内存保护。
- 如果某个槽安装失败，尝试撤回之前已经安装的槽，且不启用任何覆盖。其他插件修改过的槽不会被强行覆盖。
- 工作线程每 5 秒读取总开关和五个子开关，用一个原子位掩码发布当前启用条件，避免回调读取半更新的内存状态。
- 日志包含 PID、每个条件解析得到的 RVA 和槽号，以及加载 / 开关状态。不包含每次调用的计数或运行时自检。

INI 的总开关默认为关闭；缺失子开关时沿用旧行为：只有同居覆盖开启，其余四项关闭。源码与 INI 注释为中文，构建使用 /utf-8。

插件不改游戏函数的机器码、资源包或存档格式。应完整退出游戏后再更新或卸载 DLL，不支持运行中卸载。

## 当前与历史地址

下面是离线解析 / 历史研究结果，**不是编译进插件的地址常量**：

| 条件 / 项目 | 原研究 EXE 1.0.0.3460 | 更新后 EXE 1.0.0.3462 |
| --- | --- | --- |
| HasLivingTogether TypeDescriptor RVA | 0x06fbb5c0 | 0x06fbc4c0 |
| HasLivingTogether 虚表 RVA | 0x05cfad38 | 0x05cfba30 |
| HasLivingTogether 评估函数 RVA | 0x024896d0 | 0x024899e0 |
| IsMarriageable 虚表 RVA | 未在此记录 | 0x05cfb8c0 |
| IsMarriageable 评估函数 RVA | 0x02488ec0 | 0x024891d0 |
| CheckFriendlyLevel 虚表 RVA | 未在此记录 | 0x05cff138 |
| CheckFriendlyLevel 评估函数 RVA | 未在此记录 | 0x02440800 |
| CheckAffinityLevel 虚表 RVA | 未在此记录 | 0x05cff2a8 |
| CheckAffinityLevel 评估函数 RVA | 未在此记录 | 0x024415e0 |
| CheckTribe 虚表 RVA | 未在此记录 | 0x05cf6b40 |
| CheckTribe 评估函数 RVA | 未在此记录 | 0x02420250 |
| 当前解析到的评估槽号 | 21 | 21 |

自动定位在模拟虚表移位及槽号由 21 改为 22 的离线映像上也通过。该模拟不执行游戏代码，不能当作实际新版或运行时挂钩测试。

## 兼容性边界

自动定位针对“类型仍存在、RTTI ABI 不变、分派与三态结果模式仍兼容，但地址或槽号移动”的更新，减少手动更新偏移的需要。它不比较游戏版本、PE 时间戳或固定映像大小，也不会通过删除所有检查来强行挂钩。

以下变化仍可能需要维护：类型改名或移除、RTTI 被裁剪、多个候选虚表无法区分、分派代码被改写、异常目录缺失、调用约定或返回语义改变。

## 原始资源与存档研究

本机使用 PAZ / PAMT / PAPGT 资源体系。未发现独立的配偶人数上限参数。当前 PAPGT 是含签名的索引格式，研究选择运行时挂钩，没有修改资源索引。

已定位的条件包括 DLC 条件 1013712、1014823 至 1014826，以及交互表 1000396、1000397、1000378、1000379 中的 HasLivingTogether。条件 1013714 中的 CheckMaritalState(<Me>,Single) 仍保留。

公开存档中存在 FieldNPCMaritialStateSaveData（游戏原名如此拼写），包含 _playableCharacterKey、_maritalState 和 _isParty；FieldNPCSaveData 通过 _npcMaritialStateDataList 保存记录。另有遍历列表、返回第一个符合条件对象的查询。因此解除交互条件并不能保证婚姻写入、同居对象选择、保存重载和离婚支持多对象。

## 参考

- [LLVM 的 Microsoft C++ RTTI 生成实现](https://llvm.googlesource.com/llvm-project/clang/+/refs/heads/main/lib/CodeGen/MicrosoftCXXABI.cpp)：用于核对 MSVC x64 RTTI 布局；未复制其实现。
- [Microsoft x64 异常处理说明](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64)：异常表中的 RUNTIME_FUNCTION 记录提供函数范围。
- [dmm-parser 的 PAPGT 格式实现](https://github.com/exodiaprivate-eng/dmm-parser/blob/main/src/binary/papgt.rs)：用于识别签名索引；未复制其代码。

游戏内部类、分派模式与返回语义依据本机二进制分析。完整多配偶行为尚未验证。
