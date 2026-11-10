#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $(basename "$0") <path> [buffer|-]"
  echo "  Formats the file and writes the result to stdout."
  echo "  <path> selects the language and where .clang-format is found."
  echo "  [buffer] is a file of editor text to format instead of <path>."
  echo "  '-' reads that text from stdin."
}

path="${1:-}"
buffer="${2:-}"

case "$path" in
  ""|-h|--help)
    usage
    exit 0
    ;;
esac

filename=$(basename "$path")
extension="${filename##*.}"
extension="${extension,,}"

# Language and style follow <path>. The bytes come from the buffer when one
# is given, so unsaved editor text is formatted instead of the saved file.
format_input() {
  local assume="$1"
  if [[ "$buffer" == "-" ]]; then
    clang-format --style=file --fallback-style=none --assume-filename="$assume"
  elif [[ -n "$buffer" ]]; then
    clang-format --style=file --fallback-style=none --assume-filename="$assume" < "$buffer"
  else
    return 1
  fi
}

case "$extension" in
  crlf)
    if [[ -n "$buffer" ]]; then
      format_input "${path}.json"
    else
      clang-format --style=file --fallback-style=none --assume-filename="${path}.json" < "$path"
    fi
    ;;
  h|hh|hpp|hxx|c|cc|cpp|cxx|cppm|ccm|cxxm|c++|m|mm|inc|js|mjs|cjs|ts|tsx|java|json|cs|proto|td|sv|svh|v|vh|textproto)
    if [[ -n "$buffer" ]]; then
      format_input "$path"
    else
      clang-format --style=file --fallback-style=none --assume-filename="$filename" "$path"
    fi
    ;;
  *)
    echo "No formatter for .$extension" >&2
    usage >&2
    exit 1
    ;;
esac
