# rx_tech_demo

`rx_tech_demo` 现在按“纯接收流程”组织源码，当前主线是 **DPDK 接收端**。

## 当前定位

- 这是一个接收端软件雏形，不再是多技术栈 benchmark 框架
- 当前主实现只服务 `DPDK`
- `AF_XDP` 代码仍保留，但已降级到 `src/legacy/af_xdp`
- 当前成功标准是：稳定收包、写入内存、旁路录制保存、输出轻量统计

## 当前源码结构

```text
src/
  receiver/
    app/
    ingress/dpdk/
    protocol/
    runtime/
    sidecar/
    storage/
  legacy/
    af_xdp/
```

说明：

- `src/receiver` 是当前主线
- `src/receiver/ingress/dpdk` 是唯一主接收实现
- `src/receiver/runtime` 负责配置与接收运行时
- `src/receiver/protocol` / `storage` / `sidecar` 负责协议、存储和从属观测
- `src/legacy/af_xdp` 仅保留兼容和参考价值，不再是主线

## 本地构建

Windows PowerShell，执行目录：`D:\WorkSpace\Company\Tower\rx_tech_demo`

```powershell
Set-Location "D:\WorkSpace\Company\Tower\rx_tech_demo"
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

## 服务器构建

Linux server，执行目录：`/home/devuser/WorkSpace/rx_tech_demo`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
bash ./scripts/build_server_shared_cache.sh
```

## 服务器测试

Linux server，执行目录：`/home/devuser/WorkSpace/rx_tech_demo/build`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/build
ctest --output-on-failure
```

## 运行入口

当前主入口：

- `rx_receiver_dpdk`
- 兼容保留：`rxbench_dpdk`

Legacy 入口：

- `rx_receiver_af_xdp_legacy`
- 兼容保留：`rxbench_xdp`

## 样本数据

本地可用样本：

- `data/cpi_0002_complete`

该样本可用于配置检查、协议数据核对和接收链路验证。
