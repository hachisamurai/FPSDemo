# Git版本管理

## 目的与结构

项目使用Git记录C++、配置、工具、说明文档与UE资产；远端为 https://github.com/hachisamurai/FPSDemo.git，默认分支main。UE资产及原始音频/图片/模型通过Git LFS保存，普通Git记录指针，避免每次修改都增加完整二进制历史。

## 首次获取与使用

安装Git及Git LFS后执行：

```powershell
git lfs install
git clone https://github.com/hachisamurai/FPSDemo.git
cd FPSDemo
git lfs pull
```

右键FPSDemo.uproject生成IDE工程，使用UE5.4.4编译FPSDemoEditor，再打开项目。FPSDemo.sln和Binaries/Intermediate由本机重新生成，不从Git获取。后端真实环境配置按Backend/.env.local.example在本地配置，不能提交密码。

修改并保存Editor资源后执行`git status`检查、`git add`暂存、`git commit`提交，再`git push`推送。需要获取他人改动时先保存并提交本地工作，再执行`git pull --ff-only`；发生分叉应明确合并或重放本地提交，不强制覆盖远端。二进制资源多人同时修改应提前协调，同一资产不能依靠文本合并解决冲突。

## 忽略范围与边界

根目录.gitignore排除UE缓存/构建输出、Saved内全部存档/日志/截图/打包文件及云身份、IDE工作区、Python缓存和后端bin/obj/TestResults。Build中的FileOpenOrder属于Cook记录，同样忽略；其他Build源文件仍可提交。真实.env、私钥/证书、云身份迁移包排除，.env示例模板保留。源码、Config、Content、SourceAssets、Art、Tools、Scripts及Documentation仍纳入版本控制。

.gitattributes声明二进制LFS类型，不生成或修改.uasset内容。LFS客户端或认证不可用时，资源可能只有文本指针，需要`git lfs pull`；不要把指针文件当作UE资源打开。若GitHub拒绝LFS上传，应先解决账号权限/配额，再重试push；本地提交仍保留，不能宣称远端备份成功。此规则不会删除本机被忽略的文件。

## 验证与维护

检查`git remote -v`、`git status --short --branch`、`git lfs ls-files`和`git lfs fsck`；推送后用`git ls-remote origin refs/heads/main`比较本地HEAD。首次提交前检查暂存文件列表，确认Saved、真实.env.local和编译产物没有进入索引。新增二进制格式时先更新.gitattributes再暂存，新增秘密文件类型时同步忽略规则；如果秘密已提交，新增ignore不能清除历史，必须撤销相关凭据并处理历史。

影响文件：.gitignore、.gitattributes、本文及README文档入口。Git版本管理只影响协作和备份，不修改游戏逻辑或存档规则。
