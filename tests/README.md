# F8 伴侣管理验证

Windows x64，需要与 `build.cmd` 相同的 MSVC 工具链。

```powershell
.\tests\run.cmd
```

构建插件，运行模拟角色、单个临时 INI 和原生对话框回归；验证名字与功能开关共用 INI、保留中文注释、重置只删除目标名字，不加载或操作游戏。测试完成后删除其创建的临时配置。

窗口绑定回归还会在工作线程接入模拟主窗口，模拟旧窗口销毁但插件未收到通知、窗口隐藏及延迟销毁，验证自动重新绑定、每个窗口正确转发到自己的前任入口，以及旧通知不会清除新窗口状态。

原生名字回归覆盖已缓存长度及 `MAXDWORD` 未缓存长度的 UTF-16 字符串；模拟角色的默认名字采用实机观察到的未缓存格式，验证实际名字赋值和重置，而不只检查插件列表。

双语回归覆盖两种语言的完整文案、默认值、持久化、外部修改重新加载、保存失败回滚及多个窗口同步。管理窗口还检查滚动位置、未保存名字和编辑选区；美容窗口检查原名/自定义名保留、同名区分，以及确定、双击、取消。两个窗口都检查语言下拉框中的默认按钮保护。

可额外传入游戏 EXE 路径，检查真实文件的自动定位和 ABI 不兼容时的拒绝行为：

```powershell
.\tests\run.cmd "D:\gametool\steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe"
```

定位程序只在独立进程内把 EXE 映射为数据，不调用游戏入口或原生函数，不安装挂钩。

检查界面和 F8 的交互可在 x64 Native Tools 命令行运行：

```cmd
cl /nologo /utf-8 /std:c++17 /O2 /W4 /MT /EHsc /DPARTNER_MANAGER_PREVIEW tests\PartnerManagerTests.cpp src\UiLanguage.cpp /Fobuild\ /link user32.lib /OUT:build\PartnerManagerPreview.exe
build\PartnerManagerPreview.exe
```

聚焦测试宿主后按 F8 打开窗口，Escape 关闭；关闭宿主结束测试。所有对象和名字仍为模拟数据。以上检查不能代替游戏内显示、读档和召唤的实机验证。

美容窗口可单独预览（先运行一次 `build.cmd` 生成 MinHook 对象）：

```cmd
cl /nologo /utf-8 /std:c++17 /O2 /W4 /MT /EHsc /DPARTNER_BEAUTY_PREVIEW tests\BeautyPickerTests.cpp src\UiLanguage.cpp /Fobuild\ /link user32.lib build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /OUT:build\BeautyPickerPreview.exe
build\BeautyPickerPreview.exe
```
