#!/usr/bin/env bash
set -euo pipefail

mev_repo_root() {
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "${script_dir}/../.." && pwd
}

mev_require_windows_visible_repo() {
  local repo_root="$1"
  if [[ ! "${repo_root}" =~ ^/mnt/[a-zA-Z]/ ]]; then
    cat >&2 <<'EOF'
This repo must live on a Windows-visible path for WSL2 -> Windows workflows.
Recommended location:
  C:\dev\my-english-voice
Opened from WSL2 as:
  /mnt/c/dev/my-english-voice
EOF
    exit 1
  fi
}

mev_require_command() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "Required command not found: ${name}" >&2
    exit 1
  fi
}

mev_powershell_command() {
  if command -v powershell.exe >/dev/null 2>&1; then
    command -v powershell.exe
    return
  fi

  if command -v pwsh.exe >/dev/null 2>&1; then
    command -v pwsh.exe
    return
  fi

  echo "powershell.exe (or pwsh.exe) is required in WSL2 for Windows wrappers." >&2
  exit 1
}

mev_repo_root_win() {
  local repo_root
  repo_root="$(mev_repo_root)"
  wslpath -w "${repo_root}"
}

mev_script_root_win() {
  local script_path="$1"
  wslpath -w "${script_path}"
}
