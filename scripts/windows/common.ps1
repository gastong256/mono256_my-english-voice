[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-MevRepoRoot {
  param(
    [string]$RepoRoot
  )

  if ($RepoRoot) {
    return (Resolve-Path -Path $RepoRoot).Path
  }

  return (Resolve-Path -Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Assert-MevCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Name
  )

  if (-not (Get-Command -Name $Name -ErrorAction SilentlyContinue)) {
    throw "Required command not found on PATH: $Name"
  }
}

function Assert-MevWindowsCheckout {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
  )

  if ($RepoRoot -notmatch '^[A-Za-z]:\\') {
    throw "RepoRoot must be a Windows path such as C:\dev\my-english-voice. Received: $RepoRoot"
  }
}

function Get-MevDefaultVcpkgRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
  )

  return (Join-Path $RepoRoot ".local\vcpkg")
}

function Get-MevDefaultOnnxRuntimeRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
  )

  return (Join-Path $RepoRoot ".local\onnxruntime\onnxruntime-win-x64-gpu-1.17.3")
}

function Get-MevLocalEnvFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
  )

  return (Join-Path $RepoRoot ".local\windows-dev-env.ps1")
}

function Import-MevLocalEnvIfPresent {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
  )

  $envFile = Get-MevLocalEnvFile -RepoRoot $RepoRoot
  if ((Test-Path -Path $envFile) -and (-not $env:VCPKG_ROOT -or -not $env:ONNXRUNTIME_ROOT)) {
    . $envFile
  }
}

function Set-MevBuildEnvironment {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,
    [string]$VcpkgRoot,
    [string]$OnnxRuntimeRoot
  )

  Import-MevLocalEnvIfPresent -RepoRoot $RepoRoot

  if (-not $VcpkgRoot) {
    if ($env:VCPKG_ROOT) {
      $VcpkgRoot = $env:VCPKG_ROOT
    } else {
      $VcpkgRoot = Get-MevDefaultVcpkgRoot -RepoRoot $RepoRoot
    }
  }

  if (-not $OnnxRuntimeRoot) {
    if ($env:ONNXRUNTIME_ROOT) {
      $OnnxRuntimeRoot = $env:ONNXRUNTIME_ROOT
    } else {
      $OnnxRuntimeRoot = Get-MevDefaultOnnxRuntimeRoot -RepoRoot $RepoRoot
    }
  }

  $env:VCPKG_ROOT = $VcpkgRoot
  $env:ONNXRUNTIME_ROOT = $OnnxRuntimeRoot

  return @{
    RepoRoot = $RepoRoot
    VcpkgRoot = $VcpkgRoot
    OnnxRuntimeRoot = $OnnxRuntimeRoot
  }
}

function Write-MevLocalEnvFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,
    [Parameter(Mandatory = $true)]
    [string]$VcpkgRoot,
    [Parameter(Mandatory = $true)]
    [string]$OnnxRuntimeRoot
  )

  $localDir = Join-Path $RepoRoot ".local"
  New-Item -ItemType Directory -Force -Path $localDir | Out-Null

  $envFile = Get-MevLocalEnvFile -RepoRoot $RepoRoot
  @(
    "`$env:VCPKG_ROOT = '$VcpkgRoot'"
    "`$env:ONNXRUNTIME_ROOT = '$OnnxRuntimeRoot'"
  ) | Set-Content -Path $envFile -Encoding ASCII

  return $envFile
}

function Get-MevExecutablePath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,
    [Parameter(Mandatory = $true)]
    [string]$Preset
  )

  return (Join-Path $RepoRoot "build\$Preset\apps\voice_mic\mev_voice_mic.exe")
}

function Get-MevBenchmarkExecutablePath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,
    [Parameter(Mandatory = $true)]
    [string]$Preset
  )

  return (Join-Path $RepoRoot "build\$Preset\benchmarks\benchmark_pipeline_latency.exe")
}

function Resolve-MevConfigPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath
  )

  if ($ConfigPath -match '^[A-Za-z]:\\') {
    return $ConfigPath
  }

  return (Join-Path $RepoRoot $ConfigPath)
}

function Invoke-MevStep {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Description,
    [Parameter(Mandatory = $true)]
    [string]$Command,
    [string[]]$Arguments = @(),
    [string]$WorkingDirectory
  )

  Write-Host "==> $Description"
  if ($WorkingDirectory) {
    Push-Location $WorkingDirectory
  }

  try {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
      throw "Command failed with exit code $LASTEXITCODE: $Command $($Arguments -join ' ')"
    }
  } finally {
    if ($WorkingDirectory) {
      Pop-Location
    }
  }
}

function Invoke-MevDownload {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Uri,
    [Parameter(Mandatory = $true)]
    [string]$OutFile
  )

  if (Test-Path -Path $OutFile) {
    Write-Host "==> Reusing existing download: $OutFile"
    return
  }

  $parent = Split-Path -Parent $OutFile
  if ($parent) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }

  Write-Host "==> Downloading $Uri"
  Invoke-WebRequest -Uri $Uri -OutFile $OutFile
}
