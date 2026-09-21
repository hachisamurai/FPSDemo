# MongoDB 玩家存档与同步：实际实现

## 目的与范围

本模块已实现 UE 5.4 → 独立 ASP.NET Core API → MongoDB Atlas。用户确认同步永久武器解锁、通关记录、最近主武器偏好，以及三个战役检查点槽。本文替代原先仅有设计的版本；启动与实际验证见《云存档运行与验证.md》。

按最新要求，编辑器目标硬性只使用本地存档。WITH_EDITOR下两个子系统的ShouldCreateSubsystem都返回false，PIE、Editor Standalone及Editor-Cmd -game均不登录/上传/下载；只有不含编辑器的打包游戏目标才启用本文云流程。HUD通过子系统是否存在隐藏编辑器云入口。

永久进度和战役检查点生命周期独立：新建存档只生成手枪，同槽死亡/放弃重开保留金币余额、金币永久成长/逐项价格及实际已装备主枪/库存/主副槽选择，只重置银币和临时成长。装备复用V5既有Weapons/PrimaryId/ActiveSlot，随重置后的Hub检查点同步，不新增协议字段；账号偏好不能代替本槽配装。下载解锁不会自动发枪。战斗中保存进入战斗前的检查点和挑战资格，不上传Actor、任意位置、临时GameplayEffect或蓝图路径。

依赖：UE HTTP/Json/JsonUtilities、Windows DPAPI；独立.NET 10、MongoDB.Driver 3.12.0；支持多文档事务的MongoDB副本集/Atlas。服务不会自动改变副本集，也不drop数据库。MongoDB URI只在后端环境中，UE包不携带数据库凭据。

## 架构与设计理由

```text
UDemoPlayerProfile / UDemoRunSaves：本地数据与校验
  ↕
UDemoCloudSync：耐久发件箱、合并、冲突、UI状态
  ↕
FPSDemoOnline / UDemoApiClient：HTTP、超时、取消、DPAPI凭据
  ↕ 本机HTTP / 远端HTTPS
ASP.NET Core Endpoints → ProfileService → MongoClient连接池
  ↕ 同一session事务
MongoDB：档案 + 通关凭据 + 幂等回执 + 绑定水位
```

Online是独立Runtime模块，避免把MongoDB依赖放进游戏。后端保持单服务的HTTP/业务边界，不拆微服务。无限通关历史与回执放独立集合；三个检查点数量及大小有界，嵌入player_profiles，GET取得同一版本的永久进度与三槽数据。

## 身份边界

匿名安装账号由GUID和操作系统CSPRNG产生的256位随机秘密组成。客户端先DPAPI加密落盘再登录。后端仅存随机秘密SHA256摘要并做常量时间比较；该方案用于高熵随机凭据，不是用户自选密码方案。服务器playerId不能单独授权。

Bearer为随机256位令牌，有效期两小时，客户端只保存在内存，数据库保存摘要与到期时间。身份文件独立于SaveGame，删除游戏档案后仍可恢复原账号云数据。DPAPI文件不能直接复制到另一个Windows用户。开发工具提供密码加密账号迁移包用于跨设备；商业登录/账号找回/平台绑定仍是后续接口扩展。同一后端且同一认证身份才会看到同一份云存档。

## 集合、字段与索引

| 集合 | 字段和用途 | 索引 |
|---|---|---|
| players | _id服务器GUID、installationId、secretHash、status(active/disabled)、createdAt | installationId唯一 |
| player_profiles | _id=playerId、schemaVersion=1、serverRevision BSON Int64、clearedDifficulties最多五种通关事实、服务端派生unlockedWeaponIds、lastSelectedPrimary、slots最多三项、createdAt/updatedAt | playerId唯一 |
| profile_bindings | _id绑定GUID、playerId、clientProfileId、epoch、installationId、lastAcceptedLocalRevision、createdAt | playerId+clientProfileId+epoch唯一 |
| campaign_runs | _id、playerId、runId、difficultyId、clientCompletedAt、receivedAt、completedLevels=10、rulesVersion、acceptance、claimHash | playerId+runId唯一；playerId+receivedAt降序+_id降序 |
| sync_requests | _id、playerId、requestId、payloadHash、responseJson、createdAt | playerId+requestId唯一 |
| auth_sessions | _id、playerId、installationId、tokenHash、expiresAt | tokenHash唯一；expiresAt TTL=0 |

slots元素是{slotIndex,serverRevision,snapshot}，索引0..2，槽版本为Int64十进制字符串。snapshot包含version、createdLocal、savedUtc、runId、phase、difficulty、completedLevel、coins、silverCoins、kills、purchases、goldPurchases、silverPurchases、upgradeProgress{goldLevels[3],silverLevels[3],permanentDamage,permanentHealth,permanentMagazine}、maxHealth、health、damageBonus、magazineBonus、healAmount、dashSpeed、weapons[{id,ammo,reserve}]、primaryId、activeSlot。范围见Contracts.cs与UDemoRunSave::Validate。日期仅展示，不用于最后写入获胜。

