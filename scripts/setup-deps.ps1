[CmdletBinding()]
param(
    [string]$WebView2Version = "1.0.4258.31",
    [string]$WilVersion = "1.0.260126.7",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$ThirdPartyDir = Join-Path $RootDir "third_party"
$DlDir = Join-Path $ThirdPartyDir "dl"

if (-not (Test-Path $DlDir)) {
    New-Item -ItemType Directory -Path $DlDir -Force | Out-Null
}

function Download-And-Extract-Nuget {
    param(
        [string]$PackageId,
        [string]$Version,
        [string]$DestinationPath,
        [string]$ExpectedFile
    )

    $expectedFullPath = Join-Path $DestinationPath $ExpectedFile
    if (-not $Force -and (Test-Path $expectedFullPath)) {
        Write-Host "[$PackageId] Already present at $DestinationPath. Use -Force to re-download." -ForegroundColor Green
        return
    }

    $zipFile = Join-Path $DlDir "$PackageId.$Version.zip"
    $url = "https://www.nuget.org/api/v2/package/$PackageId/$Version"

    Write-Host "[$PackageId] Downloading version $Version from $url..." -ForegroundColor Cyan
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13
    Invoke-WebRequest -Uri $url -OutFile $zipFile -UseBasicParsing

    Write-Host "[$PackageId] Extracting to $DestinationPath..." -ForegroundColor Cyan
    if (Test-Path $DestinationPath) {
        Remove-Item -Recurse -Force $DestinationPath | Out-Null
    }
    New-Item -ItemType Directory -Path $DestinationPath -Force | Out-Null
    Expand-Archive -LiteralPath $zipFile -DestinationPath $DestinationPath -Force

    if (-not (Test-Path $expectedFullPath)) {
        throw "Extraction check failed: could not find $expectedFullPath"
    }

    Write-Host "[$PackageId] Installed successfully." -ForegroundColor Green
}

Write-Host "=== Setting up third-party dependencies ===" -ForegroundColor Magenta

# 1. WebView2
Download-And-Extract-Nuget `
    -PackageId "Microsoft.Web.WebView2" `
    -Version $WebView2Version `
    -DestinationPath (Join-Path $ThirdPartyDir "webview2") `
    -ExpectedFile "build\native\x64\WebView2LoaderStatic.lib"

# 2. WIL
Download-And-Extract-Nuget `
    -PackageId "Microsoft.Windows.ImplementationLibrary" `
    -Version $WilVersion `
    -DestinationPath (Join-Path $ThirdPartyDir "wil") `
    -ExpectedFile "include\wil\com.h"

Write-Host "=== All dependencies ready! ===" -ForegroundColor Magenta
