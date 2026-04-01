# 纯接收端流程分层重构设计

## 1. 背景

`rx_tech_demo` 当前已经不再是“多技术栈性能对比工程”，而是一个正在收敛为纯接收端软件的项目。

现状问题主要有三类：

- 目录命名仍保留 `benchmark_core`、`apps`、`backends` 等技术验证阶段语义
- 主链路编排虽然已经简化，但代码组织仍未按接收流程分层
- `AF_XDP` 仍位于主源码路径中，容易和当前“只做 DPDK 接收主线”的目标混淆

本次重构不以新增业务功能为目标，而是以“让代码结构看起来像一个真正的接收端软件”为目标。

## 2. 目标

本次重构目标如下：

- 把源码主目录重组为“纯接收流程分层”
- 让 `DPDK` 成为当前唯一主接收实现
- 保留 `AF_XDP` 代码，但降级到 `legacy/`，不再占据主目录语义
- 把主链路编排明确为 `OwnerLoop`
- 让各模块职责与 [接收端模块接口与 ownership 边界.md](D:\WorkSpace\Company\Tower\rx_tech_demo\docs\设计方案\接收端专题\接收端模块接口与 ownership 边界.md) 对齐
- 为后续逐步长成完整接收端保留稳定骨架

## 3. 非目标

本次重构不处理以下事项：

- 不改变接收端专题文档里已经冻结的协议语义
- 不删除解析、校验、重组、CPI 相关代码能力
- 不引入新的下游处理线程模型
- 不把 `AF_XDP` 完全移除
- 不在本轮实现完整的 CPI 级 owner-loop 全链业务闭环

## 4. 设计原则

### 4.1 目录结构必须表达主流程

目录应该优先表达“接收端如何工作”，而不是表达“历史上怎么拆库”。

因此新的主源码目录必须围绕：

- ingress
- protocol
- admit
- storage
- finalize
- output
- sidecar
- core
- runtime

来组织。

### 4.2 Owner 内同步模块与跨线程模块边界必须清晰

主数据路径模块都应是 owner 内同步调用模块。

真正跨线程边界只保留在：

- 最终输出对象投递
- 下游释放回执

这两处。

### 4.3 当前主线只服务 DPDK

当前代码组织必须明确传达：

- 主接收实现是 `DPDK`
- `AF_XDP` 不是当前主接收实现
- `AF_XDP` 仅作为兼容/历史保留实现存在

### 4.4 先完成结构归位，再逐步收紧实现

本次重构优先保证：

- 目录正确
- target 正确
- 模块边界正确
- 命名正确

而不是在同一轮把所有历史实现一次性改造成专题文档中的最终形态。

## 5. 推荐目录结构

新的主目录结构如下：

```text
src/
  receiver/
    app/
      main_dpdk.cpp
      cli/
        cli_args.h
        cli_args.cpp
    core/
      owner_loop.h
      owner_loop.cpp
      owner_state.h
      enums.h
      reason_code.h
    ingress/
      dpdk/
        rx_ingress.h
        rx_ingress.cpp
        dpdk_port.h
        dpdk_port.cpp
    protocol/
      demo_protocol.h
      parsed_packet_view.h
      packet_parser.h
      packet_parser.cpp
      packet_validator.h
      packet_validator.cpp
    admit/
      admission_result.h
      cpi_admission.h
      cpi_admission.cpp
      recent_closed_ring.h
    storage/
      cpi_context.h
      ctx_pool.h
      ctx_pool.cpp
      slot_index.h
      slot_index.cpp
      slot_writer.h
      slot_writer.cpp
      progress_tracker.h
      progress_tracker.cpp
    finalize/
      cpi_finalizer.h
      cpi_finalizer.cpp
      decision_policy.h
    output/
      cpi_output.h
      output_pool.h
      output_pool.cpp
      output_dispatcher.h
      output_dispatcher.cpp
      release_token.h
    sidecar/
      metrics_sink.h
      metrics_sink.cpp
      recorder_sink.h
      recorder_sink.cpp
    runtime/
      receiver_config.h
      receiver_config.cpp
      receive_runner.h
      receive_runner.cpp
      rx_backend.h
      packet_desc.h
      time_utils.h

  legacy/
    af_xdp/
      ...
```

