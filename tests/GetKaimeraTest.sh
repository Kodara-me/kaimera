#!/bin/sh
# Runs scripts/get-kaimera.sh against a fake release served from a temporary folder, with
# stub cmake and ctest that only record their arguments: nothing is built or installed for real.
# Usage: GetKaimeraTest.sh <path to get-kaimera.sh>
set -u

script=$1
root=$(mktemp -d) || exit 1
trap 'rm -rf "$root"' EXIT
failures=0

check() {
    if [ "$1" -eq 0 ]; then
        printf 'PASS: %s\n' "$2"
    else
        printf 'FAIL: %s\n' "$2"
        failures=$((failures + 1))
    fi
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

# Fake release v0.1.0 with a source archive, also served as the latest release.
mkdir -p "$root/source/kaimera-0.1.0" "$root/releases/download/v0.1.0" "$root/bin"
echo 'project(Kaimera VERSION 0.1.0)' >"$root/source/kaimera-0.1.0/CMakeLists.txt"
release="$root/releases/download/v0.1.0"
tar -czf "$release/kaimera-0.1.0.tar.gz" -C "$root/source" kaimera-0.1.0
printf '%s  kaimera-0.1.0.tar.gz\n' "$(sha256 "$release/kaimera-0.1.0.tar.gz")" >"$release/SHA256SUMS"
mkdir -p "$root/releases/latest"
cp -R "$release" "$root/releases/latest/download"

for tool in cmake ctest; do
    printf '#!/bin/sh\nprintf "%s %%s\\n" "$*" >>"$TOOL_LOG"\n[ "${FAIL_TOOL:-}" != %s ]\n' \
        "$tool" "$tool" >"$root/bin/$tool"
done
chmod +x "$root/bin/cmake" "$root/bin/ctest"

# get [arguments…]: runs the script with the stubs, the fake release and a fresh log.
get() {
    rm -f "$root/tools.log"
    PATH="$root/bin:$PATH" TOOL_LOG="$root/tools.log" KAIMERA_RELEASES_URL="file://$root/releases" \
        sh "$script" --prefix "$root/prefix" "$@" >"$root/output" 2>&1
}

get
check $? "the latest release installs"
grep -q -- "^cmake -S .*/kaimera-0.1.0 " "$root/tools.log" && grep -q -- "-DCMAKE_INSTALL_PREFIX=$root/prefix" "$root/tools.log"
check $? "CMake configures the extracted source for the prefix"
grep -q -- "^ctest --test-dir " "$root/tools.log" && grep -q -- "^cmake --install " "$root/tools.log" \
    && [ "$(grep -n -- "^ctest" "$root/tools.log" | cut -d: -f1)" -lt "$(grep -n -- "^cmake --install" "$root/tools.log" | cut -d: -f1)" ]
check $? "the tests run before the installation"

get --version v0.1.0 --qt /opt/qt
check $? "a given release installs"
grep -q -- "-DCMAKE_PREFIX_PATH=/opt/qt" "$root/tools.log"
check $? "--qt reaches CMake"

FAIL_TOOL=ctest get
[ $? -eq 1 ] && ! grep -q -- "^cmake --install" "$root/tools.log"
check $? "failed tests install nothing"

get --dry-run
check $? "a dry run succeeds"
[ ! -e "$root/tools.log" ] && grep -q "would run: cmake --install" "$root/output"
check $? "a dry run only prints the commands"

get --version v9.9.9
[ $? -eq 1 ] && [ ! -e "$root/tools.log" ]
check $? "a missing release fails before building"

get --unknown
[ $? -eq 1 ] && [ ! -e "$root/tools.log" ]
check $? "an unknown option fails before building"

mkdir -p "$root/prefix/bin"
printf '#!/bin/sh\necho "Kaimera 0.1.0"\n' >"$root/prefix/bin/kaimera"
chmod +x "$root/prefix/bin/kaimera"
get
[ $? -eq 0 ] && [ ! -e "$root/tools.log" ] && grep -q "already installed" "$root/output"
check $? "an installed version is not built again"
rm -rf "$root/prefix/bin"

echo 'tampered' >>"$release/kaimera-0.1.0.tar.gz"
get --version v0.1.0
[ $? -eq 1 ] && [ ! -e "$root/tools.log" ] && grep -q "does not match" "$root/output"
check $? "a wrong checksum fails before building"

[ "$failures" -eq 0 ]
