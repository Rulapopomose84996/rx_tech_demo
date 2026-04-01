# 一、总的模块边界结论

实现上建议只保留下面 9 个模块。

## 1）`RxIngress`

负责：

* 从 DPDK 队列批量收包
* 把 mbuf 批量交给 owner 主循环

不负责：

* 业务解析
* 合法性判断
* CPI 写入判定
* 回收裁决

**ownership：**

* mbuf 在进入 `RxIngress` 后，归 owner 线程这一轮处理逻辑独占
* 不跨线程传 mbuf
* 不允许旁路模块拿走 mbuf 引用

---

## 2）`PacketParser`

负责：

* 从 mbuf 中解析固定头字段
* 形成 `ParsedPacketView`

不负责：

* 合法性最终裁定
* CPI 归属
* 槽位写入

**ownership：**

* 只产生栈上解析视图
* 不持有 mbuf 生命周期
* 不缓存解析对象

---

## 3）`PacketValidator`

负责：

* 单包合法性校验
* 非法包 reason code 归一化

不负责：

* 当前 CPI 是否可写
* 未来/过期细分主流程展开
* 写入上下文

这符合现有冻结口径：单包层面至少校验长度、包头、包尾、字段范围、字段组合一致性；非法包不得进入归属、落位和输出路径。

**ownership：**

* 输入是 `ParsedPacketView`
* 输出是 `PacketValidity`
* 不接触 `CpiContext`

---

## 4）`CpiAdmission`

负责：

* 判断一个合法包是否允许写入当前 CPI
* 识别 `CPI_SWITCH`
* 识别命中 recent closed / 迟到包
* 给出写入许可结果

不负责：

* 具体槽位写入
* 位图推进
* 裁决执行

这对应现有冻结口径中“合法/非法、可写当前 CPI/不可写当前 CPI、首次写入/重复写入”三类最少必要判断里的第二层。

**ownership：**

* 只读访问 `OwnerState.active_ctx`
* 只读访问 `RecentClosedRing`
* 不修改 payload 区
* 不分配对象

---

## 5）`SlotWriter`

负责：

* 根据 `(PRT, Channel, PacketIndex)` 计算固定槽位
* 判定首次/重复
* 首次时一次落位 payload
* 更新 `slot_valid_bytes`

不负责：

* 创建上下文
* 业务裁决
* 输出对象生成

这和现有冻结的“固定槽位一次落位”“首次写入获胜”“重复包不覆盖原数据”完全一致。

**ownership：**

* 独占写 `active_ctx`
* 只在 owner 线程内可调用
* 一旦拷贝完成即可释放 mbuf

---

## 6）`ProgressTracker`

负责：

* 更新分片位图
* 更新通道/PRT/CPI 聚合摘要
* 设置收口触发位：`FULL_READY / TAIL_OBSERVED / TIMEOUT candidate ...`

不负责：

* 做最终裁决
* 输出对象封装
* 旁路导出

这对应现有冻结里“后续只维护位图、长度、状态和引用关系”“首次写入后只推进最少必要位图与聚合摘要”。

**ownership：**

* 独占写 `active_ctx`
* 不跨线程
* 不依赖外部锁

---

## 7）`CpiFinalizer`

负责：

* 在收口条件满足时进入裁决
* 读取聚合摘要
* 形成唯一裁决结果
* 封口：`ACTIVE -> DECIDING -> SEALED`
* 推进边界
* 决定输出或作废
* 进入 `TOMBSTONE`

这和现有裁决阶段文档完全对齐：必须先封口，再推进边界；同一 CPI 只能完成一次有效裁决；作废也必须完成封口和回收闭环。

**ownership：**

* 独占写待裁决 `CpiContext`
* 只由 owner 调用
* 不允许下游参与生命周期推进

---

## 8）`OutputDispatcher`

负责：

* 从输出池申请 `CpiOutput`
* 填只读视图与元数据
* 推入 `tx_spsc`
* 把 `CpiContext` 保持在 `TOMBSTONE` 等待回收

这对应“最终 CPI 输出对象只读化、与可写上下文分离、输出池与上下文池分离、经 SPSC 交给下游消费者”。

**ownership：**

* `CpiOutput` 在 push 成功前归 owner
* push 成功后归下游消费者只读持有
* `CpiContext` 仍归 owner，但不可复用

---

## 9）`AsyncSidecar`

负责：

* 轻量统计汇聚
* 异步日志/事件
* 可选录制
* 旁路导出

不负责：

