# 测试目录职责重组

本轮从 firmware `30e7d8b` 重组测试目录。生产无线实现和设备 workspace 未修改，
没有构建或刷写 ESP32 镜像。SDK 覆盖表中的测试路径随迁移更新，因此生成的覆盖
元数据摘要同步变化；它不改变公开 API、能力或原生运行逻辑。

## 实际结构

- `tests/c/` 保留原有 CMake/CTest 用例；`integration/` 收纳 334 个原生行为、
  SDK、VM 回归驱动，`fixtures/` 收纳从这些驱动及共享 VM 边界提取的 1,141 段
  C 源码。`.inc` 是参与组装的 C 片段，不要求单独构成可编译的 translation unit。
- `tests/python/` 原有 39 个模块负责工具和静态契约；另增加测试入口的十一项回归。
- `tests/support/` 收纳共享 compiler、原文读取、C 函数提取、VM 和 discovery 支持。
  `compile_run` 不再定义在测试用例模块中，调用方直接使用共享入口。
- 共用 CSI 协议样本移入 `tests/fixtures/`，原字节及 SHA-256 保留，原生与 Host
  解析验证共享同一份输入。
- `tests/js/flash_data/` 与 `templates/` 保留实机 JS 职责；测试 harness 的 Host
  校准 JS 输入放入 `tests/js/host-fixtures/`，不作为设备或 RF 验收。
- `remote.py test --scope c` 分别执行、报告 CTest 和原生 C/SDK/VM 集成测试；
  Python discover 只执行工具／静态契约。CI 与 README、AGENTS 同步。

完整职责与命令见 [tests/README.md](../../tests/README.md)。

## 覆盖保留与重复执行

迁移前的真实 discovery 结果为 1,253 次登记、1,131 个独立 case ID。
其中 122 次重复来自 imported TestCase：例如调用另一个测试类的 fixture factory，
使该类本身再次被 unittest 收集。原来的总数是实际重复执行次数，没有被改写为
1,253 个独立回归。

新入口只登记当前模块定义的 TestCase，保留本地子类继承的测试方法；导入失败和
空选择不会作为通过。迁移后的原有清单为 756 个原生集成用例、375 个工具／契约
用例。与原来 1,131 个独立 case ID 比较，缺失、意外新增、重复均为零。
新增十一项入口测试另外计数，工具测试共 386 项。

初始迁移的 1,141 个 C 片段都按 Python 字面量求值后的原文提取，逐段 SHA-256 一致。
原文读取不转换 CRLF、转义或 UTF-8 字节；缺失文件明确失败。五个片段保留原有
尾部空白或分隔换行，fixture 目录的属性保留这些输入，不修改其文本来满足排版检查。
动态源码组装、SDK 调用替身、生产函数提取和原断言均保留。

随后全量回归发现 `test_wifi_start_events` 尚未适配 `388069d` 新增的 netif helper
边界。用迁移前保存的 Python 原文在当前生产代码上也复现了相同编译失败。
仅为该用例的两段 C fixture 补上 helper/诊断声明，并增加准入失败不调用 helper、
helper 失败执行清理的断言。其余 1,139 段原文哈希保持不变；此项明确记录为额外
fixture 修复，不声称最终所有文件都只是字节不变的搬移。

## 验证记录

原始记录位于 `build/test-layout/`：

- `before-cases.json`：迁移前登记清单。
- `migration.json`：路径映射、C 片段来源及 SHA-256。
- `discovery-comparison.json`：迁移前后 case ID 比较。
- `runner-tests-final.log`：新增十一项入口测试通过；涵盖 imported class 去重、本地子类、
  单文件选择、空选择、导入错误、fixture 原文读取、失败子用例计数和 skip 保留。
  另覆盖显式选择去重、SDK 传递／缺失、整组 setup 失败及中止后未执行用例的统计。
- `tooling-final.log`：386 项通过，6.207 秒。
- `native-full.log`：原有 CTest 138 项通过；首轮原生集成的失败终态原样保留。
  它暴露了 CSI 样本相对路径遗漏、上述旧 fixture、子进程未继承 CLI 解析出的 SDK，
  以及 setup 失败／跳过时首版报告未按受影响用例统计的问题。
- `start-events-original-before.log`：迁移前原文复现旧边界失败；
  `csi-path-after.log` 与 `start-events-after.log`：修复后的相关六项通过。
