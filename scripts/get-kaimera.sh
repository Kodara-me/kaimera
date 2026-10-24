#!/bin/sh
# Builds, tests and installs a Kaimera release, on Linux or macOS:
#
#   curl -fsSL https://github.com/jstmichel/kaimera/releases/latest/download/get-kaimera.sh | sh
#
# Usage: get-kaimera.sh [--version <tag>] [--prefix <dir>] [--qt <dir>] [--dry-run] [--help]
#
#   --version <tag>  install this release (for example v0.1.0) instead of the latest
#   --prefix <dir>   install under this folder (default: ~/.local, ~/Applications on macOS)
#   --qt <dir>       Qt installation to build against, when CMake does not find it
#   --dry-run        download and check, then print the build commands without running them
#
# Requires curl, tar, git, CMake 3.24+, a C++20 compiler, Qt 6.5+ with Widgets, Network,
# WebSockets, SVG, Multimedia (with its FFmpeg backend) and LinguistTools, and LibArchive.
# Installed Ardoise and Nukleus SDK
# packages are used when found, otherwise CMake clones their pinned tags. The release is
# installed only after its tests pass. Exit codes: 0 installed or already installed, 1 failure.

set -u

releases=${KAIMERA_RELEASES_URL:-https://github.com/jstmichel/kaimera/releases}
tag=""
if [ "$(uname -s)" = Darwin ]; then
    prefix="${HOME}/Applications"
else
    prefix="${HOME}/.local"
fi
qt=""
dry_run=0

usage() {
    sed -n '2,17s/^# \{0,1\}//p' "$0" 2>/dev/null \
        || echo "Usage: get-kaimera.sh [--version <tag>] [--prefix <dir>] [--qt <dir>] [--dry-run]"
}

say() {
    printf '%s\n' "$*"
}

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case $1 in
        --version)
            [ $# -ge 2 ] || fail "--version needs a tag, for example v0.1.0"
            tag=$2
            shift 2
            ;;
        --prefix)
            [ $# -ge 2 ] || fail "--prefix needs a folder"
            prefix=$2
            shift 2
            ;;
        --qt)
            [ $# -ge 2 ] || fail "--qt needs the Qt installation folder"
            qt=$2
            shift 2
            ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown option: $1 (see --help)" ;;
    esac
done

# run <command…>: runs it, or only prints it with --dry-run.
run() {
    if [ "$dry_run" -eq 1 ]; then
        say "  would run: $*"
        return 0
    fi
    "$@"
}

# sha256 <file>: prints the file's SHA-256 digest.
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

for tool in curl tar git cmake ctest mktemp; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is needed"
done
command -v sha256sum >/dev/null 2>&1 || command -v shasum >/dev/null 2>&1 \
    || fail "sha256sum or shasum is needed"

work=$(mktemp -d) || fail "cannot create a temporary folder"
trap 'rm -rf "$work"' EXIT
trap 'exit 1' INT TERM

if [ -n "$tag" ]; then
    base="$releases/download/$tag"
else
    base="$releases/latest/download"
fi

say "Looking for ${tag:-the latest release}…"
curl -fsSL -o "$work/SHA256SUMS" "$base/SHA256SUMS" || fail "cannot download $base/SHA256SUMS"
archive=$(sed -n 's/^[0-9a-f]\{64\}  \(kaimera-[0-9][0-9.]*\.tar\.gz\)$/\1/p' "$work/SHA256SUMS" | head -n 1)
[ -n "$archive" ] || fail "the release lists no source archive"
version=${archive#kaimera-}
version=${version%.tar.gz}

for executable in "$prefix/bin/kaimera" "$prefix/kaimera.app/Contents/MacOS/kaimera"; do
    [ -x "$executable" ] || continue
    if [ "$(QT_QPA_PLATFORM=offscreen "$executable" --version 2>/dev/null)" = "Kaimera $version" ]; then
        say "Kaimera $version is already installed in $prefix."
        exit 0
    fi
done

say "Downloading $archive…"
curl -fsSL -o "$work/$archive" "$base/$archive" || fail "cannot download $base/$archive"
expected=$(sed -n "s/^\([0-9a-f]\{64\}\)  $archive\$/\1/p" "$work/SHA256SUMS")
[ "$(sha256 "$work/$archive")" = "$expected" ] \
    || fail "$archive does not match the release's SHA256SUMS; nothing was installed"
say "Checksum verified."

tar -xzf "$work/$archive" -C "$work" || fail "cannot extract $archive"
source="$work/kaimera-$version"
[ -f "$source/CMakeLists.txt" ] || fail "$archive has no kaimera-$version/CMakeLists.txt"

build="$work/build"
set -- -S "$source" -B "$build" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix"
if command -v ninja >/dev/null 2>&1; then
    set -- "$@" -G Ninja
fi
if [ -n "$qt" ]; then
    set -- "$@" "-DCMAKE_PREFIX_PATH=$qt"
fi

say "Building Kaimera $version…"
run cmake "$@" || fail "CMake could not configure Kaimera; check that Qt 6.5+ and CMake 3.24+ are installed, or pass --qt"
run cmake --build "$build" --parallel || fail "Kaimera did not build"
say "Testing Kaimera $version…"
run ctest --test-dir "$build" --output-on-failure -E get-kaimera-test \
    || fail "the Kaimera tests failed; nothing was installed"
if [ "$dry_run" -eq 0 ] && ! mkdir -p "$prefix" 2>/dev/null; then
    fail "cannot write to $prefix; choose another folder with --prefix"
fi
run cmake --install "$build" || fail "Kaimera was not installed in $prefix"

if [ "$dry_run" -eq 1 ]; then
    say "Dry run: nothing was installed."
    exit 0
fi

say ""
say "Kaimera $version is installed in $prefix."
say "It connects to a Nukleus service: https://github.com/jstmichel/nukleus"
case "$(uname -s)" in
    Darwin) say "Open $prefix/kaimera.app." ;;
    *)
        case ":${PATH}:" in
            *":$prefix/bin:"*) say "Start it from the application launcher or with: kaimera" ;;
            *) say "Start it from the application launcher or with: $prefix/bin/kaimera" ;;
        esac
        ;;
esac
