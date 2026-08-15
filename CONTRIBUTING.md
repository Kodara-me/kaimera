# Contributing to Kira Canvas

Kira Canvas is built on Linux and macOS with CMake and Qt 6 Widgets. This guide covers a local development build of both the desktop application and the optional Kira Service.

## Requirements

- CMake 3.21 or newer
- Ninja
- A C++20-capable compiler
- Qt 6.2 or newer development files for Widgets, Network, and WebSockets

`sd-cli` is not a build dependency. Install it separately on the machine that runs `kira-service` only when using image generation or upscaling.

## Linux setup

Install the build dependencies for your distribution.

### Debian or Ubuntu

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-websockets-dev
```

### Fedora

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtwebsockets-devel
```

### Arch Linux

```sh
sudo pacman -S base-devel cmake ninja qt6-base qt6-websockets
```

For native desktop appearance, also install your distribution’s Qt platform integration for the desktop you use, such as Plasma Integration on KDE Plasma or the Qt GTK platform theme on GNOME. These integration packages are recommended runtime dependencies, not build requirements.

Configure and build from the repository root:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Run the desktop application:

```sh
./build/src/kira-canvas
```

Run the optional local service:

```sh
./build/src/kira-service
```

## macOS setup

Install Apple’s compiler and SDK tools:

```sh
xcode-select --install
```

Install CMake, Ninja, and Qt with Homebrew:

```sh
brew install cmake ninja qt
```

Homebrew installs Qt in a separate prefix, so configure CMake with that prefix:

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
```

Run the developer app bundle:

```sh
./build/src/kira-canvas.app/Contents/MacOS/kira-canvas
```

Run the optional local service:

```sh
./build/src/kira-service
```

The macOS app bundle is for host-architecture development. Signing, notarization, universal binaries, and deployment packaging are outside the current project scope.

## Test

After building on either platform, run the test suite from the repository root:

```sh
ctest --test-dir build --output-on-failure
```

The automated suite uses local test fixtures and does not download production resources.

## Commit messages

Use Conventional Commits for every commit:

```text
type(optional-scope): imperative summary
```

Use one of `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, `chore`, or `perf`. Mark a breaking change with `!` after the type or scope, or add a `BREAKING CHANGE:` footer.
