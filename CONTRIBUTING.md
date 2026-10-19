# Contributing to Kaimera

The preview editing toolbar uses shared Ardoise IconButtons and bundled icons.
Build/install the current sibling UI framework when using its installed package.
Crop stays local: scene coordinates map to original image pixels, selection is
clipped to image bounds, and release appends/selects a new session entry.
Escape cancels and a zero-area click does not create an image. The original
remains in history. Fit and zoom controls share the existing preview APIs;
checked states follow menu actions and Ctrl+wheel as well as toolbar clicks.
Save as uses the main window's explicit save dialog and output preferences.
Upscale and portrait generation reuse the SDK job lifecycle.
Identity export is local: SDK IdentityPackage produces a version-2 .kiraid ZIP
with manifest.json (including generation/edit usage prompts) and one to four PNG
references. Save uses QSaveFile atomically. Attaching an identity unpacks it with
the SDK; IdentityPrompts.h holds the default instructions and comma-aware joining.
Kaimera tests cover zoomed/scrolled reverse cropping, cancellation, preserved
source pixels/history, button states, and upscale/save signal routing.
The toolbar uses a compact shared Panel with kiraFlat icon buttons. Preview
telemetry is painted using shared theme tokens, with native image/crop dimensions
and view-transform zoom; its text is also available as the accessible description.
Tests verify that edit submission retains the instructions while clearing the field.

Kaimera is a C++20 Qt 6 Widgets desktop application targeting Linux, macOS and Windows. Nukleus is developed, installed, and managed from its separate repository.

Generation uses service-owned preset routing; editing uses the service's fixed
editing qualities (Fast, Balanced, Quality; exactly one the default). Presets
have no quality layer. Kaimera has no configuration editor. SDK presets and
qualities transmit name (English) and nameFr; displayLabel chooses French for
French locales and English otherwise. Manager adds and removes generation
presets (Photorealism and Illustration initially) and configures prefixes and,
per preset and per editing quality, one model with a Steps/CFG snapshot;
selecting a model copies its recommendations and saving retains manually edited
values.

Workspace Steps/CFG initialize from the selected preset, falling back to the
mapped model/shared defaults. Preserve manual overrides on same-preset refresh
and New Image; reset them on preset/model changes, including two presets using
the same model. Model-based resolution behavior remains unchanged. Editing uses
the quality selected on the edit bubble's slider (starting on the default; Edit
with an identity always uses the default), submitting its qualityId with its
model, Steps/CFG, source dimensions and model sampler/scheduler.
Submit original prompts with the preset ID; service snapshots prefix composition.
Explicit-content classification does not affect availability. Identity uses an
independent preset selection with the same routing rules; service prefixes apply
to every view. Legacy LocalStyles settings remain untouched. Tests cover
locale selection, shared-model/different-preset defaults, recommendations,
overrides, unavailable mappings, upgrade behavior and edit uploads/history.

## Requirements

- CMake 3.24 or newer
- Ninja
- A C++20-capable compiler
- Qt 6.5 or newer Widgets, SVG, Network, WebSockets, and LinguistTools development files
- Qt Test development files when `BUILD_TESTING` is enabled (the default)
- Ardoise 0.1, from an installed package or cloned by CMake at tag `v0.1.0`
- LibArchive development files for the SDK's compressed `.kiraid` identity format

On Debian/Ubuntu, install `build-essential cmake ninja-build qt6-base-dev qt6-svg-dev qt6-websockets-dev qt6-tools-dev qt6-tools-dev-tools libarchive-dev`. On Fedora use `gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebsockets-devel qt6-qttools-devel libarchive-devel`; on Arch use `base-devel cmake ninja qt6-base qt6-svg qt6-websockets qt6-tools libarchive`. On macOS also install `brew install libarchive`.

## Build and test

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Run Linux builds with `./build/src/kaimera`. macOS creates a developer application bundle under `build/src/`.

### macOS startup troubleshooting

Homebrew's LibArchive is keg-only. If CMake cannot find its headers, add its
prefix alongside Qt when configuring, for example
`cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qtbase;/opt/homebrew/opt/libarchive"`.

Launch `./build/src/kaimera.app/Contents/MacOS/kaimera` from a terminal
to see startup errors. A `dyld: Symbol not found` error referencing
`QObjectPrivate` and different Qt versions indicates mismatched Qt modules;
the application can build successfully but abort before `main()`.

