# 实现说明

## 为什么先做运行时补丁

本机版本使用 PAZ / PAMT / PAPGT 资源体系。对提取的条件表与交互表检查后，发现限制来自条件逻辑，未找到独立的人数上限参数。

当前 PAPGT 是含签名的索引格式。实验选择 ASI 运行时挂钩，没有修改这些索引，也没有制作资源覆盖包。关于格式的外部研究参考为 [dmm-parser 的 PAPGT 实现](https://github.com/exodiaprivate-eng/dmm-parser/blob/main/src/binary/papgt.rs)；仓库未复制该项目的代码。

## 已定位的条件

- DLC `conditioninfo.dlc_01.staticinfobody`：条件 `1013712` 含人类、好感等级、可结婚及 `!HasLivingTogether()` 检查。
- 同表的 `1014823` 至 `1014826`：部分任务前置也包含 `!HasLivingTogether()`。
- 原版 `interactioninfo.staticinfobody`：`1000396`、`1000397`、`1000378`、`1000379` 四个 `Affinity_Start` 交互内嵌该条件。
- 条件 `1013714` 还包含 `CheckMaritalState(<Me>,Single)`。Single / Dating / Married 等状态参与对象的阶段逻辑，当前补丁没有统一移除它们。

## 挂钩位置

下列值均为当前 EXE 的 RVA，相对于模块加载基址，不是固定的进程绝对地址：

| 项目 | 值 |
| --- | --- |
| 类型 | `pa::ConditionData_HasLivingTogether` |
| RTTI 名称 RVA | `0x06fbb5d0` |
| 虚函数表 RVA | `0x05cfad38` |
| 评估函数槽号 | `21`，从 0 开始 |
| 评估函数 RVA | `0x024896d0` |
| PE 时间戳 | `0x6ae58024` |
| PE 映像大小 | `0x13ee0000` |

研究时 EXE 的 SHA256 为 `4e38ce1d8fe98eee7be0e26026a1d24ea6a17e80aa88e906b5f3ed553af11f63`。此哈希是版本参考，插件不会在运行时计算完整 EXE 哈希。

表中的 PE 时间戳和映像大小仅作为研究记录保留。当前源码已删除与这两个固定值的比较，不再使用它们判断游戏版本是否匹配；读取实际映像大小仅用于目标地址的边界检查。

## 执行流程

1. ASI Loader 加载插件，`DllMain` 建立工作线程。
2. 检查 PE64 格式、目标地址是否在实际映像范围内、RTTI 名称、原评估函数前 32 字节及原虚函数槽；任一检查失败则记录原因并退出工作线程，不进行游戏版本匹配。
3. 保存原函数指针，临时调整虚函数槽所在内存的保护，用 `InterlockedCompareExchangePointer` 替换入口，并恢复保护。
4. 每次回调先执行原函数，再按开关执行 `0 → 1` 映射。
5. 工作线程每 5 秒刷新 INI，开关状态变化时写入日志。

```cpp
auto result = original(object, context);
return enabled && result == 0 ? 1 : result;
```

实际实现保留了目标位置检查、并发替换检查和加载 / 开关状态日志。插件不把缺失上下文的返回值 `2` 伪造为有效结果。测试用的结果映射自检及调用计数已从当前版本移除，评估回调只调用原函数、读取开关并映射结果。

## 关于配偶数据结构的发现

下载的较新公开存档包含 `FieldNPCMaritialStateSaveData`（游戏原名如此拼写），字段包括：

```text
_playableCharacterKey
_maritalState
_isParty
```

每个 `FieldNPCSaveData` 有 `_npcMaritialStateDataList`，说明状态按 NPC 与可操作角色关联保存。该公开存档的 1,001 个 NPC、共 3,003 条记录中，婚姻状态字段都未显式写入，不能把它当作已婚测试样本。

反汇编发现 RVA `0x01922860` 按角色键查找一个列表内的对象；`0x01922910` 遍历步长为 `0x38` 的记录，跳过状态字节等于 `3` 的记录，返回第一个剩余对象。`HasLivingTogether` 使用后一查询的空/非空结果。

这里只能确定“有列表”和“存在取第一个对象的查询”。尚未确定数值 `3` 的枚举含义，不能据此宣称底层已经支持完整的多配偶行为。

## 当前实现的限制

- 修改的是同名条件类的评估入口，使用该类的其他任务或交互也可能受影响。
- 没有修改婚姻请求处理、NPC 状态写入、同居对象选择或存档格式。
- 即使第二个恋爱入口出现，后续仍可能被其他检查阻止、替换旧对象，或只在 UI 中显示第一个对象。
- 游戏更新后所有地址和语义均需重新确认；当前插件不比较版本标识，但目标地址、类型、函数字节或原虚函数槽不匹配时仍停止挂钩。
- 本次用户叫停时，关键的“已有对象后继续结婚”测试仍未完成。