## 6. 现有目录到新目录的迁移映射

### 6.1 当前保留并迁入主线的内容

现有以下文件应进入新的 `src/receiver/` 主线：

- `src/apps/common/cli_args.*` -> `src/receiver/app/cli/`
- `src/apps/common/app_main_common.*` -> 其职责拆到 `app/` 与 `runtime/`
- `src/apps/rxbench_dpdk_main.cpp` -> `src/receiver/app/main_dpdk.cpp`
- `src/benchmark_core/include/rxtech/demo_protocol.h` -> `src/receiver/protocol/demo_protocol.h`
- `src/benchmark_core/include/rxtech/parser.h` / `src/benchmark_core/src/parser.cpp` -> `src/receiver/protocol/`
- `src/benchmark_core/include/rxtech/reassembly.h` / `src/benchmark_core/src/reassembly.cpp` -> 暂放 `src/receiver/storage/` 或 `finalize/` 相关位置，后续再细分
- `src/benchmark_core/include/rxtech/metrics.h` / `src/benchmark_core/src/metrics.cpp` -> `src/receiver/sidecar/`
- `src/benchmark_core/include/rxtech/rx_config.h` / `src/benchmark_core/src/rx_config.cpp` -> `src/receiver/runtime/receiver_config.*`
- `src/benchmark_core/include/rxtech/receive_context.h`
- `src/benchmark_core/include/rxtech/receive_runner.h`
- `src/benchmark_core/src/receive_runner.cpp`
- `src/benchmark_core/include/rxtech/rx_backend.h`
- `src/benchmark_core/include/rxtech/packet_desc.h`
- `src/benchmark_core/include/rxtech/time_utils.h`
- `src/backends/dpdk/*` -> `src/receiver/ingress/dpdk/`

### 6.2 当前降级为 legacy 的内容

现有以下目录应整体降级为 `legacy/`：

- `src/backends/af_xdp/` -> `src/legacy/af_xdp/`

迁移后约束如下：

- 默认不参与主构建
- 不再作为 README 主线描述对象
- 不再作为接收端默认入口
- 仅保留兼容/参考价值

### 6.3 当前应删除或彻底退出主语义的旧命名

以下旧命名必须退出主语义：

- `benchmark_core`
- `rxbench_*`
- “backends 为主语义”的目录组织

即便短期仍保留少量兼容 target，也不应继续让这些命名作为仓库骨架存在。

## 7. 模块边界

### 7.1 主编排器

`OwnerLoop` 是唯一主调度者。

它负责：

- 从 `RxIngress` 拉 burst
- 对单包依次调用：
  - `PacketParser`
  - `PacketValidator`
  - `CpiAdmission`
  - `SlotWriter`
  - `ProgressTracker`
  - 必要时 `CpiFinalizer`
  - 必要时 `OutputDispatcher`
- drain 回收回执
- flush 轻量 sidecar

### 7.2 ingress

`RxIngress` 只负责从 DPDK 队列取包并把 mbuf 交给 owner。

它不负责：

- 解析
- 校验
- CPI 判定
- 裁决

### 7.3 protocol

`PacketParser` 和 `PacketValidator` 只处理单包视图与单包合法性。

它们不接触：

- `OwnerState`
- `CpiContext`
- 输出对象

### 7.4 admit

`CpiAdmission` 只判断一个合法包是否可写当前 CPI。

它不直接切换上下文，不分配对象，不推进生命周期。

### 7.5 storage

`SlotWriter`、`ProgressTracker`、`CtxPool`、`CpiContext` 统一归 `storage`。

这一层负责：

- 固定槽位定位
- 首次写入
- 位图与聚合摘要推进
- 上下文对象池

### 7.6 finalize

`CpiFinalizer` 只做：

- 裁决
- 封口
- 边界推进

