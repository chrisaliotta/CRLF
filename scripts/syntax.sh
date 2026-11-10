#!/usr/bin/env bash
# Reads the editor buffer on stdin and writes a JSON diagnostic array to stdout.
# The path argument names the language and the directory quoted includes are
# resolved from. It is not the file to read. See docs/diagnostics.md.
set -euo pipefail

usage() {
  echo "Usage: $(basename "$0") <path>" >&2
  echo "  Reads the buffer on stdin and writes a JSON diagnostic array to stdout." >&2
}

path="${1:-}"

case "$path" in
  -h|--help)
    usage
    exit 0
    ;;
  "")
    echo '[]'
    exit 0
    ;;
esac

filename=$(basename -- "$path")
extension="${filename##*.}"
extension="${extension,,}"

json_escape() {
  local s=$1
  local out=""
  local i char
  for ((i = 0; i < ${#s}; i++)); do
    char=${s:i:1}
    case "$char" in
      \\) out+='\\' ;;
      \") out+='\"' ;;
      $'\n'|$'\r') out+=' ' ;;
      *) out+="$char" ;;
    esac
  done
  printf '%s' "$out"
}

# first is shared by the emit_* readers. Each prints one JSON object.
first=1

emit_item() {
  local line=$1 column=$2 severity=$3 message=$4
  message=$(json_escape "$message")
  if [[ $first -eq 0 ]]; then
    printf ','
  fi
  first=0
  printf '{"line":%s,"column":%s,"severity":"%s","message":"%s"}' \
    "$line" "$column" "$severity" "$message"
}

emit_clang() {
  local line severity message
  first=1
  printf '['
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ ^(.+):([0-9]+):([0-9]+):\ (fatal\ error|error|warning):\ (.*)$ ]]; then
      severity="${BASH_REMATCH[4]}"
      if [[ "$severity" == "fatal error" ]]; then
        severity="error"
      fi
      message="${BASH_REMATCH[5]}"
      emit_item "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" "$severity" "$message"
    fi
  done
  printf ']\n'
}

# csc: file.cs(12,5): error CS1002: ; expected
emit_csharp() {
  local line severity message
  first=1
  printf '['
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ ^(.+)\(([0-9]+),([0-9]+)\):\ (error|warning)\ (.+)$ ]]; then
      severity="${BASH_REMATCH[4]}"
      message="${BASH_REMATCH[5]}"
      emit_item "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" "$severity" "$message"
    fi
  done
  printf ']\n'
}

