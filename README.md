# Kira Canvas

Kira Canvas is a native Qt desktop application for Linux and macOS, intended for AI-assisted image generation and editing. It follows the active desktop appearance: Qt’s Plasma and GNOME integration on Linux, and the Aqua/Cocoa style on macOS.

## Status

Images can be imported into the persistent Library with the native Open dialog. The Library stores image paths through Qt settings and restores readable files from their original locations on the next launch. Search matches filenames, and category filters distinguish Generated, Imported, and Upscaled images. The Library offers both icon and list views; selected source images can be moved to the desktop Trash from its context menu or with Delete on Linux and Command-Backspace on macOS after confirmation. Library selection drives the optional Generate base image. An active preview tab enables the toolbar’s Upscale action, which offers 2× or 4× scaling and imports the result into the Library. Generation and upscaling are submitted to a separately installed Kira Service; each desktop keeps its own Library and managed output folders.

Double-click a Library image to open it in a closable preview tab. The workspace remains empty until an image tab is opened.

Generation explicitly passes resolution, sampler, scheduler, steps, CFG scale, and seed to `sd-cli`. The developer console records the reconstructed command and streams `sd-cli` output while it runs.

The status bar derives generation progress from the streamed `sd-cli` lifecycle and sampling output. It shows a temporary progress bar while generating and hides it again after completion or failure.

## Images opened by the user

External images remain in their original filesystem locations. Kira Canvas does not copy, move, rename, or alter them when opening them. A user-confirmed Move to Trash action is the exception: it moves the selected source file to the desktop Trash and removes it from the Library.

## Kira Canvas managed outputs

Future Kira Canvas outputs will use `<Pictures>/Kira Canvas/`, where `<Pictures>` is resolved with Qt's `QStandardPaths::PicturesLocation` rather than assuming `~/Pictures`.

- `Generated/`
- `Upscaled/`

Generated, modified, and upscaled images are written to their respective directories. On startup, Kira Canvas scans these Kira Canvas-owned output folders and restores readable images to the Library.

## Storage

Kira Service-managed models use `<GenericDataLocation>/kira-canvas/models/`, with upscalers under `models/upscale/`, on the service host. On many Linux systems the data root is similar to `~/.local/share/kira-canvas/`; on macOS it is in the user’s Application Support location. Qt resolves these locations rather than the application hard-coding either platform’s path. The current interface does not provide an in-app model installer.

Curated runtime resources are separate from user-installed models. They live under `<GenericDataLocation>/kira-canvas/resources/`, with `generation/`, `upscale/`, and `metadata/` subdirectories on the service host. Each resource is downloaded to a neighboring `.part` file, verified before an atomic install, and recorded in internal verification metadata. On startup, Kira Service prepares any required curated resources; it reports `preparing` until they are ready.

## AI service

`kira-service` is a standalone cross-platform daemon for the machine with `sd-cli`, models, and GPU resources. It resolves the system-installed `sd-cli` through Qt standard paths, does not bundle stable-diffusion.cpp, and runs one-shot processes through a single-worker FIFO queue. Curated generation and upscale resources are prepared on the service host, not on every desktop.

The desktop connects with WebSockets and streams structured progress plus raw backend output. Input and output image bytes travel over the connection; no desktop filesystem path is exposed to the service. A disconnected client cancels its queued or active job, and service results are not retained.

Build and run the service alongside the desktop:

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/src/kira-service
```

It listens only on `127.0.0.1:9384` by default. For a trusted home LAN, create a file containing a long random token and bind explicitly:

```sh
./build/src/kira-service --listen 192.168.1.20 --port 9384 --token-file /path/to/kira-service.token
```

Configure the same `ws://host:port` address and token through **Settings → Service Preferences** on each desktop. The token is saved in that desktop user's Qt settings. LAN mode is unencrypted and must not be port-forwarded or used on an untrusted network. Use a private VPN or add TLS in a future deployment slice for remote access.

For unattended use, run the command under a user-level `systemd` service on Linux or a `launchd` LaunchAgent on macOS. Keep the token file readable only by the service user and ensure its environment includes `sd-cli` on `PATH`.

## Technology

- C++20
- Qt 6 Widgets
- CMake and Ninja

`QMainWindow` uses Qt’s active platform integration without application-set stylesheets or a forced style. The macOS build adds a native application bundle and unified title/toolbar presentation; it is a developer build only and is not signed, notarized, or deployed by this project.

## Prerequisites

- CMake 3.21 or newer
- Ninja
- C++20-capable compiler
- Qt 6 Widgets, Network, and WebSockets development files

On Linux, install the distribution’s Qt desktop integration for the desktop you use (such as Plasma Integration for KDE Plasma or the Qt GTK platform theme for GNOME) so Qt can follow its native palette, icons, fonts, and dialogs.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

## Run

On Linux:

```sh
./build/src/kira-canvas
```

On macOS, CMake creates a developer `.app` bundle:

```sh
./build/src/kira-canvas.app/Contents/MacOS/kira-canvas
```

## Test

```sh
ctest --test-dir build
```

The automated tests use a local HTTP fixture to cover curated-resource download, verification, startup preparation, and workflow selection behavior without downloading production resources.

## Future Direction

Future slices may expand the image canvas, model management, service security, and additional service workflows such as llama.cpp chat. The current service protocol implements image generation and upscaling only.