检查点V3加入永久来源与六项独立价格，purchases等于goldPurchases+silverPurchases，两个总数又分别等于账本数组求和；永久增量不得超过合计属性。V4加入unlockedAmmoIds/selectedAmmoId；V5加入bEndless/bestEndlessLevel/pistolChallenge。服务端接受V1..V5但不改写请求版本/迁移货币，UE按[关卡与经济](关卡与经济.md)及[战役配置与难度](战役配置与难度.md)迁移。新增零值/false字段或旧请求null字段省略，保证历史幂等哈希不改变。总HTTP协议仍是2，与检查点版本区分；先部署支持V5检查点的API再发布客户端，旧API会拒绝新格式。

通关事实白名单为easy/normal/hard/hard_pistol/hell（最多五种）；hard_pistol表示困难整轮仅手枪实际开火且完成十关，hell表示地狱十关完成。它们仍由runId幂等记账；无尽没有十关胜利，不创建campaign_runs记录。服务端接受hell前要求已有hard_pistol；写入Hell检查点要求hard_pistol，写入bEndless检查点还要求hell。一次离线批次可先接纳顺序提交的通关事实，再在同事务写入解锁后的检查点。其真实性仍受下文离线信任边界限制。

2026-09-22武器规则更新：normal解锁shotgun，hard/hard_pistol/hell均解锁shotgun和sniper，easy仍单独解锁rifle。`WeaponUnlockRules.Resolve`统一用于GET快照和同步事务的装备权限验证；GET从真实clearedDifficulties推导，旧困难投影无需再次通关就返回散弹权限，读操作不增修订或写库，同步时更新存储投影。不补造低难度通关记录、不发额外金币、不更改协议版本。客户端Normalize按同一规则处理旧事实及历史幂等回执。新增纯规则控制台测试`Backend/FPSDemo.RulesTests`覆盖9项，无网络/数据库依赖；本次API编译和纯规则测试通过，未执行HTTP/MongoDB端到端回归或部署。对应运行链、日志及UE真实关卡验证见[武器库与玩家存档](武器库与玩家存档.md)。

V5的pistolChallenge仅0/1/2，bEndless只允许Hell；无尽Reward/Intermission允许超过十关且bestEndlessLevel>=completedLevel，不允许无尽Victory。旧版本不能携带新模式/资格/纪录。最高纪录与本槽快照一起受CAS控制，不单独跨槽或跨设备取最大合并；客户端永久档案版本3与服务端player_profiles schemaVersion=1是不同层级。新增/改动文件为Contracts.cs（DTO、稳定ID白名单）、ProfileService.cs（校验/事务）、Program.cs（health能力列表）、Test-Backend.ps1（31项隔离HTTP回归）。维护协议时同步UE Validate/ExportCloud/ImportCloud；失败仍沿原有拒绝、冲突、发件箱保留路径处理。

启动创建缺失集合和固定索引，不清空旧数据。Mongo JSON Schema目前仅约束对象及字符串_id；完整字段白名单、范围和关联条件在DTO/业务服务校验。已有数据/索引冲突会阻止启动，需要显式迁移。TTL清理有延迟，认证仍检查过期时间。同步回执无自动TTL；若要归档，须同时设计最大重试期限和归档查询，不能让旧请求失去幂等证明。

## HTTP接口

| 方法与路径 | 请求 | 响应 |
|---|---|---|
| GET /health | 无 | 实际DB ping通过才返回ok、protocolVersion=2 |
| POST /v1/auth/session | installationId、secret | playerId、accessToken、expiresInSeconds |
| POST /v1/me/profile-bindings | Bearer；clientProfileId、epoch | bindingId、lastAcceptedLocalRevision |
| GET /v1/me/profile | Bearer | serverProfileId、serverRevision字符串、clearedDifficulties、unlockedWeaponIds、lastSelectedPrimary、slots |
| POST /v1/me/profile/sync | Bearer；协议2写操作 | requestId、bindingId、acceptedLocalRevision、acceptedRunIds、profile |

同步请求：protocolVersion=2、随机GUID requestId、bindingId、localRevision、baseServerRevision字符串、clears最多64条、hasPreferenceChange、lastSelectedPrimary、slots最多3条。槽写操作是slotIndex、baseSlotRevision、snapshot。拒绝未知字段，不允许直接上传解锁列表、playerId选择器或MongoDB操作符。

