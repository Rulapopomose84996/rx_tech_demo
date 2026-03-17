# rx_tech_demo

高性能服务器接收端技术验证 Demo 骨架工程。

## 目标

- 统一 benchmark core
- 统一后端接口：socket / AF_XDP / DPDK
- 统一处理模式：RX-only / Parse / SPSC
- 统一场景、指标和结果输出

## 当前状态

当前仓库已落地第一版项目骨架：

- 顶层 CMake 与模块拆分
- benchmark core 占位实现
- socket / AF_XDP / DPDK 后端占位
- unit / integration 测试骨架
- 场景、脚本、部署、文档模板
- 服务器工作区已完成 clone：`/home/devuser/WorkSpace/rx_tech_demo`
- 本项目共享缓存命名空间已完成建立：`/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo`
- 已提供 AF_XDP / DPDK 环境自检脚本
- 已提供最小 BPF / XDP attach / AF_XDP bind 验证脚本
- 已在服务器上完成一轮最小 AF_XDP 实测闭环
- AF_XDP backend 已编进正式工程并在服务器上构建通过
- `libxdp 1.2.9` 已通过共享缓存前缀补齐

当前代码侧已进一步补齐：

- CLI 已支持 `--dry-run`，可在本地只解析配置与场景，不启动真实 backend
- benchmark core 已支持 step 级执行结果输出，产物包含 `summary.json/csv` 与 `steps.json/csv`
- `summary` 已增加运行状态、backend 可用性与 CPU 指标可用性标记，避免本地伪成功结果混入正式数据
- `run_matrix.py --dry-run` 已可校验二进制、配置和场景路径
- `report_compare.py` 已可同时聚合 summary 与 step 级结果

## 无服务器阶段

当暂时无法使用服务器时，当前推荐目标不是给出正式性能结论，而是优先推进：

- 配置与场景装载
- benchmark core 的 step 执行与结果格式
- `socket` 本地 loopback 运行链路
- `AF_XDP` / `DPDK` 的失败路径和不可用状态表达
- 脚本 dry-run 与结果聚合能力

当前不应在无服务器阶段宣称完成：

- `AF_XDP` 真实 ingress 闭环
- `DPDK` 正式 benchmark 实测
- 基于真实 NUMA / RSS / IRQ 固化后的公平性结论

本地推荐验证方式：

- 优先在 `WSL/Linux` 中执行 `scripts/build_production.sh`
- 需要只检查参数时，执行 `rxbench_* --dry-run`
- 需要校验矩阵编排时，执行 `python3 scripts/run_matrix.py --dry-run`
- 需要聚合现有本地结果时，执行 `python3 scripts/report_compare.py`

## 开发约束

- 禁止在本地 Windows 工作区直接编译
- 代码改动后按功能分类提交
- 远程验证以 `ssh kds` 登录服务器为准
- 服务器工作区统一使用 `/home/devuser/WorkSpace/rx_tech_demo`
- 构建时统一使用 `/home/devuser/WorkSpace/ThirdPartyCache` 作为共享第三方依赖缓存
- 本项目专用缓存命名空间为 `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo`

## 远程工作流

1. 本地编辑并按功能分类提交
2. 推送到远程仓库
3. 通过 `ssh kds` 登录服务器
4. 在 `/home/devuser/WorkSpace/rx_tech_demo` 下 pull 项目
5. 使用共享第三方缓存执行服务器构建
6. 在服务器上运行测试或压测

## 服务器构建

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/build_server_shared_cache.sh
```

## 服务器测试

Linux server:

说明：

- 麒麟 V10 ARM64 服务器上的旧版 `CTest` 不稳定，不以顶层递归发现作为正式测试入口
- 正式测试入口固定为 `build/tests/unit` 与 `build/tests/integration`
- 进入对应测试目录后执行 `ctest --output-on-failure`

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
cd build/tests/unit
ctest --output-on-failure

cd /home/devuser/WorkSpace/rx_tech_demo
cd build/tests/integration
ctest --output-on-failure
```

## 环境自检

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
./scripts/check_af_xdp_env.sh enP1s25f3 0
./scripts/check_dpdk_env.sh 0001:05:00.3
```

当前推荐决策：

- AF_XDP 测试口：`enP1s25f3`
- DPDK 解绑测试口：`enP1s25f3`
- AF_XDP 依赖管理：`libbpf` 系统安装 + `libxdp` 共享缓存前缀
- DPDK 依赖管理：共享缓存离线化优先
- `2026-03-17` 已确认 `enP1s25f3` 可长期作为专用实验口，并允许在正式 DPDK 压测窗口继续执行驱动重绑
- AF_XDP / DPDK 基线真源文档：`docs/设计方案/AF_XDP_DPDK_准备与分工.md`

当前 AF_XDP 最小实测结果：

- `libbpf 0.8.1` 已安装并可用
- `libxdp 1.2.9` 已安装到 `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix`
- 最小 `.bpf.o` 已在服务器上成功生成
- 最小 XDP 程序已成功 attach 到 `enP1s25f3`
- 最小 AF_XDP socket bind probe 已成功绑定 `enP1s25f3 queue 0`
- 收包级别 AF_XDP RX PoC 已成功运行 2 秒轮询
- 实验结束后已将目标口上的 XDP 程序卸除
- `rxbench_xdp` 与 `run_af_xdp_benchmark.sh` 已可在服务器上运行

## 最小 AF_XDP 验证

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
./scripts/compile_min_xdp.sh
./scripts/attach_min_xdp.sh enP1s25f3
bpftool net
./scripts/build_af_xdp_bind_probe.sh
./build_af_xdp_probe/af_xdp_bind_probe enP1s25f3 0
./scripts/detach_xdp.sh enP1s25f3
```

## AF_XDP RX PoC 与 Benchmark

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/build_af_xdp_rx_poc.sh
sudo ./build_af_xdp_probe/af_xdp_rx_poc enP1s25f3 0 2
sudo ./scripts/run_af_xdp_benchmark.sh enP1s25f3 0 2 rx_only results/af_xdp_benchmark_script
```
