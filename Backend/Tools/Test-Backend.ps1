# 真实HTTP/MongoDB集成回归；创建独立随机测试账号，不读取或覆盖玩家账号。
param([string]$BaseUrl = 'http://127.0.0.1:5087') # 仅本机API，数据库凭据留在服务进程。
$ErrorActionPreference = 'Stop' # 断言失败停止，不误报通过。
Write-Host '[CALL] Test-Backend'
$taskClient = [System.Net.Http.HttpClient]::new() # 单个HTTP连接池，测试结束释放。
$taskClient.Timeout = [TimeSpan]::FromSeconds(30)
$taskToken = '' # 测试Bearer仅内存，禁止输出响应正文。
function Invoke-TestApi([string]$Method, [string]$Route, $Body, [bool]$Authenticated = $true) {
    # 参数是固定测试方法/路由/DTO；Authenticated控制是否附加本测试会话。
    Write-Host "[CALL] Test HTTP $Method $Route"
    $taskRequest = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($Method), $BaseUrl + $Route) # 单次请求拥有正文。
    if ($Authenticated -and $script:taskToken) { $taskRequest.Headers.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $script:taskToken) }
    if ($null -ne $Body) { $taskRequest.Content = [System.Net.Http.StringContent]::new(($Body | ConvertTo-Json -Depth 20 -Compress), [Text.Encoding]::UTF8, 'application/json') }
    try {
        $taskReply = $taskClient.SendAsync($taskRequest).GetAwaiter().GetResult() # 测试串行执行，事务不并发共享session。
        try {
            $taskText = $taskReply.Content.ReadAsStringAsync().GetAwaiter().GetResult() # 敏感登录正文不打印。
            Write-Host "[HTTP] status=$([int]$taskReply.StatusCode)" # 仅记录状态；失败诊断也禁止转储认证响应。
            return @{ Status = [int]$taskReply.StatusCode; Data = ($taskText | ConvertFrom-Json); Text = $taskText }
        } finally { $taskReply.Dispose() }
    } finally { $taskRequest.Dispose() }
}
function Assert-Test([bool]$Condition, [string]$Name) {
    # Condition为语义断言，Name固定非敏感说明；不展开请求或凭据。
    Write-Host "[CALL] Assert $Name"
    if (-not $Condition) { throw "BACKEND_TEST_FAILED: $Name" }
    Write-Host "PASS $Name"
}
try {
    $taskUnauth = Invoke-TestApi GET /v1/me/profile $null $false # 未认证读取必须拒绝。
    Assert-Test ($taskUnauth.Status -eq 401) 'unauthenticated read rejected'
    $taskSecretBytes = [byte[]]::new(32) # CSPRNG 256位匿名测试凭据。
    [Security.Cryptography.RandomNumberGenerator]::Fill($taskSecretBytes)
    $taskLogin = @{ installationId = [guid]::NewGuid().ToString(); secret = [Convert]::ToHexString($taskSecretBytes) } # 不保存或输出秘密。
    $taskSession = Invoke-TestApi POST /v1/auth/session $taskLogin $false # 建立全新隔离账号。
    Assert-Test ($taskSession.Status -eq 200) 'anonymous registration'
    $script:taskToken = $taskSession.Data.accessToken
    $taskAgain = Invoke-TestApi POST /v1/auth/session $taskLogin $false # 登录丢包重试不能创建另一个账号。
    Assert-Test ($taskAgain.Status -eq 200 -and $taskAgain.Data.playerId -eq $taskSession.Data.playerId) 'stable authenticated identity'
    $taskWrong = Invoke-TestApi POST /v1/auth/session @{ installationId=$taskLogin.installationId; secret=('0' * 64) } $false # 错误秘密不能访问既有账号。
    Assert-Test ($taskWrong.Status -eq 401) 'wrong secret rejected'
    $taskBinding = Invoke-TestApi POST /v1/me/profile-bindings @{ clientProfileId=[guid]::NewGuid().ToString(); epoch=[guid]::NewGuid().ToString() } # 当前账号绑定。
    Assert-Test ($taskBinding.Status -eq 200) 'profile binding'
    $taskBefore = Invoke-TestApi GET /v1/me/profile $null # 首次档案应只有手枪。
    Assert-Test ($taskBefore.Data.serverRevision -eq '0' -and $taskBefore.Data.unlockedWeaponIds.Count -eq 1 -and $taskBefore.Data.unlockedWeaponIds[0] -eq 'pistol') 'initial pistol only'
    $taskNow = [DateTime]::UtcNow.ToString('o') # 固定一次生成，幂等重发不能重写完成时间。
    $taskCheckpoint = @{ version=1; createdLocal=$taskNow; savedUtc=$taskNow; runId=[guid]::NewGuid().ToString(); phase='Intermission'; difficulty='normal'; completedLevel=2; coins=25; kills=12; purchases=1; maxHealth=120; health=110; damageBonus=5; magazineBonus=0; healAmount=35; dashSpeed=1300; weapons=@(@{id='pistol';ammo=10;reserve=60},@{id='rifle';ammo=20;reserve=100}); primaryId='rifle'; activeSlot=1 } # 有效已解锁装备检查点，与通关记录属于不同战役。
    $taskSync = @{ protocolVersion=2; requestId=[guid]::NewGuid().ToString(); bindingId=$taskBinding.Data.bindingId; localRevision=1; baseServerRevision='0'; clears=@(@{runId=[guid]::NewGuid().ToString();difficultyId='easy';completedUtc=$taskNow}); hasPreferenceChange=$true; lastSelectedPrimary='rifle'; slots=@(@{slotIndex=0;baseSlotRevision='0';snapshot=$taskCheckpoint}) } # 同事务接受解锁/偏好/检查点。
    $taskFirst = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskFirst.Status -eq 200 -and $taskFirst.Data.profile.serverRevision -eq '1' -and $taskFirst.Data.profile.unlockedWeaponIds -contains 'rifle') 'atomic unlock and checkpoint save'
    $taskRepeat = Invoke-TestApi POST /v1/me/profile/sync $taskSync # 模拟响应丢失后同请求重放。
    Assert-Test ($taskRepeat.Status -eq 200 -and $taskRepeat.Text -eq $taskFirst.Text) 'idempotent exact receipt replay'
    $taskSync.localRevision = 2
    $taskReuse = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskReuse.Status -eq 409 -and $taskReuse.Data.code -eq 'request_id_reused') 'request ID cannot change payload'
    $taskSync.requestId = [guid]::NewGuid().ToString(); $taskSync.baseServerRevision = '1'; $taskSync.clears = @(); $taskSync.slots = @(); $taskSync.lastSelectedPrimary = 'sniper'
    $taskLocked = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskLocked.Status -eq 400 -and $taskLocked.Data.code -eq 'weapon_locked') 'locked preference rejected'
    $taskSync.lastSelectedPrimary = 'rifle'; $taskSync.unlockedWeaponIds = @('sniper')
    $taskForged = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskForged.Status -eq 400) 'unknown unlock grant field rejected'
    $taskSync.Remove('unlockedWeaponIds'); $taskSync.baseServerRevision = '0'
    $taskStale = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskStale.Status -eq 409 -and $taskStale.Data.code -eq 'revision_conflict') 'global revision conflict'
    $taskSync.baseServerRevision = '1'; $taskSync.slots = @(@{slotIndex=0;baseSlotRevision='0';snapshot=$taskCheckpoint})
    $taskSlotConflict = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskSlotConflict.Status -eq 409 -and $taskSlotConflict.Data.code -eq 'slot_conflict') 'overlapping checkpoint conflict'
    $taskAfter = Invoke-TestApi GET /v1/me/profile $null # 拒绝的事务不应推进任何版本或更改检查点。
    Assert-Test ($taskAfter.Data.serverRevision -eq '1' -and $taskAfter.Data.slots[0].snapshot.coins -eq 25 -and $taskAfter.Data.slots[0].snapshot.primaryId -eq 'rifle') 'rejected writes preserve stored snapshot'
    # 与打包客户端同步覆盖V2双币和V3来源账本，避免仅测旧V1导致运行中的旧后端漏检。
    $taskCheckpoint.version = 2; $taskCheckpoint.silverCoins = 45; $taskCheckpoint.goldPurchases = 1; $taskCheckpoint.silverPurchases = 0
    $taskSync.requestId = [guid]::NewGuid().ToString(); $taskSync.baseServerRevision = '1'; $taskSync.localRevision = 2
    $taskSync.slots = @(@{slotIndex=0;baseSlotRevision='1';snapshot=$taskCheckpoint})
    $taskV2 = Invoke-TestApi POST /v1/me/profile/sync $taskSync # 旧服务在此返回invalid_json，而不是网络/账号失败。
    Assert-Test ($taskV2.Status -eq 200 -and $taskV2.Data.profile.slots[0].snapshot.silverCoins -eq 45) 'V2 dual currency checkpoint accepted'
    $taskCheckpoint.version = 3
    $taskCheckpoint.upgradeProgress = @{goldLevels=@(1,0,0);silverLevels=@(0,0,0);permanentDamage=5;permanentHealth=0;permanentMagazine=0} # 非零永久伤害验证账本不会静默丢字段。
    $taskSync.requestId = [guid]::NewGuid().ToString(); $taskSync.baseServerRevision = '2'; $taskSync.localRevision = 3
    $taskSync.slots = @(@{slotIndex=0;baseSlotRevision='2';snapshot=$taskCheckpoint})
    $taskV3 = Invoke-TestApi POST /v1/me/profile/sync $taskSync # 模拟游玩后保存新版检查点。
    Assert-Test ($taskV3.Status -eq 200 -and $taskV3.Data.profile.slots[0].snapshot.upgradeProgress.permanentDamage -eq 5) 'V3 permanent upgrade ledger accepted'
    $script:taskToken = '' # 模拟退出进程丢弃短期会话，仅保留原安装凭据和不可变待确认请求。
    $taskRestart = Invoke-TestApi POST /v1/auth/session $taskLogin $false # 重启同一账号，不能另注册玩家绕过错误。
    Assert-Test ($taskRestart.Status -eq 200 -and $taskRestart.Data.playerId -eq $taskSession.Data.playerId) 'restart authenticates same player'
    $script:taskToken = $taskRestart.Data.accessToken
    $taskReplayed = Invoke-TestApi POST /v1/me/profile/sync $taskSync # 模拟退出前回执未落盘，重启原样补传。
    Assert-Test ($taskReplayed.Status -eq 200 -and $taskReplayed.Text -eq $taskV3.Text) 'restart replays V3 outbox exactly once'
    $taskRestored = Invoke-TestApi GET /v1/me/profile $null # 检查Mongo投影与重启后的下载值一致。
    Assert-Test ($taskRestored.Status -eq 200 -and $taskRestored.Data.serverRevision -eq '3' -and $taskRestored.Data.slots[0].snapshot.version -eq 3 -and $taskRestored.Data.slots[0].snapshot.coins -eq 25 -and $taskRestored.Data.slots[0].snapshot.silverCoins -eq 45 -and $taskRestored.Data.slots[0].snapshot.upgradeProgress.goldLevels[0] -eq 1) 'restart restores currencies and upgrade ledger'
    # V4金币和弹药解锁以同槽快照上传，测试只使用独立随机账号。
    $taskCheckpoint.version = 4; $taskCheckpoint.unlockedAmmoIds = @('normal','fire'); $taskCheckpoint.selectedAmmoId = 'fire'
    $taskSync.requestId = [guid]::NewGuid().ToString(); $taskSync.baseServerRevision = '3'; $taskSync.localRevision = 4
    $taskSync.slots = @(@{slotIndex=0;baseSlotRevision='3';snapshot=$taskCheckpoint})
    $taskV4 = Invoke-TestApi POST /v1/me/profile/sync $taskSync # 验证新字段不被丢弃。
    Assert-Test ($taskV4.Status -eq 200 -and $taskV4.Data.profile.slots[0].snapshot.selectedAmmoId -eq 'fire') 'V4 ammo unlock and selection accepted'
    $taskV4Replay = Invoke-TestApi POST /v1/me/profile/sync $taskSync # 原样重放不能重复提交。
    Assert-Test ($taskV4Replay.Status -eq 200 -and $taskV4Replay.Text -eq $taskV4.Text) 'V4 replay preserves exact receipt'
    $taskSync.requestId = [guid]::NewGuid().ToString(); $taskSync.baseServerRevision = '4'; $taskSync.slots[0].baseSlotRevision = '4'
    $taskCheckpoint.selectedAmmoId = 'piercing' # 客户端不能装配该槽尚未解锁的弹药。
    $taskInvalidAmmo = Invoke-TestApi POST /v1/me/profile/sync $taskSync
    Assert-Test ($taskInvalidAmmo.Status -eq 400 -and $taskInvalidAmmo.Data.code -eq 'invalid_checkpoint_ammo') 'unowned ammo selection rejected'
    $taskV4Download = Invoke-TestApi GET /v1/me/profile $null # 拒绝的写入不覆盖钱包与有效解锁。
    Assert-Test ($taskV4Download.Status -eq 200 -and $taskV4Download.Data.slots[0].snapshot.unlockedAmmoIds.Count -eq 2 -and $taskV4Download.Data.slots[0].snapshot.selectedAmmoId -eq 'fire' -and $taskV4Download.Data.slots[0].snapshot.coins -eq 25) 'V4 download retains wallet and ammo after rejected write'
    # 记录非敏感测试账号ID便于管理员审计；没有自动删除现有数据库或用户的步骤。
    $taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path # 固定工程输出路径。
    $taskReport = Join-Path $taskRoot 'Saved/Logs/BackendTestResult.json' # 只存测试结果与随机测试玩家ID。
    @{ success=$true; testedUtc=[DateTime]::UtcNow.ToString('o'); testPlayerId=$taskSession.Data.playerId; assertions=23 } | ConvertTo-Json | Set-Content -LiteralPath $taskReport -Encoding utf8
    Write-Host 'BACKEND_TEST_SUCCESS'
} finally { $taskClient.Dispose(); $script:taskToken = '' }