错误：401会话/凭据无效；400格式、字段或锁定武器；403绑定归属或离线进度关闭；409 revision_conflict、binding_stale、slot_conflict、request_id_reused、run_id_reused；503数据库不可用。错误正文只含稳定code，不返回连接串或原始异常。

## 核心流程

1. GI加载永久档、三槽、同步状态，登录匿名账号。
2. 有旧发件箱时先重放同一请求ID/正文。只有明确拒绝才可换ID；超时不能换ID。
3. 本地档案重建/版本回退时重新绑定epoch，下载投影。永久进度合并；新检查点只在大厅导入，不改正在战斗的World。
4. 本地变化先保存SaveGame。同步器按修订和槽快照指纹发现变化，生成不可变请求并保存DemoCloudState.sav，成功后才发HTTP。
5. 后端验证DTO，再在独立session内依次查幂等回执、验证绑定/水位、全局CAS、通关去重、派生解锁、槽CAS/装备解锁、偏好检查，写档案/绑定/历史/回执。
6. 事务使用Snapshot/Primary/Majority。回调可被驱动重试，内部只做同session数据库操作，无发奖消息等非幂等副作用，不并行共享session。
7. 客户端匹配账号/请求/绑定/修订，先持久化确认结果，再清理发件箱；上传期间产生的新记录和检查点保持待同步。

永久档V2：Clears为待确认事实，CloudClearedDifficulties为确认基线，RecentAcceptedRuns最多128项防近期重复回调（服务端runId永久唯一），PreferenceRevision/SyncedPreferenceRevision独立防分批通关确认吞掉未上传偏好。V1老档记录完整迁移为待上传事实。

## 检查点冲突与失败

永久通关按runId去重、难度并集合并。两局的金币/血量不能相加；同一槽本机与云端都变化时暂停同步，大厅显示“保留本机检查点 / 使用云端检查点”。选择前将本机三槽和远端投影保存到Saved/Cloud/Conflicts/<GUID>.json；归档失败不覆盖。

保留本机更新已知槽CAS基准，再上传本地变化；采用云端先验证/备份再写入版本变化的检查点。本次选择针对这一批变化槽，不是逐槽可视化差异编辑器。选择期间远端再变化仍会再次冲突。

网络/5xx退避2..60秒并保留本地数据；401重新认证；协议/磁盘不可恢复错误停止请求并显示状态。大厅与终端可重试。单人玩法不等待网络。当前已选战役不会因下载而在战斗途中改变。

游戏内退出按钮另设保存屏障：先写永久档与检查点，再用 `BeginExitSync/PollExitSync/CancelExitSync` 等待最新本地快照全部确认，最长30秒，成功提示1秒后退出。复用当前HTTP与不可变发件箱，旧回执后仍比较最新槽指纹，不因旧请求200提前退出；本地确认写盘失败同样不算成功。类型化结果与HUD文案分离，初始化失败的同步状态不能通过退出重试绕过版本/格式保护。失败提供重试、仅本地退出与取消；取消不删除队列，未确认请求保留原ID跨进程补传。Editor继续不创建网络子系统。完整界面流程、生命周期和验证入口见[暂停设置与检查点存档](暂停设置与检查点存档.md)的退出弹窗章节。

## 安全与限制

单人离线Demo只能验证身份、字段、版本、幂等和规则映射，不能证明客户端真的打完十关。接受的通关标记offline_accepted，不作为竞技防作弊认证。Profile__AllowOfflineProgress=false会拒绝离线通关；当前没有可信专用游戏服务器成绩提交接口。

公网部署还需HTTPS、注册/会话限流、运营认证及备份恢复方案；当前只监听本机。客户端远端BaseUrl必须HTTPS。当前支持Windows单人Standalone/单人PIE，不声称完整多人联机或其他平台凭据支持。分页历史查询、账号找回和自动冲突差异展示尚未实现。

## 影响文件与维护

Backend/FPSDemo.Api的Contracts、ProfileService、Program与csproj；Backend/Tools、环境模板；Source/FPSDemoOnline、uproject、FPSDemo.Build.cs、DefaultGame.ini；Player/DemoPlayerProfile、DemoCloudSync、DemoPlayerController；Save/DemoRunSave；UI/DemoHUD；GameMode测试入口与Tests/DemoCloudTest；武器库与运行文档。

无注释配置含义：uproject注册Runtime网络模块；csproj固定net10.0及MongoDB.Driver 3.12.0，不参加UBT。迁移包JSON字段见运行文档。变更目录稳定ID、关卡数、检查点字段/范围，必须同步双端验证、版本迁移和回归。

参考：[MongoDB C#事务](https://www.mongodb.com/docs/drivers/csharp/current/crud/transactions/)、[唯一索引](https://www.mongodb.com/docs/manual/core/index-unique/)、[Atlas连接](https://www.mongodb.com/docs/atlas/driver-connection/)。
