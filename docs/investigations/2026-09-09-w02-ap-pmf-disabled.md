# W-02 AP 显式 PMF disabled

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [独立 PMF Driver 操作](2026-09-09-w07-pmf-control.md)，本批接入
`startAP({ pmf: "disabled" })`，保留 sole v1，无新增 callable。

## 实现

AP 参数捕获支持 disabled/optional/required；只有 WPA2 或 WPA/WPA2、未启用 WPA3
compatible 时可选 disabled。WPA3、WPA2/WPA3、OWE、compatible/SAE-EXT 冲突在
原生写入前拒绝。Open/pure WPA 继续没有 ergonomic PMF 选项；未指定时保持原默认。
既有严格字符串检查拒绝大小写、NUL 后缀及转换对象，失败沿用凭据擦除与 rooted capture。

disabled 在已验证的 AP 输入中编码为 capable=false、required=false。停止后的
配置事务再次核对安全条件，再按 set_config → 专用 disable_pmf_config → readback
执行，成功后才能 START。没有将 deprecated capable=false 的普通 set_config
当成 SDK 已关闭 PMF。新阶段为 ap-pmf-security 和 ap-pmf-config。

专用 SDK 调用失败可能已改变状态，仍进入既有复合 rollback：恢复之前的配置、
原 PMF 状态和 controls，逐项读回；回滚失败继续保留 cleanup。不同 AP 配置的
运行中 activation 仍需完整协调，不因本选项隐式停止其他 owner 或改变 Station。

AP acceptance 和共享 reopen 匹配同时更新：disabled 必须读回两个标志均 false；
optional/required 的 RSN 配置必须保留 capability，required 仍不得降低。后者修正
了此前仅校验 required、可能接受丢失 capability 的静态匹配缺口。没有实机复现
此缺口；生产函数的延后用例已补入，不把源码观察写成运行验收。

成功 AP result/status 的 pmf 字段可返回 disabled；Open/pure WPA 仍返回 null。
`capabilities().accessPointOptions.pmf`、正式类型、API 文档和任务表同步更新。
Station capability 不增加 disabled。Station connect 是另一条运行中生命周期，
raw configure 目前仍以 pmfRequired 表达输入，两者的显式 disable 集成继续待办。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w02-ap-pmf-c5-build.txt`，binary 2,770,416 bytes，比前批增加 208。
常驻账本与链接 hash 见 `build/w02-ap-pmf-evidence.json`。没有新增 native 账本或
分配，符号大小不代替运行时内存测量。

deferred `test_wifi_ap_config.py` 调用生产 capture、validator、readback/reopen matcher
和结果转换，补合法 auth、冲突 auth/compatible、严格字符串、capability 丢失、
disabled 不符和转换结果；原有 VM GC/OOM 循环沿用。
`test_wifi_config_controls.py` 调用生产停止后事务，模拟 set_config 重新启用 PMF，
验证专用关闭、SDK 部分失败后的完整 rollback、其他接口不变和安全预验证。
仅 AST 解析，没有导入、编译或执行两份 fixture。

生成物、类型与 MQuickJS 语法检查见 evidence。Host/Python/VM/故障注入、其他
target/feature-disabled、完整重启/AP activation、RF/共存和实机均 not-run。
全部 Wi-Fi API 完成后集中阶段与实机功能测试，长时间 soak 留到 BLE API 完成后。
未刷写、串口操作、擦除 workspace、前端构建、提交、推送或更新根 gitlink。