它不负责：

- 旁路录制
- 下游投递
- 对象回收

### 7.7 output

`OutputDispatcher`、`CpiOutput`、`OutputPool`、`ReleaseToken` 归 `output`。

这一层负责：

- 构造只读输出对象
- 投递到下游
- 管理输出对象生命周期

### 7.8 sidecar

`metrics_sink`、`recorder_sink` 等只消费：

- 最终输出对象
- 轻量计数
- 轻量事件

绝不允许消费 owner 可写上下文。

## 8. CMake 组织

新的 CMake 应该体现“主线是 receiver，不是 benchmark”。

建议 target 组织为：

- `rx_receiver_runtime`
- `rx_receiver_protocol`
- `rx_receiver_storage`
- `rx_receiver_output`
- `rx_receiver_sidecar`
- `rx_receiver_dpdk_ingress`
- `rx_receiver_app_dpdk`

`legacy/af_xdp` 可组织为：

- `rx_legacy_af_xdp`

并满足：

- 默认不参与主入口 target
- 默认不参与 README 中的推荐构建目标

## 9. 可执行程序与命名

建议逐步把：

- `rxbench_dpdk`
- `rxbench_xdp`

改成更符合接收端语义的名称，例如：

- `rx_receiver_dpdk`
- `rx_receiver_af_xdp_legacy`

若本轮担心连锁影响过大，可分两步：

1. 先改目录与 target
2. 再改最终二进制名

## 10. 迁移顺序

### 第一阶段：骨架迁移

- 建立 `src/receiver/` 与 `src/legacy/`
- 迁移 DPDK 主线代码到新目录
- 迁移 AF_XDP 到 `legacy/`
- CMake 改为新目录驱动

### 第二阶段：命名收敛

- 去掉 `benchmark_core` 命名
- 去掉 `rxbench_*` 命名
- README、配置、测试统一新口径

### 第三阶段：模块内聚

- 从 `receive_runner` 中抽出 `owner_loop`
- 把 `metrics` 收入 `sidecar`
- 把 DPDK 入口从“后端”改成“ingress”
- 让 header 边界逐步对齐专题文档

## 11. 风险与控制

### 风险一：目录迁移导致 include 和 target 大面积断裂

控制方式：

- 先做机械迁移
- 再做 include 和 target 收口
- 每阶段都跑本地和服务器构建测试

### 风险二：AF_XDP 降级后仍被旧入口误引用

控制方式：

- 从主 CMake 中摘除默认接入
- 明确把入口命名改成 legacy
- README 不再把 AF_XDP 写成主线

### 风险三：结构改完但行为边界仍旧混乱

控制方式：

- 以 `OwnerLoop` 为唯一主调度者
- 各模块只负责一步
- 强制把 output/sidecar 从可写上下文隔离开

## 12. 完成判据

本轮结构简化完成的判据如下：

- 仓库主源码目录以 `receiver/` 为主语义
- `DPDK` 是默认唯一主接收实现
- `AF_XDP` 被迁入 `legacy/`
- 主 CMake 不再以 `benchmark_core` 为核心 target
- README 不再把项目描述成技术验证 benchmark 工程
- 本地与服务器构建测试通过

## 13. 推荐执行策略

建议下一轮实现按以下顺序推进：

1. 新建 `src/receiver` 和 `src/legacy`
2. 搬迁 `DPDK` 主线代码并修正 CMake
3. 搬迁 `AF_XDP` 到 `legacy`
4. 修正入口与配置命名
5. 修正 README 与测试
6. 本地验证
7. 服务器验证

## 14. 结论

本次重构的本质不是“再做一次清理”，而是把项目的代码组织正式从“技术验证工程”切换到“接收端软件雏形”。

新的主语义应该是：

- 纯接收流程分层
- DPDK 当前主线
- AF_XDP 降级保留
- OwnerLoop 单点编排
- 输出与 sidecar 严格从属

这套结构能够支撑当前“只做接收”的目标，也能为后续继续长成完整接收端保留稳定骨架。
