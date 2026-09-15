$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$libraryRoot = Join-Path $projectRoot 'vendor/LilyGoLib'
$revision = 'c4a10b29b05c95983f2310eaa2a3c5802044dcba'
if (-not (Test-Path -LiteralPath $libraryRoot)) {
    git clone https://github.com/Xinyuan-LilyGO/LilyGoLib.git $libraryRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not download LilyGoLib' }
    git -C $libraryRoot checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'Could not select the pinned LilyGoLib revision' }
}
$actual = git -C $libraryRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual -ne $revision) {
    throw "Existing LilyGoLib differs from the required revision $revision; inspect it before changing it."
}
Write-Host "LilyGoLib ready at $revision"