* 接触 owner 可写上下文
* 反压主链路
* 回写任何主状态

这必须严格遵守现有边界：旁路、录制、日志只能消费最终对象或必要副本，不接触 owner 可写上下文，不得反压主链路。

**ownership：**

* 只能消费 `CpiOutput` 或轻量事件
* 不允许保留 `CpiContext*`

---

# 二、推荐的目录与代码边界

建议代码目录直接按接口边界落，不要按“算法概念”乱拆。

```text
receiver/
  core/
    owner_loop.h
    owner_state.h
    reason_code.h
    enums.h

  ingress/
    rx_ingress.h
    rx_burst.h

  protocol/
    packet_parser.h
    packet_validator.h
    parsed_packet_view.h

  admit/
    cpi_admission.h
    admission_result.h
    recent_closed_ring.h

  storage/
    cpi_context.h
    slot_index.h
    slot_writer.h
    progress_tracker.h
    ctx_pool.h

  finalize/
    cpi_finalizer.h
    decision_policy.h
    boundary_advancer.h

  output/
    cpi_output.h
    output_pool.h
    output_dispatcher.h
    release_token.h

  sidecar/
    metrics_sink.h
    event_sink.h
    recorder_sink.h
```

这个目录体现一个原则：

**数据面主链路模块必须都是 owner 内同步调用模块；真正跨线程边界只留在 output 和 release 回执这两处。**

---

# 三、推荐的顶层编排关系

顶层只保留一个主编排器：

## `OwnerLoop`

它是唯一主调度者。

### 它负责的事

* 调 `RxIngress::poll_burst()`
* 对每个包依次调用：

  * `PacketParser`
  * `PacketValidator`
  * `CpiAdmission`
  * `SlotWriter`
  * `ProgressTracker`
  * 必要时 `CpiFinalizer`
* 定期 drain 下游回收回执
* 定期 flush 轻量统计

### 它不负责的事

* 解析细节内部实现
* 裁决规则内部实现
* 录制内部实现

### 它的价值

把所有状态推进点集中到一个地方，避免 ownership 漏洞。

---

# 四、完整接口草案

下面直接给建议接口形态。不是最终代码，但已经是头文件级别的接口草案。

---

## 1）`RxIngress`

```cpp id="rxingress_h"
class RxIngress {
public:
    // 返回本轮实际收到的 mbuf 数
    uint16_t poll_burst(rte_mbuf** out, uint16_t capacity) noexcept;
};
```

### 输入输出

* 输入：预分配好的 `rte_mbuf*` 数组
* 输出：本轮 burst 数

### ownership

* `poll_burst` 返回后，这批 mbuf 的处置权归 `OwnerLoop`
* `RxIngress` 不缓存 mbuf，不持有后续责任

---

## 2）`PacketParser`

```cpp id="packetparser_h"
struct ParsedPacketView {
    uint16_t cpi;
    uint16_t prt;
    uint16_t channel;
    uint16_t packet_index;

    bool is_tail;
    const uint8_t* payload_ptr;
    uint16_t payload_len;
    uint64_t rx_tsc;
};

class PacketParser {
public:
    bool parse(const rte_mbuf& mbuf, ParsedPacketView& out) const noexcept;
};
```

### 输入输出

* 输入：单个 mbuf
* 输出：栈上 `ParsedPacketView`

### ownership

* `ParsedPacketView` 只是一层借用视图
* `payload_ptr` 只在当前 mbuf 存活期有效
* 绝不能跨函数链长期保存

---

## 3）`PacketValidator`

```cpp id="packetvalidator_h"
enum class ReasonCode : uint16_t {
    NONE,
    INVALID_LEN,
    INVALID_HEADER,
    INVALID_TAIL,
    INVALID_CHANNEL,
    INVALID_PRT,
    INVALID_PACKET_INDEX,
    INVALID_FIELD_COMBINATION,
    NOT_WRITABLE_CURRENT_CPI,
    DUPLICATE_PACKET,
    LATE_TO_CLOSED_CPI,
    INTERNAL_ERROR
};

struct PacketValidity {
    bool ok;
    ReasonCode reason;
};

class PacketValidator {
public:
    PacketValidity validate(const ParsedPacketView& pkt,
                            const BoundWaveSnapshotLite* bind) const noexcept;
};
```

### 说明

这里允许校验分成两层：

* 不依赖当前上下文的纯单包校验
* 依赖 `bind->n_prt` 的范围校验

### ownership

* 只读输入
* 不触碰 `OwnerState`
* 不写上下文