- `setup-accounting-before.log`：先复现整组 setup 失败时计数遗漏，再修复报告。
  新报告分别保留 selected/executed、各 case 终态和 fixture 错误；未执行项不能算通过。
- `native-retry.log` / `native-retry-result.json`：只补测尚未通过的 71 个 SDK 用例，
  传入实际解析的固定 SDK 后全部通过，23.988 秒。
- `native-acceptance.json`：按独立 case ID 合并首轮和定向补测，完整覆盖原生集成
  756 项：755 passed、1 skipped（缺少 tshark）、无遗漏。没有把首轮失败改写成
  一次全量绿色，也没有为修复再跑整轮 CTest 或固件构建。
- `netif-smoke.log`：迁移后的两项生产 Station 启动回归通过。

API manifest、覆盖表及 feature 文档生成检查通过，MQuickJS 语法检查为 70 个源码、
69 个文档片段通过。SDK 保持 `fff9895c82d744c7237be8847347bdd1b07c6643` 且工作区干净。

本次不沿用旧镜像的硬件证据作为新测试结构的执行结果，也不把工具、Host VM、
SDK/linker/emulator 结果提升为 RF 或长时间运行资格。

## 按领域继续分层

在上述职责迁移完成后，继续拆分平铺目录：

- CTest 源码放入 `tests/c/unit/<domain>/`，CMake 入口及 138 个注册名称保留。
- 原生驱动放入 `tests/c/integration/<domain>/`，Wi-Fi 再按 lifecycle、driver、
  config、station、ap、security、CSI、monitor、TX、FTM、mesh、NAN、TWT 分组；
  provisioning 下再分 DPP、SmartConfig、WPS。
- C fixture 使用与驱动相同的 `<domain>/<case>/` 层级；共享 VM 片段放入
  `fixtures/shared/`，BLE、memory/radio stubs 放入 `tests/c/support/`。
- Python 工具分为 `tooling/build`、`tooling/generators`；静态契约分为
  `contracts/runtime`、`io`、`net`、`wireless`；测试基础设施放入 `infrastructure/`。
- JS 原本已按公开 API 模块分层，本轮保留其结构。

各 Python 目录补齐包入口，测试使用统一的 `tests.support.paths.ROOT` 定位生产源。
显式 native selector 支持多层目录，仍拒绝非 native 用例、仅目录及非法包名；
文件模式继续递归选择且只登记自有 TestCase。同步了 CMake 源路径、交叉导入、
fixture 路径、覆盖表和文档中的路径。现有历史结果文件保留其原始 case ID。

本次分层的独立记录位于 `build/test-hierarchy/`：

- `moves.json`：1,593 个文件的本轮前后路径映射。
- `before-cases.json`、`native-after.json`、`python-after.json`、`audit.json`：
  756 个原生用例和 386 个 Python 用例全部保留，缺失、新增、重复均为零。
- `before-source-hashes.json` / `audit.json`：比对 1,218 个 C 源码、头文件及片段，
  只有 `test_wifi_radio.c` 更新了相对生产 include 路径；其余字节一致。
  1,141 处静态 fixture 引用全部解析且可读取。
- `ctest.log` 保留首次配置失败：循环中的 CSI 源路径未跟随迁移；修正后
  `ctest-final.log` 的 138 项全部通过。
- `tooling.log`：386 项通过；扩展多层 selector、去重和拒绝边界后，
  `runner-final.log` 的 11 项再次通过。
- `native-smoke-request.json` / `native-smoke.json` / `native-smoke.log`：
  从全部 21 个原生叶目录选择 22 个模块，67 项通过、无 skip，80.287 秒。
  包括迁移后的 BLE support、memory/radio stubs、共享 VM、CSI golden、
  深层 WPS/SmartConfig/DPP fixture 和 Radio/netif helper 路径。
- `generated-checks.log`：API manifest、feature 文档、Wi-Fi 覆盖表和配置 schema
  检查通过，schema 同时对照本地 SDK header。`diff-check.log` 无空白错误。

本次没有重跑耗时的全量原生集成，也没有构建、刷写固件或执行设备测试。
前一轮 755 passed / 1 skipped 是前一轮的完整证据；本轮的运行证据明确是上述
67 项定向回归，完整清单及片段比对只证明迁移没有丢失用例或改写 fixture。