# node --check:
#   path:line
#      ^
#   SyntaxError: message
emit_node() {
  local line line_no="" column="" message
  first=1
  printf '['
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" == */* && "$line" =~ :([0-9]+)$ ]]; then
      line_no="${BASH_REMATCH[1]}"
      column=""
      continue
    fi
    if [[ -n "$line_no" && "$line" =~ ^([[:space:]]*)\^+$ ]]; then
      column=$(( ${#BASH_REMATCH[1]} + 1 ))
      continue
    fi
    if [[ -n "$line_no" && "$line" =~ ^SyntaxError:\ (.*)$ ]]; then
      message="${BASH_REMATCH[1]}"
      emit_item "$line_no" "${column:-1}" "error" "$message"
      line_no=""
      column=""
    fi
  done
  printf ']\n'
}

# Newest SDK's csc.dll, and the matching reference assemblies when the pack
# is installed. Prints nothing when the SDK compiler is not there.
find_dotnet_csc() {
  local sdk_line sdk_ver sdk_dir dotnet_root pack_root pack_ver
  sdk_line=$(dotnet --list-sdks 2>/dev/null | tail -n 1)
  if [[ ! "$sdk_line" =~ ^([^[:space:]]+)\ \[([^]]+)\]$ ]]; then
    return 1
  fi
  sdk_ver="${BASH_REMATCH[1]}"
  sdk_dir="${BASH_REMATCH[2]}"
  csc_dll="$sdk_dir/$sdk_ver/Roslyn/bincore/csc.dll"
  if [[ ! -f "$csc_dll" ]]; then
    return 1
  fi
  dotnet_root=$(dirname -- "$sdk_dir")
  pack_root="$dotnet_root/packs/Microsoft.NETCore.App.Ref"
  if [[ -d "$pack_root" ]]; then
    pack_ver=$(find "$pack_root" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V | tail -n 1)
    if [[ -n "$pack_ver" ]]; then
      ref_dir=$(find "$pack_root/$pack_ver/ref" -mindepth 1 -maxdepth 1 -type d -print -quit)
    fi
  fi
  return 0
}

check_clang() {
  local compiler=("$@")
  if ! command -v "${compiler[0]}" >/dev/null 2>&1; then
    echo "${compiler[0]} not found" >&2
    exit 1
  fi
  local include_dir tmp output status
  include_dir=$(dirname -- "$path")
  tmp=$(mktemp --suffix=".$extension")
  cleanup+=("$tmp")
  cat > "$tmp"
  set +e
  output=$("${compiler[@]}" -fno-color-diagnostics -fno-caret-diagnostics -I "$include_dir" "$tmp" 2>&1)
  status=$?
  set -e
  if [[ $status -gt 1 ]]; then
    printf '%s\n' "$output" >&2
    exit 1
  fi
  printf '%s\n' "$output" | emit_clang
}

check_node() {
  if ! command -v node >/dev/null 2>&1; then
    echo '[]'
    exit 0
  fi
  local tmp output status parsed
  tmp=$(mktemp --suffix=".$extension")
  cleanup+=("$tmp")
  cat > "$tmp"
  set +e
  output=$(node --check "$tmp" 2>&1)
  status=$?
  set -e
  if [[ $status -gt 1 ]]; then
    printf '%s\n' "$output" >&2
    exit 1
  fi
  parsed=$(printf '%s\n' "$output" | emit_node)
  if [[ $status -ne 0 && "$parsed" == "[]" ]]; then
    printf '%s\n' "$output" >&2
    exit 1
  fi
  printf '%s\n' "$parsed"
}

check_csharp() {
  local tmp out output status parsed args ref
  local csc_dll="" ref_dir="" mode=""

  if command -v csc >/dev/null 2>&1; then
    mode=csc
  elif command -v dotnet >/dev/null 2>&1 && find_dotnet_csc; then
    mode=dotnet
  else
    echo '[]'
    exit 0
  fi

  tmp=$(mktemp --suffix=.cs)
  out=$(mktemp --suffix=.dll)
  cleanup+=("$tmp" "$out" "${out%.dll}.pdb")
  cat > "$tmp"

  if [[ "$mode" == csc ]]; then
    args=(csc -nologo -t:library "-out:$out" "$tmp")
  else
    args=(dotnet exec "$csc_dll" -nologo -t:library "-out:$out")
    if [[ -n "$ref_dir" && -d "$ref_dir" ]]; then
      for ref in "$ref_dir"/*.dll; do
        args+=("-r:$ref")
      done
    fi
    args+=("$tmp")
  fi

  set +e
  output=$("${args[@]}" 2>&1)
  status=$?
  set -e
  if [[ $status -gt 1 ]]; then
    printf '%s\n' "$output" >&2
    exit 1
  fi
  parsed=$(printf '%s\n' "$output" | emit_csharp)
  if [[ $status -ne 0 && "$parsed" == "[]" ]]; then
    printf '%s\n' "$output" >&2
    exit 1
  fi
  printf '%s\n' "$parsed"
}

cleanup=()
trap 'rm -f "${cleanup[@]}"' EXIT

case "$extension" in
  c)
    check_clang clang -fsyntax-only -x c
    ;;
  h|hh|hpp|hxx|cc|cpp|cxx|cppm|ccm|cxxm|c++|m|mm|inc)
    check_clang clang++ -fsyntax-only -x c++
    ;;
  js|mjs|cjs)
    check_node
    ;;
  cs)
    check_csharp
    ;;
  *)
    echo '[]'
    ;;
esac
exit 0
