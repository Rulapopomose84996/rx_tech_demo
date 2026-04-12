# devuser 免密 sudo 配置说明

## 目标

为 `kds` 服务器上的 `devuser` 配置受控的免密 `sudo`，让 AI 在常见运维和验证任务中不必反复询问密码，同时保留对高风险破坏性操作的限制。

## 作用范围

- 仅作用于 `kds` 这台服务器
- 仅作用于用户 `devuser`
- 不写入仓库、不落盘保存 sudo 密码
- 不向 `main` 分支或其他机器同步这份服务器本地权限配置

## 当前实现

配置文件：

- `/etc/sudoers.d/devuser-codex`

安装方式：

- 由 `root` 在服务器本地安装
- 安装后使用 `visudo -cf /etc/sudoers.d/devuser-codex` 校验语法

## 权限策略

当前策略分成三类：

1. 只读运维命令
1. 受控服务管理命令
1. 少量受控配置编辑入口

### 1. 只读运维命令

允许 `devuser` 免密执行以下类别命令：

- `systemctl status *`
- `journalctl *`
- `ip *`
- `ethtool *`
- `ss *`
- `timedatectl`
- `hostnamectl`
- `firewall-cmd *`
- `nmcli *`
- `ls *`
- `cat *`
- `grep *`
- `find *`
- `head *`
- `tail *`
- `df`
- `du`
- `free`
- `ps`

这些命令用于检查服务状态、网络、端口、日志、磁盘和系统基本信息。

### 2. 受控服务管理命令

允许 `devuser` 免密执行以下类别命令：

- `systemctl start *`
- `systemctl stop *`
- `systemctl restart *`
- `systemctl reload *`
- `systemctl daemon-reload`
- `systemctl enable *`
- `systemctl disable *`
- `firewall-cmd --reload`
- `nmcli connection up *`
- `nmcli connection down *`

这些命令覆盖常见的服务重载、启停和网络连接切换，但没有放开任意 shell、任意文件写入或任意包管理操作。

### 3. 受控配置编辑入口

允许 `devuser` 使用 `sudoedit` 编辑以下范围内的配置：

- `/etc/systemd/system/*`
- `/etc/sysctl.d/*`
- `/etc/NetworkManager/system-connections/*`
- `/etc/chrony.conf`
- `/etc/firewalld/zones/*`

这类入口用于服务器运维配置调整，但仍然限制在指定目录和文件类型内。

## 明确不放开的能力

以下能力当前仍然不开放：

- 任意 `root` shell
- 任意 `sudo bash`
- 任意 `sudo sh`
- 任意删除系统文件
- 任意写入 `/etc` 的所有路径
- 任意包管理安装或卸载
- 任意格式化、挂载、分区、清盘操作

## 验证方式

可以通过以下方式确认配置有效：

```bash
sudo -n systemctl status sshd
sudo -n /bin/rm /etc/hosts
```

预期结果：

- 第一条应当成功
- 第二条应当被拒绝

## 维护建议

- 如果后续需要新的运维命令，优先逐条加入白名单，不要扩大到完整 root 权限
- 如果某类命令频繁出现报错，先检查是否属于应放行的运维场景，再决定是否加入白名单
- 如果确实需要更大权限，优先按命令粒度收敛，而不是开放整类 shell 入口

## 当前结论

这份配置能满足当前服务器上的常规自动化运维和验证需求，同时把高风险破坏性操作保留在白名单之外。