For Homebrew installations, check `brew list --versions qtbase qtwebsockets qtsvg qttools`
and upgrade outdated modules to match Qt Base (for example, `brew upgrade qtwebsockets`).
Then rerun CMake configuration, build, and tests using the commands above.

### VS Code debugging

Configure the `build` directory using the commands above, then select **Debug
Kaimera** in Run and Debug and press F5. The checked-in launch configuration uses
CodeLLDB and targets the Kaimera executable directly, including the application
bundle executable on macOS. Its build task builds the `kaimera` target and
its dependencies.

In a workspace containing Kaimera, Service, and UI Framework, this configuration
still launches Kaimera when a file from another project is active. It does not use
CMake Tools' active launch target, which may refer to a framework example or a
service executable.

Kaimera windows and dialogs use native OS title bars and controls. The Kira theme
styles their content; the vertical editing toolbar includes identity creation and
a bottom cog opening Service Preferences directly, without an application popup menu.
Verify decorations and window movement/resizing on each desktop; offscreen tests
verify window flags and application interactions.

## Nukleus

Kaimera uses the sibling `../nukleus` checkout by default. To use another checkout:

```sh
cmake -S . -B build -G Ninja -DNUKLEUS_SOURCE_DIR=/path/to/nukleus
```

Without a valid checkout, CMake uses an installed `NukleusSdk` 0.1 package when
found (add its prefix with `-DCMAKE_PREFIX_PATH=/path/to/nukleus-install`), otherwise
it clones the SDK of Nukleus tag `v0.1.0`; CI and releases build that way. Set
`-DNUKLEUS_SOURCE_DIR` to a path without `src/sdk` to skip the sibling checkout.
Every mode provides `Nukleus::SDK`; Kaimera never builds the daemon or manager.

## Continuous integration and releases

`.github/workflows/ci.yml` builds and tests every push to `main` and every pull
request in an Arch Linux container. Pushing a `v*` tag runs `release.yml`: it checks
that the tag matches the CMake project version, builds and tests the tag, and drafts a
GitHub release with the source archive, `scripts/get-kaimera.sh` and their
`SHA256SUMS`, using `docs/release-notes/<version>.md` as notes. The maintainer
publishes the draft. `tests/GetKaimeraTest.sh` checks the installer against a fake
release with stub `cmake` and `ctest`.

Kaimera owns QSettings, the Kira workspace, local image preview/export, session
history, and translated UI behavior. The SDK owns protocol v3,
transport, model discovery, and service connection state. Generation submission
is limited to one text-to-image job at a time; Kaimera does not manage model installation or invoke
`sd-cli`. Manage models in Nukleus Manager. The retired dictionary, scene taxonomy and
fragment library remain removed.

## Ardoise

Kaimera and its UI test executable link `Ardoise::Widgets`. CMake uses an installed
Ardoise 0.1 package when found (add its prefix to `CMAKE_PREFIX_PATH`), otherwise it
clones tag `v0.1.0` from https://github.com/jstmichel/ardoise. To build against a
local checkout:

```sh
cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_ARDOISE=/path/to/ardoise
```

If several packages require explicit prefixes, pass a quoted semicolon-separated
`CMAKE_PREFIX_PATH`.

Embedded framework builds disable their examples and installation by default and
do not register framework tests with Kaimera. Run the framework's own tests from
its standalone build. Kaimera explicitly calls `Ardoise::Theme::apply()` before
creating widgets. The framework supplies the shared dark theme, controls and
native window/dialog shells. See its `docs/design-system.md` for tokens and visual policy.

### Windows developer build

