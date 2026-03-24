[CmdletBinding()]
param(
  [string]$RepoRoot,
  [string]$VcpkgRoot,
  [string]$OnnxRuntimeRoot,
  [switch]$SkipVcpkgInstall,
  [switch]$SkipPackageInstall,
  [switch]$SkipOnnxDownload,
  [switch]$SkipModelDownloads
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-MevRepoRoot -RepoRoot $RepoRoot
Assert-MevWindowsCheckout -RepoRoot $repoRoot

Assert-MevCommand -Name "git"
Assert-MevCommand -Name "cmake"
Assert-MevCommand -Name "ninja"

$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Get-Command -Name "cl.exe" -ErrorAction SilentlyContinue) -and -not (Test-Path -Path $vsWhere)) {
  throw "Visual Studio 2022 Build Tools were not detected. Install VS2022 with C++ CMake tools."
}

$envData = Set-MevBuildEnvironment -RepoRoot $repoRoot -VcpkgRoot $VcpkgRoot -OnnxRuntimeRoot $OnnxRuntimeRoot
$vcpkgRoot = $envData.VcpkgRoot
$onnxRuntimeRoot = $envData.OnnxRuntimeRoot

$downloadsDir = Join-Path $repoRoot ".local\downloads"
$onnxZip = Join-Path $downloadsDir "onnxruntime-win-x64-gpu-1.17.3.zip"
$onnxUri = "https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-win-x64-gpu-1.17.3.zip"

$modelsDir = Join-Path $repoRoot "models"
$modelDownloads = @(
  @{
    Uri = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin"
    OutFile = (Join-Path $modelsDir "ggml-small.bin")
  },
  @{
    Uri = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"
    OutFile = (Join-Path $modelsDir "ggml-tiny.bin")
  },
  @{
    Uri = "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx"
    OutFile = (Join-Path $modelsDir "en_US-lessac-medium.onnx")
  },
  @{
    Uri = "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json"
    OutFile = (Join-Path $modelsDir "en_US-lessac-medium.onnx.json")
  }
)

if (-not $SkipVcpkgInstall) {
  if (-not (Test-Path -Path (Join-Path $vcpkgRoot ".git"))) {
    Invoke-MevStep -Description "Cloning vcpkg" -Command "git" -Arguments @(
      "clone",
      "https://github.com/microsoft/vcpkg.git",
      $vcpkgRoot
    )
  }

  $bootstrap = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
  if (-not (Test-Path -Path $bootstrap)) {
    throw "vcpkg bootstrap script not found at $bootstrap"
  }

  Invoke-MevStep -Description "Bootstrapping vcpkg" -Command $bootstrap
}

$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
if (-not $SkipPackageInstall) {
  if (-not (Test-Path -Path $vcpkgExe)) {
    throw "vcpkg.exe not found at $vcpkgExe. Run setup without -SkipVcpkgInstall first."
  }

  Invoke-MevStep -Description "Installing native packages via vcpkg" -Command $vcpkgExe -Arguments @(
    "install",
    "portaudio:x64-windows",
    "espeak-ng:x64-windows",
    "libsamplerate:x64-windows"
  )
}

if (-not $SkipOnnxDownload) {
  Invoke-MevDownload -Uri $onnxUri -OutFile $onnxZip

  $onnxParent = Split-Path -Parent $onnxRuntimeRoot
  New-Item -ItemType Directory -Force -Path $onnxParent | Out-Null
  if (-not (Test-Path -Path $onnxRuntimeRoot)) {
    Invoke-MevStep -Description "Extracting ONNX Runtime" -Command "powershell.exe" -Arguments @(
      "-NoProfile",
      "-Command",
      "Expand-Archive -Path '$onnxZip' -DestinationPath '$onnxParent' -Force"
    )
  }
}

if (-not $SkipModelDownloads) {
  New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null
  foreach ($model in $modelDownloads) {
    Invoke-MevDownload -Uri $model.Uri -OutFile $model.OutFile
  }
}

if (-not (Test-Path -Path (Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
  throw "vcpkg toolchain file missing under $vcpkgRoot"
}

if (-not (Test-Path -Path (Join-Path $onnxRuntimeRoot "include"))) {
  throw "ONNX Runtime include directory missing under $onnxRuntimeRoot"
}

foreach ($requiredFile in @(
  (Join-Path $modelsDir "ggml-small.bin"),
  (Join-Path $modelsDir "ggml-tiny.bin"),
  (Join-Path $modelsDir "en_US-lessac-medium.onnx"),
  (Join-Path $modelsDir "en_US-lessac-medium.onnx.json")
)) {
  if (-not (Test-Path -Path $requiredFile)) {
    throw "Required model file missing: $requiredFile"
  }
}

$envFile = Write-MevLocalEnvFile -RepoRoot $repoRoot -VcpkgRoot $vcpkgRoot -OnnxRuntimeRoot $onnxRuntimeRoot

Write-Host ""
Write-Host "Windows development environment is ready."
Write-Host "RepoRoot:         $repoRoot"
Write-Host "VCPKG_ROOT:       $vcpkgRoot"
Write-Host "ONNXRUNTIME_ROOT: $onnxRuntimeRoot"
Write-Host "Env helper:       $envFile"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. . $envFile"
Write-Host "  2. .\scripts\windows\build.ps1 -Preset windows-msvc-full"
Write-Host "  3. .\scripts\windows\self-test.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml"
Write-Host "  4. .\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml"
