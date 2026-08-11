param(
    [Parameter(Mandatory = $false)] [string] $BuildDirectory = "build/release",
    [Parameter(Mandatory = $false)] [string] $QtBin = $env:QT_ROOT + "\bin",
    [Parameter(Mandatory = $false)] [string] $Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$wixVersion = "5.0.2"
$wixDirectory = Join-Path $projectRoot "build\tools\wix"
$wix = Join-Path $wixDirectory "wix.exe"
$stage = Join-Path $projectRoot "out\LMDBArchiver-$Version-win64"
$output = Join-Path $projectRoot "out\LMDBArchiver-$Version-x64.msi"
$intermediate = Join-Path $projectRoot "build\msi"

& (Join-Path $PSScriptRoot "package.ps1") -BuildDirectory $BuildDirectory -QtBin $QtBin
if ($LASTEXITCODE -ne 0) { throw "Portable package staging failed with exit code $LASTEXITCODE" }

if (-not (Test-Path -LiteralPath $wix)) {
    New-Item -ItemType Directory -Path $wixDirectory -Force | Out-Null
    & dotnet tool install wix --tool-path $wixDirectory --version $wixVersion
    if ($LASTEXITCODE -ne 0) { throw "WiX Toolset installation failed with exit code $LASTEXITCODE" }
}

& $wix extension add "WixToolset.UI.wixext/$wixVersion"
if ($LASTEXITCODE -ne 0) { throw "WiX UI extension setup failed with exit code $LASTEXITCODE" }

New-Item -ItemType Directory -Path $intermediate -Force | Out-Null
& $wix build (Join-Path $projectRoot "installer\Product.wxs") `
    -arch x64 `
    -ext WixToolset.UI.wixext `
    -bindpath "Payload=$stage" `
    -define "Version=$Version" `
    -intermediatefolder $intermediate `
    -defaultcompressionlevel high `
    -out $output
if ($LASTEXITCODE -ne 0) { throw "MSI build failed with exit code $LASTEXITCODE" }

& $wix msi validate $output
if ($LASTEXITCODE -ne 0) { throw "MSI validation failed with exit code $LASTEXITCODE" }
Write-Host "MSI created: $output"
