param(
    [Parameter(Mandatory = $false)] [string] $BuildDirectory = "build/release",
    [Parameter(Mandatory = $false)] [string] $QtBin = $env:QT_ROOT + "\bin",
    [Parameter(Mandatory = $false)] [string] $Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$executable = Join-Path $buildPath "LMDBArchiver.exe"
$cliExecutable = Join-Path $buildPath "LMDBArchiverCLI.exe"
$deployTool = Join-Path $QtBin "windeployqt.exe"
$stage = Join-Path $projectRoot "out\LMDBArchiver-$Version-win64"
$archive = "$stage.zip"

if (-not (Test-Path -LiteralPath $executable)) { throw "Build output not found: $executable" }
if (-not (Test-Path -LiteralPath $cliExecutable)) { throw "CLI build output not found: $cliExecutable" }
if (-not (Test-Path -LiteralPath $deployTool)) { throw "windeployqt not found: $deployTool" }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $executable -Destination $stage
Copy-Item -LiteralPath $cliExecutable -Destination $stage
Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") -Destination $stage
Copy-Item -LiteralPath (Join-Path $projectRoot "THIRD_PARTY_NOTICES.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\USER_GUIDE.ko.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\images") -Destination (Join-Path $stage "images") -Recurse
$lmdbLicense = Join-Path $buildPath "_deps\lmdb-src\libraries\liblmdb\LICENSE"
if (Test-Path -LiteralPath $lmdbLicense) { Copy-Item -LiteralPath $lmdbLicense -Destination (Join-Path $stage "LICENSE-LMDB.txt") }
$qtSdkRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $QtBin))
$qtLicense = Join-Path $qtSdkRoot "Licenses\LICENSE"
if (Test-Path -LiteralPath $qtLicense) { Copy-Item -LiteralPath $qtLicense -Destination (Join-Path $stage "LICENSE-QT.txt") }
& $deployTool --release --no-translations --no-compiler-runtime (Join-Path $stage "LMDBArchiver.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

$crtRoot = $env:VCToolsRedistDir
if ([string]::IsNullOrWhiteSpace($crtRoot)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $redistBase = Join-Path $vsRoot "VC\Redist\MSVC"
        $crtRoot = (Get-ChildItem -LiteralPath $redistBase -Directory |
            Where-Object Name -Match '^\d+(\.\d+)+$' |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1).FullName
    }
}
$crtDirectory = Join-Path $crtRoot "x64\Microsoft.VC143.CRT"
if (-not (Test-Path -LiteralPath $crtDirectory)) { throw "MSVC runtime directory not found: $crtDirectory" }
Copy-Item -Path (Join-Path $crtDirectory "*.dll") -Destination $stage
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Package created: $archive"
