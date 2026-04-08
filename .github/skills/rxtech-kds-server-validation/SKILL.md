---
name: rxtech-kds-server-validation
description: 'Use when validating rx_tech_demo changes on the Linux server through ssh kds, syncing local Windows edits to /home/devuser/WorkSpace/rx_tech_demo, choosing between scp or git-based sync, or running remote build/test steps after local code changes. Trigger on requests like "服务器验证", "ssh kds 测试", "同步到服务器跑一下", "本地改完上传验证", or "远程构建".'
argument-hint: '修改范围、同步方式偏好、远程要执行的验证目标'
---

# RxTech KDS Server Validation

这个技能用于 rx_tech_demo 项目的标准验证流程：

- 在 Windows 本地阅读和修改代码。
- 通过 ssh kds 把变更同步到 Linux 服务器。
- 在服务器上的 /home/devuser/WorkSpace/rx_tech_demo 执行构建、测试或运行验证。
- 仅把 Linux 服务器结果视为权威结论。

## When to Use

- 用户要求到服务器验证当前修改。
- 用户要求先在本地改代码，再上传到服务器跑构建或测试。
- 用户提到 ssh kds、scp、远程构建、远程测试、服务器联调。
- 需要明确区分本地编辑环境和 Linux 服务器验证环境。
- 需要在同步方式之间做选择：git push/pull、scp、rsync、隔离目录拷贝。

## Project Rules

- 先读取仓库根目录的 AGENTS.md，并以其中规则为最高优先级。
- 不要把 Windows 本地构建、IDE 静态分析或干跑结果当作最终验证。
- 所有权威性的 build、test、runtime、integration 结论都必须来自 Linux 服务器。
- 默认远程入口是 ssh kds。
- 远程项目主路径是 /home/devuser/WorkSpace/rx_tech_demo。
- 如果 ssh kds 在项目命令执行前就不可用，才可以按仓库文档考虑 WSL 兜底路径，并且必须明确说明这不是权威服务器验证。

## Security Rules

- 不要把明文密码写入仓库文件、SKILL.md、脚本或命令日志。
- 如果服务器使用口令登录，只通过交互式输入完成，不在命令中回显或持久化。
- 优先使用现有的 ssh 配置、密钥或 ssh-agent。

## Procedure

1. 读取当前仓库状态。
   - 确认本地修改范围、目标模块、需要验证的行为。
   - 读取 AGENTS.md 以及当前任务直接相关的活动文档。
   - 不要依赖归档文档，除非当前文档缺失关键信息。

2. 确认验证目标。
   - 明确是构建验证、单元测试、集成测试、运行验证，还是闭环联调。
   - 明确哪些结论必须依赖服务器结果才能成立。

3. 检查远程连通性。
   - 先用非破坏性方式验证 ssh kds 可达。
   - 如果 ssh kds 不可用，并且问题发生在项目命令执行之前，明确报告阻塞点，再决定是否转入文档允许的兜底路径。

4. 选择同步方式。
   - 默认判断是否需要让服务器工作区直接吸收本地变更。
   - 常规分支工作流：优先 git push 加服务器侧 git pull 或等价安全同步方式。
   - 小范围变更、一次性验证、或不希望扰动远程工作区：优先 scp、rsync 或隔离目录拷贝。
   - 只在确认目标路径和覆盖范围安全时执行文件同步。

5. 显式执行同步。
   - 在执行远程验证前，明确说明同步方法、源路径、目标路径。
   - 如果同步到主工作区，目标路径默认是 /home/devuser/WorkSpace/rx_tech_demo。
   - 如果同步到隔离目录，必须说明隔离目录的目的和与主工作区的区别。

6. 在服务器上执行验证。
   - 进入明确的远程目录后再运行命令。
   - 构建和测试命令优先从 README、AGENTS.md 和当前有效设计/环境文档中获取。
   - 不要凭空发明构建命令、运行参数或测试入口。
   - 对可能超过 1 分钟的长时验证，默认先说明命令、用途和预期证据，再决定是否执行。

7. 按真实边界报告结果。
   - 明确每个步骤运行在 Windows、本地 WSL 还是 Linux 服务器。
   - 明确使用了哪种同步方式，以及服务器上的实际代码路径。
   - 明确本次结果是权威服务器验证，还是仅限兜底验证。
   - 如果外部 sender、真实网卡链路或闭环环境不可用，要把验证边界说清楚，不能把阶段性结果说成最终闭环成功。

## Decision Points

### Sync Method Choice

- 满足常规分支开发、远程仓库状态可控时：优先 git push/pull。
- 只改了少量文件，或远程目录不应被整体更新时：优先 scp 或 rsync。
- 需要避免污染远程主工作区时：优先同步到隔离验证目录。

### Validation Depth Choice

- 只需确认代码可编译：执行最小构建验证。
- 需要确认行为不回归：执行对应的单元测试或集成测试。
- 需要确认真实链路：只有在 sender、网络路径和运行依赖都满足时，才执行运行态联调。

### Fallback Choice

- 只有在 ssh kds 在项目命令开始前不可用时，才考虑 WSL 兜底。
- 一旦进入兜底路径，必须显式标注结果级别低于服务器验证。

## Completion Checklist

- 已说明本地修改范围和验证目标。
- 已说明使用的文档依据。
- 已说明同步方式以及为什么这样选。
- 已说明服务器目标路径是否为 /home/devuser/WorkSpace/rx_tech_demo 或隔离目录。
- 已说明每条关键命令运行在哪个环境。
- 已明确结果是否来自 Linux 服务器权威验证。
- 已明确未验证部分和剩余风险。

## Output Expectations

输出应至少包含：

- 本次验证目标。
- 同步策略和远程路径。
- 实际执行的服务器命令或未执行原因。
- 结果结论与验证边界。
- 后续还需要的补充验证。
