# BUILD

## 原则

- 本项目禁止本地 Windows 编译
- 默认验证环境为 Linux server
- 服务器工作目录固定为 `/home/devuser/WorkSpace/rx_tech_demo`
- 第三方依赖缓存固定为 `/home/devuser/WorkSpace/ThirdPartyCache`
- 本项目专用缓存目录固定为 `/home/devuser/WorkSpace/ThirdPartyCache/rx_tech_demo`

## 服务器当前状态

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
git status --short --branch
```

当前服务器侧已完成：

- 项目 clone
- 专用共享缓存目录创建
- 缓存 README 初始化

## 服务器构建

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/build_server_shared_cache.sh
```

## 服务器测试

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
ctest --test-dir build/tests/unit --output-on-failure
ctest --test-dir build/tests/integration --output-on-failure
```

## 环境自检

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/check_af_xdp_env.sh enP1s25f3 0
./scripts/check_dpdk_env.sh 0001:05:00.3
```

## 本地职责

- 本地只负责编辑、静态检查、分类提交
- 需要结果时，统一走服务器 pull 后构建验证
