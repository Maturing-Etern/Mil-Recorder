#!/usr/bin/env bash
# 一键推送 Mil-Recorder 的「无注释 + 无 milize_js」版本到 GitHub main。
# 2026-09-20 生成。当时 WorkBuddy 沙箱代理对 github.com:443 返回 502，推送未能完成。
# 在普通终端（Clash Verge 开启、或直连可用）里跑这个脚本即可。
set -e
export PATH="/c/Users/Administrator/.workbuddy/binaries/PortableGit/versions/1.2.0/cmd:$PATH"
cd "E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder"

echo "本地 HEAD: $(git rev-parse HEAD)"
echo "远端 main: $(git ls-remote origin refs/heads/main | cut -f1)"

# 远端 e2e63e9 是旧内容的 squash 快照，与本地历史分叉；用 force-with-lease 覆盖。
git push --force-with-lease=main:e2e63e9569016b7e5d9035ac025689a1d928c77a origin main

echo "--- 校验 ---"
git ls-remote origin refs/heads/main
echo "期望: 18b4a7a814932312834be13d40501e09d95d2c63"
