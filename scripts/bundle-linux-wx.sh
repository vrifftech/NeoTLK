#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <staged-install-root>" >&2
  exit 2
fi

STAGE_ROOT="$(realpath "$1")"
GUI_PATH="$STAGE_ROOT/bin/NeoTLK"
LIB_DIR="$STAGE_ROOT/lib/neotlk"
LIBEXEC_DIR="$STAGE_ROOT/libexec/neotlk"
REAL_GUI="$LIBEXEC_DIR/NeoTLK"

if [[ ! -x "$GUI_PATH" ]]; then
  echo "NeoTLK GUI executable was not found at: $GUI_PATH" >&2
  exit 2
fi

mapfile -t WX_DEPENDENCIES < <(
  ldd "$GUI_PATH" | awk '
    $1 ~ /^libwx/ && $2 == "=>" && $3 ~ /^\// { print $1 "\t" $3 }
  ' | sort -u
)

if [[ ${#WX_DEPENDENCIES[@]} -eq 0 ]]; then
  echo "No dynamically linked wxWidgets libraries were found in $GUI_PATH." >&2
  echo "This bundler is intended for Linux builds using shared system wxWidgets." >&2
  exit 3
fi

mkdir -p "$LIB_DIR" "$LIBEXEC_DIR"
WX_LIBRARY_NAMES=()
for dependency in "${WX_DEPENDENCIES[@]}"; do
  soname="${dependency%%$'\t'*}"
  source_path="${dependency#*$'\t'}"
  if [[ -z "$soname" || -z "$source_path" || ! -f "$source_path" ]]; then
    echo "Invalid wxWidgets dependency reported by ldd: $dependency" >&2
    exit 3
  fi
  cp -L -- "$source_path" "$LIB_DIR/$soname"
  chmod 0644 "$LIB_DIR/$soname"
  WX_LIBRARY_NAMES+=("$soname")
done

mv -- "$GUI_PATH" "$REAL_GUI"
cat > "$GUI_PATH" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail

BIN_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_ROOT="$(CDPATH= cd -- "$BIN_DIR/.." && pwd)"
BUNDLED_LIB_DIR="$APP_ROOT/lib/neotlk"

export LD_LIBRARY_PATH="$BUNDLED_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$APP_ROOT/libexec/neotlk/NeoTLK" "$@"
WRAPPER
chmod 0755 "$GUI_PATH" "$REAL_GUI"

{
  printf 'Bundled wxWidgets runtime libraries for NeoTLK\n'
  if command -v wx-config >/dev/null 2>&1; then
    printf 'Build wxWidgets version: %s\n' "$(wx-config --version)"
    printf 'Build wxWidgets configuration: %s\n' "$(wx-config --selected-config 2>/dev/null || true)"
  fi
  printf '\nLibraries:\n'
  printf '  %s\n' "${WX_LIBRARY_NAMES[@]}"
} > "$STAGE_ROOT/BUNDLED-WXWIDGETS.txt"

# Verify that every wxWidgets dependency resolves to the staged copy rather
# than a host installation with the same SONAME but a different ABI.
verification="$(LD_LIBRARY_PATH="$LIB_DIR" ldd "$REAL_GUI")"
printf '%s\n' "$verification"
while IFS= read -r line; do
  [[ -z "$line" ]] && continue
  resolved="$(awk '{print $3}' <<<"$line")"
  if [[ "$resolved" != "$LIB_DIR"/* ]]; then
    echo "A wxWidgets dependency did not resolve to the bundled runtime: $line" >&2
    exit 4
  fi
done < <(awk '$1 ~ /^libwx/ && $2 == "=>" { print }' <<<"$verification")

printf 'Portable NeoTLK Linux tree prepared under %s\n' "$STAGE_ROOT"
