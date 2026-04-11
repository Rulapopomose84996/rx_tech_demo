#!/usr/bin/env bash
# Purpose: print a minimal system fingerprint for quick diagnostics.
# Environment: safe to run on the Linux server or WSL shell.
set -euo pipefail

uname -a
