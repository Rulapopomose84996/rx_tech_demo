# BUILD

## 原则

- 本项目禁止本地 Windows 编译
- 默认验证环境为 Linux server
- 服务器工作目录固定为 `/home/devuser/WorkSpace/rx_tech_demo`
- 第三方依赖缓存固定为 `/home/devuser/WorkSpace/ThirdPartyCache`

## 服务器首次准备

```bash
cd /home/devuser/WorkSpace
git clone <your-repo-url> rx_tech_demo
```

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

## 本地职责

- 本地只负责编辑、静态检查、分类提交
- 需要结果时，统一走服务器 clone 或 pull 后构建验证
