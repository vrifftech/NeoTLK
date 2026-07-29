#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bundle-linux-wx.sh <staged-install-dir> <output-tar.gz>

Creates a portable NeoTLK application directory by bundling the exact wxWidgets
shared libraries used at link time. The installed executable is replaced by a
launcher that selects those private libraries before starting the real binary.
USAGE
}

if [[ $# -ne 2 ]]; then
    usage >&2
    exit 2
fi

stage_input=$1
archive_output=$2

stage_dir=$(cd "$stage_input" && pwd)
archive_output=$(python3 - "$archive_output" <<'PY'
import os, sys
print(os.path.abspath(sys.argv[1]))
PY
)

binary="$stage_dir/bin/NeoTLK"
if [[ ! -x "$binary" ]]; then
    echo "NeoTLK executable was not installed at: $binary" >&2
    exit 1
fi

package_parent=$(mktemp -d)
trap 'rm -rf "$package_parent"' EXIT
package_root="$package_parent/NeoTLK"
mkdir -p "$package_root"
cp -a "$stage_dir"/. "$package_root"/

real_binary="$package_root/bin/NeoTLK"
lib_dir="$package_root/lib/neotlk"
libexec_dir="$package_root/libexec/neotlk"
mkdir -p "$lib_dir" "$libexec_dir"

mapfile -t wx_libraries < <(
    ldd "$real_binary" \
        | awk '/libwx[^ ]*\.so/ && $2 == "=>" && $3 ~ /^\// { print $3 }' \
        | sort -u
)

if [[ ${#wx_libraries[@]} -eq 0 ]]; then
    echo "No dynamically linked wxWidgets libraries were found for $real_binary" >&2
    ldd "$real_binary" >&2 || true
    exit 1
fi

for library in "${wx_libraries[@]}"; do
    if [[ ! -f "$library" ]]; then
        echo "Linked wxWidgets library does not exist: $library" >&2
        exit 1
    fi
    cp -L "$library" "$lib_dir/$(basename "$library")"
done

mv "$real_binary" "$libexec_dir/NeoTLK"
cat > "$package_root/bin/NeoTLK" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail
app_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
private_lib_dir="$app_root/lib/neotlk"
export LD_LIBRARY_PATH="$private_lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$app_root/libexec/neotlk/NeoTLK" "$@"
LAUNCHER
chmod 0755 "$package_root/bin/NeoTLK" "$package_root/libexec/neotlk/NeoTLK"

# Verify resolution without starting the GUI. Every wxWidgets dependency must
# come from the private package directory rather than the target machine.
resolution=$(LD_LIBRARY_PATH="$lib_dir" ldd "$libexec_dir/NeoTLK")
printf '%s\n' "$resolution"
if grep -E 'libwx[^ ]*\.so.*not found' <<<"$resolution" >/dev/null; then
    echo "A bundled wxWidgets dependency could not be resolved." >&2
    exit 1
fi
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    resolved=$(awk '{print $3}' <<<"$line")
    case "$resolved" in
        "$lib_dir"/*) ;;
        *)
            echo "wxWidgets dependency resolved outside the package: $line" >&2
            exit 1
            ;;
    esac
done < <(grep -E 'libwx[^ ]*\.so' <<<"$resolution")

mkdir -p "$(dirname "$archive_output")"
tar -C "$package_parent" -czf "$archive_output" NeoTLK
printf 'Created %s\n' "$archive_output"