Use a Visual Studio C++ developer shell (or a matching MinGW kit), CMake, Ninja,
and a Qt desktop kit containing the modules above. Add the Qt
prefix to `CMAKE_PREFIX_PATH` and its `bin` directory to PATH for runtime DLLs.
The source build needs only the Service SDK, not the Service daemon/manager.

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64
cmake --build build
ctest --test-dir build --output-on-failure
```

Replace the kit path with the installed version/compiler. Launch
`build/src/kaimera.exe`. For Ninja Multi-Config, add `--config Debug` when
building, `-C Debug` to CTest, and use `build/src/Debug/kaimera.exe`.
Qt DLL/plugin deployment and release installers are outside this developer build.
Native Windows/macOS runtime verification remains pending on those systems.

## Localization and UI validation

English UI source strings use `tr()`; `translations/kaimera_fr.ts` provides
French, including plural forms. CMake compiles and embeds the catalog with LinguistTools.
The app loads French for French system locales and otherwise uses English. Native
Qt dialog translations are loaded from Qt's installed translation directory when
available. To update the catalog after changing display strings:

```sh
lupdate src -no-obsolete -ts translations/kaimera_fr.ts translations/kaimera_en.ts
```

Some distributions put `lupdate` in Qt's tool directory (for example,
`/usr/lib/qt6/bin/lupdate`). Complete new translations with Qt Linguist before
building. Complete French identity translations when adding labels.
Prompt text and model IDs are data, not translated display strings.

The tests use offscreen rendering and temporary QSettings. They cover the Kira
shell, prompt counters and undo, model availability,
preview zoom/history, atomic exports, saved output preferences, narrow layout,
and French/English catalogs. Service Preferences tests cover output Save/Cancel, reopening and preservation on
New Image. Export reads Output/Format and Output/Directory from QSettings.
The Available Models dialog has a WebSocket fixture
covering all seven categories, exact server recommendations, refresh, missing options,
disconnection, and read-only requests. Local socket access is required for this test.
Parameter tests cover model pixel-budget ceilings, 64-pixel alignment, portrait/landscape choices, server defaults, model changes,
catalogue refreshes, New Image and session-only Steps/CFG overrides.
Generation tests use a local WebSocket service fixture to verify submitted model
recommendations and side-panel overrides, single-job enforcement, queue/progress
display in a dismissible non-modal dialog without resizing the inspector,
image delivery into history/preview, and recovery from job failures,
invalid images and disconnection. Local socket access is required.
Failure tests also verify that backend diagnostics reach the UI and do not leak
into subsequent jobs.

The optional capture test saves empty and populated French workspaces at normal
and narrow widths. Populated screenshots use procedural
test images; no sample images enter production data.

```sh
QT_QPA_PLATFORM=offscreen KAIMERA_SCREENSHOT_DIR=/tmp/kaimera-review ./build/tests/canvas-test captureForVisualReview
```

Compare the result with the reference mockup, including 1448 × 1086 and 1100 × 700.
Repeat with `QT_SCALE_FACTOR=2` for high DPI. Check actual dragging, edge resizing,
maximize/restore, keyboard focus, system file dialogs and fullscreen on real Linux,
Windows and macOS desktops. Native window decoration behavior cannot be established
by offscreen tests. Native platform checks remain pending where unavailable.

The project page's screenshots (`docs/screenshots/en` and `fr`) come from another capture,
at 1440 × 900. Point `KAIMERA_SCREENSHOT_IMAGES` to a folder of generated images to show
them in the preview and history; without it, drawn placeholder images are used:

```sh
QT_QPA_PLATFORM=offscreen KAIMERA_SCREENSHOT_DIR=docs/screenshots KAIMERA_SCREENSHOT_IMAGES=/path/to/images ./build/tests/canvas-test captureProjectScreenshots
```

## Commit messages

Use Conventional Commits: `type(optional-scope): imperative summary`.

## App icon

`assets/icons/kaimera-master.png` is the selected violet K artwork.
Pre-exported PNG sizes and `kaimera.icns` are committed, so builds need
no image-conversion tools. Qt embeds the PNG sizes; CMake packages the ICNS
in macOS Resources and installs Linux hicolor icons and a desktop entry.
The desktop identifier is `me.kodara.kaimera` on both platforms.

For Linux installation, configure `CMAKE_INSTALL_PREFIX` before building and
run `cmake --install build`. The desktop entry contains the configured absolute
executable path, so do not override the prefix only at install time.
Test the icon in Linux launchers/taskbars under Wayland and X11, and in macOS
Finder/Dock on a Mac. Automated tests check that each embedded PNG loads.

Kaimera currently applies no explicit-content model filtering. Service Preferences
still persists `Service/AllowExplicitModels` only on Save (default false), pending
a later revision of the rule; Available Models shows the SDK classification.
Validate Save/Cancel persistence and that explicit models stay selectable, using
isolated settings and test servers.

Kaimera automatically uses the first upscaler in the service catalogue; an empty list disables upscale. Keep the legacy Service/DefaultUpscalerModel value unchanged and unused. Editing uses the service-owned editing quality models and no style or preset prefixes; the service prepends the quality's own prefixes. Keep the legacy Service/DefaultEditionModel value unchanged. Edit uploads the reference image and instructions with source dimensions, the selected editing quality's qualityId, model and Steps/CFG and mapped-model sampler/scheduler, retaining source history. Edits queue like workspace generations, behind a pending history tile that the unselected result replaces. Upscale produces exactly 2× dimensions and keeps the single-job lifecycle and floating progress pill.
