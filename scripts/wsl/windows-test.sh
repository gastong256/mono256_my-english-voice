#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_windows_common.sh"

preset="windows-msvc-debug"
declare -a ctest_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --)
      shift
      ctest_args+=("$@")
      break
      ;;
    *)
      ctest_args+=("$1")
      shift
      ;;
  esac
done

repo_root="$(mev_repo_root)"
mev_require_windows_visible_repo "${repo_root}"
mev_require_command wslpath
ps_cmd="$(mev_powershell_command)"
repo_root_win="$(mev_repo_root_win)"
script_win="$(mev_script_root_win "${repo_root}/scripts/windows/test.ps1")"

args=(-NoProfile -ExecutionPolicy Bypass -File "${script_win}" -RepoRoot "${repo_root_win}" -Preset "${preset}")
if [[ ${#ctest_args[@]} -gt 0 ]]; then
  args+=(-CTestArgs)
  args+=("${ctest_args[@]}")
fi

"${ps_cmd}" "${args[@]}"
