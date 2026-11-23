# VoiceKey 开源发布步骤

工程目录：`D:\Download\MicYou-VoiceKey`（已 `git init`、已首次提交，分支 `main`）

---

## 一、在 GitHub 建仓库

1. 打开 https://github.com/new
2. **Repository name**：`MicYou-VoiceKey`
3. 可见性：`Public`
4. **不要**勾选 "Add a README / .gitignore / license"（本地已有，避免冲突）
5. 点 **Create repository**

## 二、推送本地代码

在 `D:\Download\MicYou-VoiceKey` 打开 PowerShell：

```powershell
cd D:\Download\MicYou-VoiceKey
git remote add origin https://github.com/Denlist/MicYou-VoiceKey.git
git push -u origin main
```

（若提示登录，用浏览器弹出的 GitHub 授权，或 Personal Access Token 作为密码。）

## 三、打 Release（发布下载产物）

插件市场通过 `releases/latest/download/` 拉取更新，所以必须发 Release，并**上传 2 个 asset**：

1. 打开 https://github.com/Denlist/MicYou-VoiceKey/releases/new
2. **Tag**：`v1.0.0`（新建 tag）
3. **Title**：`VoiceKey v1.0.0`
4. 附件（Assets）上传这两个文件：
   - `D:\Download\MicYou-VoiceKey\dist\plugin.zip`
   - `D:\Download\MicYou-VoiceKey\plugin.json`
5. 点 **Publish release**

> 验证：发布后重启 MicYou，日志里那条 `update check ... 404 Not Found` 告警应消失。

## 四、给插件市场提 PR

市场仓库：https://github.com/MicYou-Dev/MicYou-Plugins

已准备好的提交目录在：`D:\Download\MicYou-VoiceKey\market\plugin\dev.denlist.voicekey\`
包含 4 个文件：`plugin.json`、`plugin.zip`、`preview.png`、`README.md`

两种提法任选其一：

**A. 网页提（最快，无需 git 知识）**
1. 先把 MicYou-Plugins **Fork** 到自己账号
2. 在自己 fork 里进入路径 `plugin/` → **Add file → Upload files**
3. 在上传框里**新建文件夹** `dev.denlist.voicekey`，把上面 4 个文件拖进去上传
4. Commit → 然后回到 MicYou-Plugins 主仓库点 **New pull request**，选择你的 fork 的 `main` 分支

**B. 命令行提**
```powershell
git clone https://github.com/<你的用户名>/MicYou-Plugins.git
cd MicYou-Plugins
New-Item -ItemType Directory -Force plugin\dev.denlist.voicekey
Copy-Item D:\Download\MicYou-VoiceKey\market\plugin\dev.denlist.voicekey\* plugin\dev.denlist.voicekey\
git add plugin/dev.denlist.voicekey
git commit -m "Add plugin dev.denlist.voicekey (VoiceKey)"
git push
```
然后在 GitHub 上对着 MicYou-Plugins 主仓库开 PR。

> **CI 会自动**重新生成 `index.json` 和 README 表格，你不用手动改这两处。

---

## 五、版本升级流程（以后再发新版时）

1. 改 `plugin.json` 的 `version`（如 `1.0.1`）+ 代码
2. 重新构建打包：`pwsh -ExecutionPolicy Bypass -File .\scripts\build.ps1`
3. 同步 `market\plugin\dev.denlist.voicekey\` 里的 4 个文件
4. 打新 Release（tag `v1.0.1`），上传新的 `plugin.zip` + `plugin.json`
5. 给市场仓库提 PR 更新 `plugin.zip` / `plugin.json` / `preview.png`

---

## 附：本机测试结果（已验证）

| 项目 | 结果 |
| --- | --- |
| `micyou-cli plugin validate` | ✅ OK / exit 0 |
| `test\loadprobe.exe`（复刻宿主加载链） | ✅ RESULT: PASS |
| `test\hostsim.exe`（假宿主注入验证） | ✅ 9 passed, 0 failed |
| 本机 MicYou 真实加载 | ✅ DLL locked + 日志 `VoiceKey 已启动` + GUI 不崩溃 |

## 附：构建（需要重编时）

```powershell
# 一键（自动找/下载 zig，编译、校验、打包）
pwsh -ExecutionPolicy Bypass -File .\scripts\build.ps1
```
