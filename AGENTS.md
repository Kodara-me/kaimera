# Kira Canvas Development Contract

## Project Identity

- **Project:** Kira Canvas
- Native Linux and macOS desktop application.
- C++ / Qt 6 Widgets application.
- AI-assisted image generation and editing application.

## Development Philosophy

- Implement work in very small vertical slices.
- Keep changes minimal.
- Avoid speculative abstractions.
- Do not create infrastructure before it is needed.
- Prefer straightforward, readable C++.
- Keep dependencies minimal.
- Preserve native desktop behavior and appearance.
- Prefer Qt and platform facilities over custom replacements.
- Do not introduce QML or another UI framework without explicit approval.
- Do not introduce Python into the application runtime.
- Do not implement future roadmap items unless explicitly requested.
- Build and test after each implementation slice.
- Fix warnings and errors introduced by our changes.
- Keep documentation synchronized when architecture or commands change.

## UI Principles

- The application should look and behave like a native desktop application: KDE Plasma and GNOME on Linux, and macOS on Apple platforms.
- Prefer system theme, fonts, icons, dialogs, shortcuts, and conventions.
- Avoid custom styling unless there is a clear requirement.
- Kira Canvas is AI-image-workflow-first, not a Photoshop clone.
- Primary workflows are Generate and Upscale.
- Workflow-specific image references replace Photoshop-style layers.
- The center image workspace is the visual focus of the main window.
- Keep the main toolbar intentionally minimal.
- Do not introduce drawing or editing tools without an explicit requirement.
- Follow the active desktop theme; avoid application-wide custom styling, forced Qt styles, and stylesheets.
- The Image Library is the primary browser for generated, imported, modified, and upscaled images.
- Keep the Library UI compact and sidebar-oriented, using native selection and thumbnail presentation.
- Do not couple the visual library directly to filesystem persistence; its data source must remain replaceable.
- Operation references are AI-workflow relationships, not composited visual layers. Generate owns an optional Base image for image-to-image generation; Upscale owns none.
- Add further operation reference roles only for a concrete workflow, and keep these controls compact and subordinate to the Image Library and image workspace.
- The center workspace is an image viewer, not a Photoshop-style editing canvas; its initial capabilities are native-size viewing, explicit zoom, actual size, and pan. It never automatically scales images; scrollbars appear when the native image exceeds the preview area. It remains empty until a Library asset is double-clicked, which opens a closable image-preview tab.
- Viewing transformations must not alter image data. QGraphicsView/QGraphicsScene is the lightweight initial foundation for the image workspace.
- Generate is a primary AI workflow. Keep its main panel compact around Prompt, Negative Prompt, Resolution, Sampler, Scheduler, Steps, CFG Scale, Seed, and an optional Base image with strength. The built-in generation model remains hidden from normal UI.
- Do not clutter the primary Generate workflow with advanced backend-specific controls. Its optional Base image stays within the Generate panel; do not expose IP-Adapter references until their model and vision-encoder resources are managed.
- Model choices will eventually come from model discovery, not hard-coded UI values.
- Upscale is a primary workflow exposed by the main toolbar when an image-preview tab is active. Its compact confirmation dialog offers only exact 2× or 4× scale plus base and final dimensions before processing.
- The active preview image is the upscale source. Backend-specific options must not clutter the dialog; curated 2× and 4× upscalers are selected internally and output is saved to the Library.
- Library assets, the current image, and operation references are separate concepts: selecting a Library asset makes it current, while operation references point to Library assets by identity and never act as layers.
- Assigning or clearing operation references must not alter the Library or current image. Coordinate cross-widget workspace interactions through explicit Qt signals and slots, not global state.
- The active preview image is the source for Upscale. Mock in-memory data must not dictate the eventual persistence implementation.
- The Image Library is a logical view, not a physical directory. External images remain at their original locations and must never be silently copied, moved, renamed, or modified.
- A user-confirmed Library Move to Trash action uses `QFile::moveToTrash()`; it uses the FreeDesktop Trash on Linux and the macOS Trash API on Mac, and removes its Library entry only after the operation succeeds.
- Kira Canvas-owned outputs will use `<Pictures>/Kira Canvas/`, resolved through `QStandardPaths::PicturesLocation`, with Generated and Upscaled subdirectories.
- Reference roles are application relationships, not filesystem folders. The Library persists asset paths through QSettings, operation references point to Library assets, and duplicate source paths must not create duplicate entries. External image files remain in place and unavailable or unreadable paths are skipped when restoring the Library.
- Prefer native system file dialogs. Do not automatically scan external folders. On startup, scan only the Kira Canvas-owned Generated and Upscaled output directories to restore readable output images to the Library.
- Kira Canvas follows Qt platform storage conventions: application data is rooted at `QStandardPaths::GenericDataLocation/kira-canvas` and managed models live in `models/` or `models/upscale/`. This resolves to XDG locations on Linux and Application Support on macOS.
- Managed models live in Kira Canvas storage. Any future installation workflow must copy rather than move or delete the user’s source file, must not silently overwrite an existing destination, and must not infer a model category from its filename.
- Model discovery scans only Kira Canvas managed storage and identifies files without loading or validating them. Image output storage remains separate under the user’s standard Pictures location.
- Keep the main toolbar minimal: Import Photo and active-preview Upscale are the only top-level actions. Do not add automatic downloads or filesystem watchers without a concrete requirement.
- The File and Image menus reuse the existing Import, Close Preview, and Upscale actions. On macOS, use the native menu bar, standard Command shortcuts, and unified title/toolbar presentation; on Linux, let Qt’s desktop integration choose appearance and icons.
- Kira Service uses only the system-installed `sd-cli`, resolved with `QStandardPaths::findExecutable("sd-cli")`; it never owns, installs, bundles, or links stable-diffusion.cpp. Desktop clients never invoke it directly.
- AI workflows use one-shot, workflow-specific `sd-cli` QProcess executions on Kira Service. The standalone service uses a versioned WebSocket protocol for typed image requests, progress, raw output, and binary image transfer; do not introduce sd-server or HTTP inference communication.
- Kira Service owns curated resources, temporary job storage, and a single-worker FIFO GPU queue. The desktop owns its Library and writes completed returned images into its own managed output folders. Service input/output paths are never exposed over the network.
- The service defaults to loopback. Explicit trusted-LAN binding requires a shared token; the current `ws://` LAN mode is unencrypted and must never be port-forwarded or exposed to untrusted networks.
- On systemd Linux, Kira Service may be installed as a root-managed, boot-enabled system service, but inference must run as the dedicated unprivileged `kira-service` account. Its system-owned data belongs under `/var/lib/kira-service`, configuration and token live under `/etc/kira-service`, and temporary jobs use `/run/kira-service`.
- Text-to-image status progress derives asynchronously from `sd-cli` lifecycle output and sampling counters. Keep parser knowledge in the runtime layer, stream raw output to the developer console unchanged, and render only structured, user-friendly progress in the status bar.
- Kira Canvas remembers main-window geometry and user-adjustable workspace splitter positions with QSettings. Prefer Qt saveGeometry/restoreGeometry and QSplitter saveState/restoreState over manual serialization.
- Saved user layout always takes precedence over defaults. Default sidebars should remain comfortably wide while the Image Viewer stays dominant, and settings must follow Qt/XDG platform conventions.

## Planned Architecture

These are future areas only. Do not create their directories, types, or infrastructure until explicitly requested.

- `canvas` - image canvas, zoom/pan, items, and selections.
- `generation` - text-to-image and image-to-image.
- `upscale` - image upscaling.
- `models` - model discovery and metadata.
- `settings` - application and generation settings.
- stable-diffusion.cpp will initially be accessed through `QProcess`.
- Direct stable-diffusion.cpp C/C++ integration may be considered later.

## Build Integration

The foundation uses `QMainWindow` from Qt Widgets deliberately. It receives the active Qt platform theme without KDE Frameworks runtime APIs. The macOS target is a host-architecture developer `.app` bundle only; signing, notarization, universal binaries, and deployment tooling are outside the current scope.

## Workflow

Use simple command-line workflows:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/src/kira-canvas
```

Use the actual generated executable path rather than blindly preserving this example.

## Scope Discipline

Before implementing a requested task:

1. Inspect the existing project.
2. Understand the smallest change required.
3. Implement only that slice.
4. Build it.
5. Run relevant tests.
6. Summarize briefly.

Do not refactor unrelated code.

Update this document when durable architectural decisions change.