---

## 4）`CpiAdmission`

```cpp id="cpiadmission_h"
enum class AdmissionKind : uint8_t {
    WRITE_ACTIVE,
    TRIGGER_CPI_SWITCH,
    LATE_TO_CLOSED,
    DROP_NOT_CURRENT,
    DROP_NO_ACTIVE,
    DROP_UNTRUSTED
};

struct AdmissionResult {
    AdmissionKind kind;
    ReasonCode reason;
};

class CpiAdmission {
public:
    AdmissionResult decide(const ParsedPacketView& pkt,
                           const OwnerState& owner) const noexcept;
};
```

### 核心规则

* `pkt.cpi == active_cpi` -> `WRITE_ACTIVE`
* `pkt.cpi == active_cpi + 1 mod 65536` -> `TRIGGER_CPI_SWITCH`
* 命中 recent closed -> `LATE_TO_CLOSED`
* 其他 -> `DROP_NOT_CURRENT`

这与已冻结的当前 CPI 写入判定一致。

### ownership

* 只读 `OwnerState`
* 不创建新上下文
* 不推进生命周期

这里我明确建议：
**`CpiAdmission` 只做判定，不做切换动作。**
真正的 `CPI_SWITCH` 切换动作必须由 `OwnerLoop` 调 `CpiFinalizer` 后显式完成。
否则“判定模块顺手改状态”会把 ownership 搞脏。

---

## 5）`SlotWriter`

```cpp id="slotwriter_h"
struct SlotWriteResult {
    bool first_write;
    bool duplicate;
    uint32_t slot_index;
    ReasonCode reason;
};

class SlotWriter {
public:
    SlotWriteResult write(CpiContext& ctx,
                          const ParsedPacketView& pkt) const noexcept;
};
```

### 内部动作

* 计算 `slot_index`
* 查位图
* 首次则 `memcpy`
* 重复则直接返回

### ownership

* 独占写 `CpiContext`
* 只允许 owner 线程调用
* 调用结束后，上游可立即释放 mbuf

---

## 6）`ProgressTracker`

```cpp id="progresstracker_h"
struct ProgressUpdate {
    bool full_ready;
    bool tail_observed;
    bool should_finalize;
};

class ProgressTracker {
public:
    ProgressUpdate on_first_write(CpiContext& ctx,
                                  const ParsedPacketView& pkt,
                                  uint32_t slot_index) const noexcept;

    bool check_timeout(const CpiContext& ctx, uint64_t now_tsc) const noexcept;
};
```

### ownership

* 只读/写 `ctx`
* 不申请对象
* 不发队列

### 边界

`ProgressTracker` 可以设置触发位，但**不能自己调用 `CpiFinalizer`**。
否则模块职责会串位。

---

## 7）`CpiFinalizer`

```cpp id="cpifinalizer_h"
struct FinalizeResult {
    bool sealed;
    CpiDecision decision;
    bool should_output;
};

class CpiFinalizer {
public:
    FinalizeResult finalize(CpiContext& ctx,
                            TriggerBits trigger_bits,
                            uint64_t now_tsc) const noexcept;
};
```

### 它必须做的动作

* 幂等检查
* `ACTIVE -> DECIDING`
* 冻结读取摘要
* 形成唯一裁决结果
* 封口：`DECIDING -> SEALED`
* 推进边界
* 进入 `TOMBSTONE`

这与现有裁决文档一致。

### 它绝不能做的动作

* 直接把输出 push 到下游
* 直接释放上下文
* 调旁路录制

这些都应分离给后续模块，否则裁决模块就会既裁决又调度又回收，边界不干净。

---

## 8）`OutputDispatcher`

```cpp id="outputdispatcher_h"
struct DispatchResult {
    bool pushed;
    bool dropped_by_pool_exhaustion;
};

class OutputDispatcher {
public:
    DispatchResult dispatch(CpiContext& sealed_ctx,
                            OwnerState& owner) noexcept;
};
```

### 它负责

* 从 `output_pool` 申请 `CpiOutput`
* 填只读视图
* `tx_spsc.push(output)`
* 成功则把 `sealed_ctx` 留在 `TOMBSTONE`
* 失败则决定是否进入受控降级路径

### ownership

* 输出对象 push 前归 owner
* push 后归下游只读持有
* `CpiContext` 始终归 owner 生命周期管理

### 关键约束

下游只读，不回写接收端上下文，不参与接收端生命周期推进。

---

## 9）`AsyncSidecar`

