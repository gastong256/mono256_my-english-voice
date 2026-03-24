#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_windows_common.sh"

preset="windows-msvc-full"
run_duration_seconds="12"
declare -a config_paths=("config/pipeline.windows.preview.toml" "config/pipeline.windows.toml")
declare -a benchmark_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --config)
      config_paths+=("$2")
      shift 2
      ;;
    --run-duration-seconds)
      run_duration_seconds="$2"
      shift 2
      ;;
    --build-first|--skip-synthetic|--skip-self-test)
      benchmark_args+=("$1")
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      echo "Usage: $0 [--preset windows-msvc-full] [--config config/pipeline.windows.preview.toml] [--run-duration-seconds 12] [--build-first] [--skip-synthetic] [--skip-self-test]" >&2
      exit 1
      ;;
  esac
done

repo_root="$(mev_repo_root)"
mev_require_windows_visible_repo "${repo_root}"
mev_require_command wslpath
ps_cmd="$(mev_powershell_command)"
repo_root_win="$(mev_repo_root_win)"
script_win="$(mev_script_root_win "${repo_root}/scripts/windows/benchmark-latency.ps1")"

args=(-NoProfile -ExecutionPolicy Bypass -File "${script_win}" -RepoRoot "${repo_root_win}" -Preset "${preset}" -RunDurationSeconds "${run_duration_seconds}")
for config_path in "${config_paths[@]}"; do
  if [[ "${config_path}" = /* ]]; then
    args+=(-ConfigPaths "$(wslpath -w "${config_path}")")
  else
    args+=(-ConfigPaths "${config_path}")
  fi
done
if [[ ${#benchmark_args[@]} -gt 0 ]]; then
  args+=("${benchmark_args[@]}")
fi

"${ps_cmd}" "${args[@]}"
