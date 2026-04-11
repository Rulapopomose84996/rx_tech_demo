#!/bin/bash
# 更新并同步到两个远程仓库

BRANCH=${1:-$(git branch --show-current)}

echo "正在从 gitea 拉取最新代码..."
git fetch gitea

echo "正在合并 gitea/${BRANCH}..."
git merge gitea/${BRANCH}

echo "推送到 gitea..."
git push gitea ${BRANCH}

echo "推送到 github..."
git push github ${BRANCH}

echo "同步完成！"
