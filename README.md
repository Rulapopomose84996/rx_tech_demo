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

## 开发约束

- 禁止在本地 Windows 工作区直接编译
- 代码改动后按功能分类提交
- 远程验证以 `ssh kds` 登录服务器为准
- 服务器工作区统一使用 `/home/devuser/WorkSpace/rx_tech_demo`
- 构建时统一使用 `/home/devuser/WorkSpace/ThirdPartyCache` 作为共享第三方依赖缓存

## 远程工作流

1. 本地编辑并按功能分类提交
2. 推送到远程仓库
3. 通过 `ssh kds` 登录服务器
4. 在 `/home/devuser/WorkSpace` 下 clone 或 pull 项目
5. 使用共享第三方缓存执行服务器构建
6. 在服务器上运行测试或压测

## 服务器构建

Linux server:

```bash
cd /home/devuser/WorkSpace/rx_tech_demo
./scripts/build_server_shared_cache.sh
```
