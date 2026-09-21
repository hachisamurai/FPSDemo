# 读取本机环境文件并启动独立API；真实URI/密码绝不写到终端或进程参数。
param([switch]$NoBuild) # 已验证构建可跳过编译，默认先构建避免运行旧协议。
$ErrorActionPreference = 'Stop' # 配置/构建失败立即停止，不假报服务已启动。
Write-Output '[CALL] Start-Backend'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path # 固定项目根，不依赖调用者目录。
$taskEnv = Join-Path $taskRoot 'Backend/.env.local' # 本地秘密文件，受.gitignore排除。
if (-not (Test-Path -LiteralPath $taskEnv)) { throw 'Create Backend/.env.local from its example first.' }
foreach ($taskLine in Get-Content -LiteralPath $taskEnv) { # 不使用Invoke-Expression，密码字符不能变成脚本代码。
    if ([string]::IsNullOrWhiteSpace($taskLine) -or $taskLine.TrimStart().StartsWith('#')) { continue }
    $taskParts = $taskLine.Split('=', 2) # 仅按第一个等号拆分，保留URI查询参数。
    if ($taskParts.Count -ne 2 -or $taskParts[0] -notmatch '^(Mongo__ConnectionString|Mongo__Database|ASPNETCORE_URLS|Profile__AllowOfflineProgress)$') { throw 'Unsupported local configuration field.' }
    [Environment]::SetEnvironmentVariable($taskParts[0].Trim(), $taskParts[1].Trim(), 'Process')
}
if ([string]::IsNullOrWhiteSpace($env:Mongo__ConnectionString) -or $env:Mongo__ConnectionString.Contains('<')) { throw 'Fill the database password placeholder in Backend/.env.local.' }
$taskDotnet = Join-Path $taskRoot 'Saved/BackendRuntime/dotnet/dotnet.exe' # 项目便携SDK，正式部署可用安装的.NET 10运行时。
if (-not (Test-Path -LiteralPath $taskDotnet)) { & (Join-Path $PSScriptRoot 'Install-Runtime.ps1') }
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1' # 此启动脚本不发送SDK使用遥测。
if (-not $NoBuild) {
    & $taskDotnet build (Join-Path $taskRoot 'Backend/FPSDemo.Api/FPSDemo.Api.csproj') --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Backend build failed.' }
}
& $taskDotnet (Join-Path $taskRoot 'Backend/FPSDemo.Api/bin/Debug/net10.0/FPSDemo.Api.dll')
exit $LASTEXITCODE
