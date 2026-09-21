# 匿名账号跨设备迁移：只由用户显式运行；密码保护的导出包与游戏存档分离。
# 凭据读取/加密只发生在本机，不输出到终端，不发送到其他服务。
param(
    [Parameter(Mandatory)][ValidateSet('Export','Import')][string]$Mode, # Export旧电脑导出；Import新电脑首次运行游戏前导入。
    [Parameter(Mandatory)][string]$File, # 用户明确指定的.cloudkey文件路径；导出不覆盖已有文件。
    [string]$IdentityFile = '' # 打包游戏可指定实际Saved/Cloud/Identity.bin；留空使用工程Saved目录。
)
$ErrorActionPreference = 'Stop' # 认证/文件失败即停止，绝不生成替代账号覆盖原凭据。
Write-Host '[CALL] Transfer-CloudIdentity'
Add-Type -AssemblyName System.Security.Cryptography.ProtectedData
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path # 当前项目根，实际打包后应改为游戏Saved目录。
$taskIdentity = if ($IdentityFile) { [IO.Path]::GetFullPath($IdentityFile) } else { Join-Path $taskRoot 'Saved/Cloud/Identity.bin' } # 必须指向目标游戏实际身份文件，不能把Editor的本地存档当账号。
if ($Mode -eq 'Export' -and (Test-Path -LiteralPath $File)) { throw 'Export destination already exists; choose a new file.' }
if ($Mode -eq 'Import' -and (Test-Path -LiteralPath $taskIdentity)) { throw 'This installation already has an identity. Export/back up it first; this tool will not replace it.' }
$taskSecure = Read-Host 'Enter transfer-file password (at least 12 characters)' -AsSecureString # 安全交互输入，不用命令行参数传密码。
$taskPassword = [System.Net.NetworkCredential]::new('', $taskSecure).Password # 仅在本进程内用于派生密钥。
if ($taskPassword.Length -lt 12) { throw 'Use at least 12 characters for the transfer password.' }
$taskPlain = $null; $taskKey = $null # finally清零的敏感缓冲，由本脚本拥有。
try {
    if ($Mode -eq 'Export') {
        $taskPlain = [Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($taskIdentity), $null, [Security.Cryptography.DataProtectionScope]::CurrentUser) # 原Windows账号解密。
        $taskSalt = [Security.Cryptography.RandomNumberGenerator]::GetBytes(16) # 每份导出独立盐，阻止预计算。
        $taskNonce = [Security.Cryptography.RandomNumberGenerator]::GetBytes(12) # AES-GCM随机nonce，每份文件只加密一次。
        $taskTag = [byte[]]::new(16) # GCM认证标签，密码错误/篡改时拒绝导入。
        $taskCipher = [byte[]]::new($taskPlain.Length) # 不透明加密凭据，不与SaveGame混放。
        $taskKey = [Security.Cryptography.Rfc2898DeriveBytes]::Pbkdf2($taskPassword, $taskSalt, 600000, [Security.Cryptography.HashAlgorithmName]::SHA256, 32) # 256位AES密钥，协议固定迭代数。
        $taskAes = [Security.Cryptography.AesGcm]::new($taskKey, 16) # 单份文件的加密对象，用后Dispose。
        try { $taskAes.Encrypt($taskNonce, $taskPlain, $taskCipher, $taskTag) } finally { $taskAes.Dispose() }
        $taskPackage = @{ version=1; salt=[Convert]::ToBase64String($taskSalt); nonce=[Convert]::ToBase64String($taskNonce); tag=[Convert]::ToBase64String($taskTag); ciphertext=[Convert]::ToBase64String($taskCipher) } # 非注释JSON字段含义见云存档文档。
        [IO.File]::WriteAllText([IO.Path]::GetFullPath($File), ($taskPackage | ConvertTo-Json -Compress))
        Write-Host 'Encrypted account transfer file created. Keep its password separate.'
    } else {
        $taskPackage = Get-Content -LiteralPath $File -Raw | ConvertFrom-Json # 来自用户指定文件，读取后严格检查版本/尺寸。
        if ($taskPackage.version -ne 1) { throw 'Unsupported transfer file version.' }
        $taskSalt = [Convert]::FromBase64String($taskPackage.salt) # PBKDF2盐，必须16字节。
        $taskNonce = [Convert]::FromBase64String($taskPackage.nonce) # GCM随机nonce，必须12字节。
        $taskTag = [Convert]::FromBase64String($taskPackage.tag) # 认证标签，必须16字节。
        $taskCipher = [Convert]::FromBase64String($taskPackage.ciphertext) # 有界匿名身份文本密文。
        if ($taskSalt.Length -ne 16 -or $taskNonce.Length -ne 12 -or $taskTag.Length -ne 16 -or $taskCipher.Length -gt 1024) { throw 'Invalid transfer file.' }
        $taskKey = [Security.Cryptography.Rfc2898DeriveBytes]::Pbkdf2($taskPassword, $taskSalt, 600000, [Security.Cryptography.HashAlgorithmName]::SHA256, 32) # 与导出协议一致的固定成本。
        $taskPlain = [byte[]]::new($taskCipher.Length) # 只在认证通过后用于DPAPI重新加密。
        $taskAes = [Security.Cryptography.AesGcm]::new($taskKey, 16) # 当前用户进程拥有，不共享跨请求。
        try { $taskAes.Decrypt($taskNonce, $taskCipher, $taskTag, $taskPlain) } finally { $taskAes.Dispose() }
        if ([Text.Encoding]::UTF8.GetString($taskPlain) -notmatch '^[0-9a-fA-F-]{36}\n[0-9a-fA-F]{64}$') { throw 'Invalid account identity payload.' }
        $taskProtected = [Security.Cryptography.ProtectedData]::Protect($taskPlain, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser) # 新Windows账号重新封装，游戏不会读取可移植明文。
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskIdentity)) | Out-Null
        $taskStream = [IO.File]::Open($taskIdentity, [IO.FileMode]::CreateNew) # 原子拒绝覆盖新近创建的身份。
        try { $taskStream.Write($taskProtected); $taskStream.Flush($true) } finally { $taskStream.Dispose() }
        Write-Host 'Account imported. Start the game against the same backend to restore cloud saves.'
    }
} catch { throw ('Account transfer failed: ' + $_.Exception.GetType().Name) } # 不打印可能带数据的异常正文。
finally {
    if ($taskPlain) { [Array]::Clear($taskPlain, 0, $taskPlain.Length) }
    if ($taskKey) { [Array]::Clear($taskKey, 0, $taskKey.Length) }
    $taskPassword = ''; $taskSecure.Dispose()
}
