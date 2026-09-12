#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

paths=(main/sampler docs/sampler-ui docs/development/sampler)
failed=0

check_forbidden() {
  local description="$1"
  local pattern="$2"
  shift 2
  local matches
  if matches="$(rg -n -i "$pattern" "${paths[@]}" \
    --glob '*.{cpp,hpp,inl,md,html,js,css,json}' "$@" 2>/dev/null)"; then
    echo "Terminology check failed: $description" >&2
    echo "$matches" >&2
    failed=1
  fi
}

# product-spec.md contains the one intentional statement that BGM is obsolete.
check_forbidden 'BGM is a deprecated user-facing term' '\bBGM\b' \
  --glob '!product-spec.md'
check_forbidden 'the four modes are SOUND / PLAY / REC / FX' \
  'REC/PLAY/LOOP/FX|LOOPモード|mode_loop'
check_forbidden 'Beat files must not be called loops in user-facing copy' \
  'Beat Loop|WAV Loop|Audio Repeat|Loop Repeat|>Loop<|Loop settings'

if rg -n -i 'Beat / Rec|Rec settings' docs/sampler-ui --glob '*.{html,js,css}' >&2; then
  echo 'Terminology check failed: File Editor must not expose Rec settings' >&2
  failed=1
fi

# 日本語マニュアルの本文はカタカナ表記にする。実機表示と
# HTMLコメントの英語は検査対象から除外する。
if matches="$(rg -n --pcre2 \
  '<!--.*?-->(*SKIP)(*F)|`[^`]*`(*SKIP)(*F)|(?<![A-Za-z])(?:Samples?|Beats?|Pads?)(?![A-Za-z])' \
  docs/docs/ja/sampler docs/mkdocs-sampler.yml --glob '*.md' --glob '*.yml' 2>/dev/null)"; then
  echo 'Terminology check failed: use サンプル / ビート / パッド in Japanese copy' >&2
  echo "$matches" >&2
  failed=1
fi

if (( failed )); then
  exit 1
fi

echo 'Sampler terminology check passed.'
