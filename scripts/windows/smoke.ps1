[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$Preset = "windows-msvc-smoke",
  [string]$ConfigPath = "config/pipeline.windows.smoke.toml",
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [int]$RunDurationSeconds = 3,
  [switch]$SkipBuild,
  [switch]$SkipTests,
  [switch]$SkipSelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-MevRepoRoot -RepoRoot $RepoRoot
Assert-MevWindowsCheckout -RepoRoot $repoRoot

[void](Set-MevBuildEnvironment -RepoRoot $repoRoot -VcpkgRoot $VcpkgRoot -OnnxRuntimeRoot $OnnxRuntimeRoot)

if (-not $SkipBuild) {
  & (Join-Path $PSScriptRoot "build.ps1") -RepoRoot $repoRoot -Preset $Preset -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT
  if ($LASTEXITCODE -ne 0) {
    throw "Smoke build failed."
  }
}

if (-not $SkipTests) {
  & (Join-Path $PSScriptRoot "test.ps1") -RepoRoot $repoRoot -Preset $Preset -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT
  if ($LASTEXITCODE -ne 0) {
    throw "Smoke tests failed."
  }
}

if (-not $SkipSelfTest) {
  & (Join-Path $PSScriptRoot "self-test.ps1") -RepoRoot $repoRoot -Preset $Preset -ConfigPath $ConfigPath -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT
  if ($LASTEXITCODE -ne 0) {
    throw "Smoke self-test failed."
  }
}

& (Join-Path $PSScriptRoot "run.ps1") -RepoRoot $repoRoot -Preset $Preset -ConfigPath $ConfigPath -VcpkgRoot $env:VCPKG_ROOT -OnnxRuntimeRoot $env:ONNXRUNTIME_ROOT -AppArgs @(
  "--runtime.run_duration_seconds", "$RunDurationSeconds"
)

if ($LASTEXITCODE -ne 0) {
  throw "Smoke run failed."
}
