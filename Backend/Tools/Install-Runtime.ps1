# 下载便携.NET SDK并校验官方SHA512；不改系统SDK，不执行远程安装脚本。
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Write-Host '[Backend] Install-Runtime entry'
# repoRoot为此脚本对应工程根目录；所有工具安装在项目Saved内，可独立迁移。
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# runtimeRoot仅存工具/下载缓存，不属于游戏Content或源代码。
$runtimeRoot = Join-Path $repoRoot 'Saved/BackendRuntime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
# metadata来自微软官方发布清单，记录所选版本便于复现。
$metadata = Invoke-RestMethod 'https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json'
# sdkFile是win-x64 ZIP发布项，hash为SHA512。
$sdkFile = $metadata.releases[0].sdk.files | Where-Object { $_.rid -eq 'win-x64' -and $_.name -like '*.zip' } | Select-Object -First 1
# sdkRoot为本项目私有SDK，不设置全局PATH。
$sdkRoot = Join-Path $runtimeRoot 'dotnet'
if (-not (Test-Path (Join-Path $sdkRoot 'dotnet.exe'))) {
    # archive是待验证的下载文件；验证不通过即停止，不解压运行。
    $archive = Join-Path $runtimeRoot 'dotnet-sdk.zip'
    Invoke-WebRequest $sdkFile.url -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $sdkFile.hash) { throw 'SDK checksum mismatch' }
    Expand-Archive -LiteralPath $archive -DestinationPath $sdkRoot -Force
    $sdkFile | ConvertTo-Json | Set-Content (Join-Path $runtimeRoot 'dotnet-manifest.json') -Encoding utf8
}
& (Join-Path $sdkRoot 'dotnet.exe') --version
if ($LASTEXITCODE -ne 0) { throw 'SDK installation failed' }
