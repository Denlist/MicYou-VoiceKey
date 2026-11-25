# VoiceKey 开源发布步骤（符合 MicYou 作者审计意见）

工程目录：`D:\Download\MicYou-VoiceKey`（已 `git init`、分支 `main`、远程 `origin` 已配好）
当前版本：**v1.1.1**

> 本版按 MicYou 作者邮件回复整改：
> ① 市场仓库**只提交 manifest + 缩略图 + 源码**，**不提交二进制**；
> ② 删除死代码，能力声明与代码实际调用严格一致（去掉从未调用的 `event.emit`）；
> ③ 用 **GitHub Actions CI runner 自动构建并发布二进制**，不再手动打包上传。

---

## 一、源码仓库（一次性，已完成）

远程：https://github.com/Denlist/MicYou-VoiceKey

推送更新：

```powershell
cd D:\Download\MicYou-VoiceKey
git push origin main
```

## 二、CI 自动构建发布（已完成，以后每发新版照做）

`.github/workflows/release.yml` 已就位：**推 `v*` 标签即自动**在 GitHub 的 CI runner 上用 zig 从源码交叉编译、打包 `plugin.zip`、并创建 Release 上传 `plugin.zip` + `plugin.json`。

以后发新版的流程：

```powershell
cd D:\Download\MicYou-VoiceKey
# 1) 改版本号：plugin.json 的 version、src/voicekey.c 的 PLUGIN_VERSION（两处保持一致）
#    （market 目录里的 plugin.json / src 也要同步）
# 2) 提交
git add -A; git commit -m "vX.Y.Z: ..."; git push origin main
# 3) 打 tag 触发 CI 构建发布（关键：tag 名以 v 开头）
git tag -a vX.Y.Z -m "VoiceKey vX.Y.Z"; git push origin vX.Y.Z
```

到仓库 **Actions** 页可以看构建进度；成功后 **Releases** 页会自动出现带 `plugin.zip` + `plugin.json` 的发布。

> 提示：`releases/latest/download/plugin.zip` 有 CDN 缓存，刚发完可能短时间内仍指向旧版，属正常。

## 三、给插件市场提 PR

市场仓库：https://github.com/MicYou-Dev/MicYou-Plugins
贡献要求：https://github.com/MicYou-Dev/MicYou-Plugins/blob/main/CONTRIBUTING.md

**提交目录**：`D:\Download\MicYou-VoiceKey\market\plugin\dev.denlist.voicekey\`
里面**只有 5 个文件**（无二进制）：

```
plugin.json              # 清单
preview.png              # 缩略图（640×360）
README.md                # 说明
src/voicekey.c           # 源码（供审计）
src/micyou_plugin_abi.h  # 源码
```

> ⚠️ **不要放 `plugin.zip`**。二进制由 CI 构建并挂在源码仓库的 Release 上，市场清单里的 `downloadUrl` 已指向 `releases/latest/download/plugin.zip`。

### 网页提法（最快）

1. 先把 MicYou-Plugins **Fork** 到自己账号（Denlist/MicYou-Plugins）
2. 进入 fork 的 `plugin/dev.denlist.voicekey/` 目录
3. **删掉**里面旧的 `plugin.zip`（如已上传过）
4. **Add file → Upload files**，把 `market\plugin\dev.denlist.voicekey\` 下的 5 个文件（含 `src/` 文件夹）拖进去，覆盖旧文件
5. Commit → 回到你的 fork 页面，点 **Contribute → Open pull request**，base 选 `MicYou-Dev/MicYou-Plugins:main`
6. 标题写 `Add plugin dev.denlist.voicekey (VoiceKey)`，提交

> CI 会自动重新生成 `index.json` 和根 README 表格，不用手动改。

### 命令行提法

```powershell
git clone https://github.com/Denlist/MicYou-Plugins.git
cd MicYou-Plugins
# 清掉旧的二进制（若有）
Remove-Item plugin\dev.denlist.voicekey\plugin.zip -ErrorAction SilentlyContinue
Copy-Item D:\Download\MicYou-VoiceKey\market\plugin\dev.denlist.voicekey\* plugin\dev.denlist.voicekey\ -Recurse -Force
git add plugin/dev.denlist.voicekey
git commit -m "Add plugin dev.denlist.voicekey (VoiceKey)"
git push
# 然后到 GitHub 对 MicYou-Dev/MicYou-Plugins 开 PR
```

---

## 附：本机测试结果（v1.1.1，已验证）

| 项目 | 结果 |
| --- | --- |
| `micyou-cli plugin validate` | ✅ OK / exit 0（version=1.1.1，capabilities 3 项） |
| `test\loadprobe.exe`（复刻宿主加载链） | ✅ RESULT: PASS |
| `test\hostsim.exe`（假宿主注入验证） | ✅ 9 passed, 0 failed |
| GitHub Actions 构建 | ✅ success（v1.1.1） |
| Release 产物 | ✅ plugin.zip(110872B) + plugin.json，无 pdb/lib |

## 附：本地构建（需要重编时）

```powershell
# 一键（自动找/下载 zig，编译、校验、打包）
pwsh -ExecutionPolicy Bypass -File .\scripts\build.ps1
```