```cpp id="asyncsidecar_h"
class MetricsSink {
public:
    void on_counter_delta(const OwnerCounters& delta) noexcept;
};

class EventSink {
public:
    void on_cpi_event(const CpiEvent& ev) noexcept;
};

class RecorderSink {
public:
    void on_output_ready(const CpiOutput& out) noexcept;
};
```

### ownership

* 只能拿到值对象、轻量摘要或 `const CpiOutput&`
* 不能拿到 `CpiContext&`
* 不能阻塞 `OwnerLoop`

---

## 10）下游回收回执 `ReleaseChannel`

```cpp id="releasechannel_h"
struct ReleaseToken {
    uint32_t output_id;
    uint32_t ctx_pool_index;
};

class ReleaseChannel {
public:
    bool push(const ReleaseToken& token) noexcept;   // 下游 -> owner
    bool pop(ReleaseToken& token) noexcept;          // owner drain
};
```

### 作用

* 下游消费完成后只回一个轻量 `ReleaseToken`
* owner 收到后释放 `CpiOutput`
* 再把对应 `CpiContext` 回收到 `ctx_pool`

### 为什么必须有

因为现有冻结要求已经很清楚：

* 输出池与上下文池分离
* 生命周期与回收路径显式闭环
* 避免下游仍在读而接收端已复用内存。

所以不要偷懒让下游直接 free 上下文，也不要靠“猜测下游应该读完了”。

---

# 五、ownership 总表

这是最关键的部分，直接定死。

## 1）mbuf ownership

### 归属

* `RxIngress.poll_burst()` 返回后，mbuf 暂归 `OwnerLoop`
* `PacketParser/Validator/Admission` 只借用
* `SlotWriter.write()` 完成首次落位或确认丢弃后，mbuf 即可释放

### 禁止

* 不允许把 mbuf 挂到 `CpiContext`
* 不允许旁路直接拿 mbuf 异步录制
* 不允许跨线程传 mbuf

---

## 2）`ParsedPacketView` ownership

### 归属

* 当前调用栈临时对象
* 只在单包处理期间有效

### 禁止

* 不允许保存到 `OwnerState`
* 不允许塞进队列
* 不允许挂输出对象

---

## 3）`CpiContext` ownership

### 归属

* 从 `ctx_pool` 申请后，到 `RECYCLED` 前，全程归 owner 管
* 从创建到封口只允许一个 owner 写。

### 生命周期

* `ACTIVE`
* `DECIDING`
* `SEALED`
* `TOMBSTONE`
* `RECYCLED`

### 禁止

* 不允许下游持有可写引用
* 不允许 sidecar 持有引用
* 不允许第二个线程参与写

---

## 4）`CpiOutput` ownership

### 归属

* 从 `output_pool` 申请后，push 前归 owner
* push 后归下游只读持有
* 消费完成后通过 `ReleaseToken` 返还 owner

### 禁止

* 不允许下游修改裁决结果
* 不允许下游改写上下文
* 不允许把 `CpiOutput` 再传回主链路做状态推进

---

## 5）`OwnerState` ownership

### 归属

* 线程私有
* 只 owner 访问

### 禁止

* 任何模块持久保存 `OwnerState*` 做异步调用
* sidecar 读 `OwnerState` 内部可写字段

---

# 六、推荐的调用链

把一轮单包处理直接写成固定顺序：

```cpp id="ownerloop_flow"
for each mbuf in burst:
    parse -> ParsedPacketView
    validate -> PacketValidity
    if invalid:
        count/drop/free mbuf
        continue

    admission -> AdmissionResult
    switch(admission.kind):
        WRITE_ACTIVE:
            write slot
            if duplicate:
                count/free mbuf
                continue
            update progress
            free mbuf
            if should_finalize:
                finalize
                if should_output:
                    dispatch output
            continue

        TRIGGER_CPI_SWITCH:
            finalize old active ctx with CPI_SWITCH
            if should_output:
                dispatch output
            create new active ctx
            retry current packet once against new active ctx
            continue

        LATE_TO_CLOSED:
            count/drop/free mbuf
            continue

        DROP_NOT_CURRENT / DROP_NO_ACTIVE / DROP_UNTRUSTED:
            count/drop/free mbuf
            continue
```

这个调用链有三个好处：

1. 所有写操作都集中在 owner 内
2. 所有生命周期推进点都清楚
3. `CPI_SWITCH` 也不需要额外线程或复杂窗口

---

# 七、哪些模块必须“纯函数化”，哪些模块必须“有状态”

