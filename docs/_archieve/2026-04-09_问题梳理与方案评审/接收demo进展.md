## 环境漂移
ns_sender 里的 receiver1: UP, LOWER_UP，10.0.0.1/24，Speed 10000Mb/s，Duplex Full，Link detected: yes
ns_receiver 里的 receiver3: UP, LOWER_UP，10.0.0.2/24，Speed 10000Mb/s，Duplex Full，Link detected: yes

## 任务 4：把业务逻辑继续下沉到 `CpiStateCoordinator`，但保持热路径扁平

### 目标

不搞“事件总线化”，只做**职责收口**：

* `OwnerLoop` 只负责驱动流程
* `CpiStateCoordinator` 成为业务状态机主入口
* control/data 仍走两条直接路径
* 不新增线程
* 不新增队列
* 不新增动态分配

### 你要完成的内容

1. 清理 `OwnerLoop` 里与 CPI 业务有关的细节判断。
   目标是让它少知道：

   * 控制包如何绑定到 active CPI
   * 数据包如何触发 switch / late / finalize
   * 哪些 trigger 属于业务层决策

2. 把这些判断继续下沉到 `CpiStateCoordinator`：

   * `process_control_packet(...)`
   * `process_data_packet(...)`
   * 必要时补少量私有辅助函数，但不要搞统一事件框架

3. 保持热路径形式不变：

   * control 包直接调用 control 入口
   * data 包直接调用 data 入口
   * 不加 `ReceiverEvent` 队列，不加中间 bus

4. 把 finalize / switch / late packet 相关的业务决策点集中到 coordinator 内部。
   目标是以后看 CPI 生命周期时，只看 coordinator 就够了。

5. 顺手统一 coordinator 内部的状态命名和职责边界，避免：

   * snapshot 绑定逻辑散落
   * finalize 触发散落
   * previous/active 双窗口逻辑散落

### 验收标准

* `OwnerLoop` 更薄，但吞吐路径不增加新层
* `CpiStateCoordinator` 成为唯一 CPI 业务中枢
* control/data 热路径仍是直接函数调用
* 现有 DPDK / Socket 两后端行为不变

---

## 任务 5：做热路径性能加固，建立 DPDK/Socket 双后端下的实时性基线

### 目标

既然你已经有两种 ingress 了，下一步就不该继续“抽象”，而该做**性能定型**。
也就是把系统在高吞吐下最容易抖动的点先钉住。

### 你要完成的内容

1. 梳理并标记热路径上的关键开销点：

   * backend `recv_burst`
   * `PacketPipeline`
   * `process_control_packet`
   * `process_data_packet`
   * `SlotWriter`
   * `ProgressTracker`
   * `finalize_active`
   * `output_ring` / recycle 路径

2. 为 DPDK 和 Socket 两个后端分别补最小性能统计：

   * 每轮 burst 包数
   * 空轮询次数
   * 平均/峰值 burst 大小
   * backend drops / errors
   * 有效包率、过滤率、丢弃率

3. 检查并消除热路径里的明显性能雷点：

   * 不必要的拷贝
   * 不必要的字符串构造
   * 热路径日志输出
   * 每包临时对象膨胀
   * 重复分支判断
   * 可合并的元数据访问

4. 对 Socket 后端重点做两件事：

   * 检查 burst 内存复用是否稳定
   * 检查 `release_burst()` 是否真的低成本
   * 确认没有“每包 malloc/free”这类问题

5. 对 output 路径做背压体检：

   * `output_ring` 满时系统表现是否可控
   * finalize 后是否会拖慢收包主线
   * control 包突发时是否影响数据包主链

6. 补一组面向性能的验证任务，不一定是 benchmark 工具，但至少有：

   * DPDK 路径跑通并出统计
   * Socket 路径跑通并出统计
   * 两后端同协议、同 duration 下可比较
   * 能观察 burst 行为、drop 行为、空轮询行为

### 验收标准

* 你能说清楚当前瓶颈更偏 backend、protocol 还是 coordinator
* DPDK/Socket 两路径都有统一可读的实时性统计
* 热路径没有明显的结构性性能坑
* 后面再做优化时，有基线可对比，不再靠感觉改
