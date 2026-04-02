# Phase 3 Closeout Plan

## Goal

在不改变当前主线边界的前提下，完成 Phase 3 收尾工作：

- 清理非阻塞构建告警
- 同步项目文档到当前实现事实
- 补齐真实接收解析联调所需 sender
- 保持 Linux 服务器构建与测试通过

## Current Status

- [completed] 清理 `RXTECH_THIRD_PARTY_CACHE` 未使用告警
- [completed] 清理 legacy AF_XDP deprecation 告警噪声
- [completed] 修复 Release 下 `test_arp_responder` 的假阳性问题
- [completed] 新增协议正确 sender：`tools/rxtech_protocol_sender.py`
- [completed] 服务器构建复验
- [completed] 服务器 unit / integration 测试复验
- [completed] README、实现说明、进度与 findings 文档同步
- [completed] 分类提交全部修改并推送到 `gitea` / `github`
- [completed] 服务器主工作区拉取目标分支

## Key Decisions

- 继续坚持 Linux server-first 验证，不把 Windows 侧行为写成权威结果。
- 对 legacy AF_XDP 的处理以“局部降噪、不改主线语义”为原则。
- 真实解析联调依赖协议正确 sender，因此新增 sender 工具比继续复用旧的 raw sender 更稳妥。
- 文档统一以当前 `src/receiver` DPDK 主线和 Phase 3 热路径骨架为准。