## 建议做成纯函数/无状态对象

* `PacketParser`
* `PacketValidator`
* `SlotIndex`
* `DecisionPolicy`

理由：这些模块越无状态，越不容易污染 ownership。

## 建议做成有状态对象

* `OwnerLoop`
* `OwnerState`
* `CtxPool`
* `OutputPool`
* `RecentClosedRing`
* `ReleaseChannel`

理由：这些模块天然承载生命周期和资源归属。

---

# 八、必须明确禁止的接口形态

这部分我直接点名，不建议碰。

## 1）禁止“模块直接互相持有对方可写上下文”

比如：

* `ProgressTracker` 长期持有 `CpiContext*`
* `AsyncSidecar` 长期持有 `OwnerState*`
* `OutputDispatcher` 私自回收 `ctx_pool`

这会让 ownership 变脏。

---

## 2）禁止“裁决模块顺手做输出与回收”

`CpiFinalizer` 只负责裁决、封口、边界推进。
不要让它同时：

* 发 SPSC
* 写录制
* 释放上下文

否则后面所有异常恢复都会难看。

---

## 3）禁止“旁路直接消费可写上下文”

旁路必须只消费：

* 最终 `CpiOutput`
* 轻量统计值
* 轻量事件对象

不能消费 `CpiContext&`。现有文档已经明确：旁路只能消费最终对象或必要副本，不接触 owner 可写上下文。

---

## 4）禁止“下游直接归还 `CpiContext*` 到池里”

下游只能发 `ReleaseToken`。
真正的释放动作必须回到 owner。
否则你就把生命周期控制权交出去了。

---

## 5）禁止“主链路内部再加多级工作队列”

当前冻结已经明确：

* 主数据路径只保留最少必要队列
* 默认只在“最终 CPI 输出对象 -> 下游消费者”这一处使用 SPSC。

所以不要再引入：

* 解析队列
* 归属队列
* 重组队列
* 裁决队列

这会直接违背当前主方案。

---

# 九、建议的类职责划分

为了更贴近 C++17 工程实现，建议这样分层：

## A. 数据结构层

* `ParsedPacketView`
* `CpiContext`
* `CpiOutput`
* `ReleaseToken`
* `OwnerCounters`
* `ReasonCode`

## B. 资源层

* `CtxPool`
* `OutputPool`
* `RecentClosedRing`
* `TxSpsc`
* `ReleaseSpsc`

## C. 业务步骤层

* `PacketParser`
* `PacketValidator`
* `CpiAdmission`
* `SlotWriter`
* `ProgressTracker`
* `CpiFinalizer`
* `OutputDispatcher`

## D. 编排层

* `OwnerLoop`

这样分层的价值是：

* 数据结构稳定
* 资源模块负责生命周期
* 业务步骤模块只做一步
* 编排层统一管流程

---

# 十、和“实现级对象模型与内存布局”草案的对应关系

你上一轮那份对象模型草案里有：

* `ParsedPacketView`
* `BoundWaveSnapshotLite`
* `CpiContext`
* `CpiOutput`
* `OwnerState`
* `RecentClosedRing`

这一轮的接口和 ownership 边界，正好把这些对象分别归到了对应模块：

* `ParsedPacketView` -> `PacketParser / PacketValidator / CpiAdmission`
* `BoundWaveSnapshotLite` -> `OwnerState / CpiContext`
* `CpiContext` -> `SlotWriter / ProgressTracker / CpiFinalizer`
* `CpiOutput` -> `OutputDispatcher / 下游消费者`
* `OwnerState` -> `OwnerLoop`
* `RecentClosedRing` -> `CpiAdmission`

也就是说，两轮草案是闭合的，不是两套平行设计。

---

# 十一、推荐的实现优先顺序

如果按编码顺序推进，我建议这样落：

## 第一批：先把“边界定住”

1. `ReasonCode`
2. `ParsedPacketView`
3. `AdmissionResult`
4. `CpiOutput`
5. `ReleaseToken`

## 第二批：把“资源归属定住”

1. `CtxPool`
2. `OutputPool`
3. `TxSpsc`
4. `ReleaseSpsc`
5. `RecentClosedRing`

## 第三批：把“主流程跑通”

1. `PacketParser`
2. `PacketValidator`
3. `CpiAdmission`
4. `SlotWriter`
5. `ProgressTracker`
6. `CpiFinalizer`
7. `OutputDispatcher`

## 第四批：最后写编排

1. `OwnerLoop`
