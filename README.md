# Kira Canvas

Kira Canvas is a native Linux/KDE desktop application intended for AI-assisted image generation and editing.

## Status

Images can be imported into the persistent Library with the native Open dialog. The Library stores image paths through Qt settings and restores readable files from their original locations on the next launch. Search matches filenames, and category filters distinguish Generated, Imported, and Upscaled images. The Library offers both icon and list views; selected source images can be moved to the desktop Trash from its context menu or with Delete after confirmation. Library selection drives the optional Generate base image. An active preview tab enables the toolbar’s Upscale action, which offers 2× or 4× scaling and imports the result into the Library. Generation and upscaling run as one-shot local `sd-cli` processes.

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

Kira Canvas-managed models use `<GenericDataLocation>/kira-canvas/models/`, with upscalers under `models/upscale/`. On many Linux systems the data root is similar to `~/.local/share/kira-canvas/`, but it is resolved with Qt standard paths rather than hard-coded. The current interface does not provide an in-app model installer.

Curated runtime resources are separate from user-installed models. They live under `<GenericDataLocation>/kira-canvas/resources/`, with `generation/`, `upscale/`, and `metadata/` subdirectories. Each resource is downloaded to a neighboring `.part` file, verified before an atomic install, and recorded in internal verification metadata. On startup, Kira Canvas prepares any required curated resources before opening the main window; already verified resources open directly without the preparation screen.

## AI backend

Kira Canvas resolves the system-installed `sd-cli` through Qt standard paths. It does not bundle or manage stable-diffusion.cpp, and runs one-shot `sd-cli` processes rather than a persistent server.

## Technology

- C++20
- Qt 6 Widgets
- Extra CMake Modules
- CMake and Ninja

`QMainWindow` is used for this first empty slice. It follows the active Qt/Plasma theme without adding KDE Frameworks runtime dependencies before they are needed.

## Prerequisites

- CMake 3.21 or newer
- Ninja
- C++20-capable compiler
- Qt 6 Widgets development files
- Extra CMake Modules development files
- KDE Frameworks 6 KIO development files

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

## Run

```sh
./build/src/kira-canvas
```

## Test

```sh
ctest --test-dir build
```

The automated tests use a local HTTP fixture to cover curated-resource download, verification, startup preparation, and workflow selection behavior without downloading production resources.

## Future Direction

Future slices may expand the image canvas, model management, settings, and stable-diffusion.cpp integration through `QProcess`.
