# rx_tech_demo

高性能服务器接收端技术验证工程，当前唯一主线为 **AF_XDP**。

## 当前目标

- 以 `rxbench_xdp` 作为唯一接收入口
- 复用通用 `benchmark_core` 完成 `DemoHeader` 解析、重组、指标与结果输出
- 在服务器上直接围绕真实网口做 AF_XDP 环境验证、PoC 和联调

## 当前状态

- `socket` 接收主线已从构建入口中移除
- `rxbench_xdp` 已保留为唯一推荐接收入口
- `DemoHeader` 解析、block 重组、per-port 指标和结果写出仍保留
- 前台长时间模式已支持 `--until-stopped`，并每 10 秒输出一次状态快照
- AF_XDP 当前平台结论以 `driver + copy` 为现实目标

## 服务器工作区

- 主仓库：`/home/devuser/WorkSpace/rx_tech_demo`
- AF_XDP 隔离 worktree：`/home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver`

后续所有 AF_XDP 改动、验证与联调均在该 worktree 中完成。

## 服务器构建

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
bash ./scripts/build_server_shared_cache.sh
```

## 服务器测试

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver/build/tests/unit
ctest --output-on-failure

cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver/build/tests/integration
ctest --output-on-failure
```

## AF_XDP 自检

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
bash ./scripts/check_af_xdp_env.sh receiver0 0
bash ./scripts/compile_min_xdp.sh
bash ./scripts/build_af_xdp_bind_probe.sh
bash ./scripts/build_af_xdp_rx_poc.sh
```

## 前台运行

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo/.worktrees/af-xdp-receiver
PREFIX=/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo/build/native-aarch64/xdp-tools-1.2.9-prefix
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
./build/src/apps/rxbench_xdp --config ./configs/af_xdp_receiver0.conf --until-stopped
```

说明：
- 该命令前台占据当前终端
- 每 10 秒打印一次状态快照
- 通过 `Ctrl+C` 手动停止
