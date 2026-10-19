# Kaimera Development Contract

## Reviewable and reversible edits

- Use the `apply_patch` tool for all manual text-file additions, edits, and deletions. Do not use Python, Perl, sed, shell redirection, or other scripts to rewrite project files.
- Project generators and formatters may update files when required by the task. Identify these tool-generated changes and review their diffs.
- Inspect Git status before editing, preserve pre-existing user changes, and keep edits uncommitted unless the user requests a commit. Review the final diff, run `git diff --check`, and report changed files, including new untracked files.
- Keep changes small so they can be reviewed and undone independently. When asked to undo work, revert only the agent's changes while preserving subsequent user edits; never use a broad reset or discard unrelated changes.

## Project Identity

- **Project:** Kaimera
- Qt Widgets desktop application targeting Linux, macOS and Windows.
- C++ / Qt 6 Widgets application.
- AI-assisted image generation and editing application.

## Development Philosophy

- Implement work in very small vertical slices.
- Keep changes minimal.
- Avoid speculative abstractions.
- Do not create infrastructure before it is needed.
- Prefer straightforward, readable C++.
- Keep dependencies minimal.
- Preserve Qt desktop behavior while using the shared Kira visual design.
- Prefer Qt and platform facilities over custom replacements.
- Do not introduce QML or another UI framework without explicit approval.
- Do not introduce Python into the application runtime.
- Do not implement future roadmap items unless explicitly requested.
- Build and test after each implementation slice.
- Fix warnings and errors introduced by our changes.
- Keep documentation synchronized when architecture or commands change.
- When a change affects build requirements, setup commands, supported platforms, user-visible workflows, storage or service behavior, or architecture, update the applicable README, contributor guide, and/or this durable project contract in the same change.

## Documentation and Git Workflow

- Keep `README.md` concise and accurate as the project entry point (overview, screenshots, installation, first start), with `README.fr.md` as its French counterpart, changed together. `docs/user-guide.md` holds the detailed screen-by-screen behavior; update it with user-visible workflows. Keep `CONTRIBUTING.md` accurate as the detailed Linux and macOS contributor setup guide. Screenshots in `docs/screenshots/{en,fr}` come from the `captureProjectScreenshots` test (see CONTRIBUTING).
- Every commit must use Conventional Commits: `type(optional-scope): imperative summary`. Use standard types (`feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, `chore`, or `perf`); mark breaking changes with `!` or a `BREAKING CHANGE:` footer.
- Use Git directly for repository operations such as status, diff, branch, fetch, pull, add, commit, and push. Do not require GitHub CLI for these operations; use it only when its GitHub-specific capabilities, such as pull-request or issue management, are needed.
- When the requested publishing workflow includes a pull request, use `gh pr create --draft` after the branch is pushed if `gh` is installed and authenticated. Do not require `gh` for committing or pushing.
- Write every pull request with this reader-friendly template, using concrete content from the diff:

  ```md
  ## Summary

  - <concise description of the user-visible or developer-facing result>

  ## Changes

  - <key implementation or documentation change>

  ## Validation

  - <commands run and their outcome, or why validation was not run>

  ## Notes

  - <platform impact, follow-up, limitation, or `None`>
  ```

- If `gh` is unavailable or unauthenticated, do not block the commit or push. State that no pull request was created and provide the completed pull-request title and body in the final chat response inside a Markdown code block.

## UI Principles

- The preview's compact editing toolbar uses a shared Panel and flat icon buttons.
  The fit/actual-size choice persists under Preview/FitToView across image changes,
  clearing the preview and application restarts, defaulting to actual size (100%).
  Fit mode refits on resize; manual zoom/100% exits fit mode. Custom zoom is not saved.
  Show native image dimensions and current zoom in a translucent lower-left
  preview badge (left, clear of Kira Sparkle's bubble). During crop, also show the rectangle's source-pixel dimensions,
  independent of zoom. Capture edit instructions and clear the field on Edit.
  In the edit instruction field, Enter (including keypad Enter) clicks Edit only
  when enabled; Shift+Enter inserts a newline. Disabled submission retains the draft.

- Use Qt Widgets behavior with the shared Kira visual system. Apply `Ardoise::Theme` explicitly; it uses an explicit Fusion-based proxy style, tokens and bundled icons. Keep system fonts, shortcuts and Qt file dialogs. Do not add screen-specific QSS, duplicate shared widgets or hard-code visual colors in Kaimera.
- The visual contract is the approved dark graphite/violet mockup. Shared tokens, generic components and native window/dialog shells belong in Ardoise; domain models and workflows stay in Kaimera. No QML, Kirigami, AppKit/Cocoa UI, WinUI or platform-specific control rendering.
- The current app submits single-image text-to-image jobs through the Service SDK. Prompt editing, image Open/Save, preview zoom/pan, and session history work.
- The workspace fills the window without a splitter: the vertical editing toolbar on the left and the image preview/session thumbnails panel. The generation inspector (scrollable prompts/parameters with a persistent Generate row) lives in a "New image" GenerationCard centred over the preview viewport (max 920 px wide, spacing24 from the edges, natural height capped by the viewport and scrolling beyond), sharing FloatingSurface rendering with the edit bubble. It recentres and resizes with the viewport, rises and fades in when activated, and has no collapse or pill. From 660 px wide the inspector is landscape: the look, camera shot and references (steps 1–3) on the left, the prompts (steps 4–5) on the right, and Generate under the prompts with a Ctrl+Return hint; narrower, it stacks with a full-width Generate. Ctrl+Return (or Ctrl+Enter) within the inspector clicks Generate only when enabled. The card is a detailed generation tool, independent of the selection: the checkable New Image toolbar button toggles it over whatever the preview shows (empty or an image), keeping the draft; the New Image action opens it with a reset draft without clearing the selection. It starts closed, so the app opens on an empty preview. Its header close button and Escape close it. The history strip has no "New image" tile and Previous stops at the first image. While the card is open the edit bubble stays hidden; the two never coexist. Opening the card closes the identity panel. Workspace generation progress belongs in its history tile (see the generation rules), never in the pill. Other job progress belongs in a JobActivityIndicator pill floating at the top centre of the preview viewport (FloatingSurface rendering, progress ring, job title, stage and percentage), never in a dialog or the inspector. While running it is at least 420 px wide and grows to fit its title, status and a reserved "100%" up to 720 px (bounded by the viewport), never shrinking until the next job starts; failures use 560 px. Prompt translation shows "Translating the prompt…" with an indeterminate ring. The New image card moves down to clear it when there is room but never shrinks. Running jobs cannot be dismissed; success shows a check briefly after image delivery and fades out; failures expand the pill with the full message and stay until dismissed or the next job starts. Keep scene builder, Character Library, dictionary and Models screen removed unless explicitly requested. Upscale belongs in image context menus. Do not restore retired scene-taxonomy or dictionary assets.
- Use `Ardoise::MainWindow` with native OS title bar/buttons and no application popup menu. A Person icon opens identity creation in its own toolbar section under Improve sharpness, followed by Edit with an identity; a Settings cog at the bottom opens Service Preferences directly. No menu bar, window-level toolbar, status bar or mode navigation. A compact vertical icon editing toolbar belongs on the left of the image preview, with: fit/actual toggle, zoom out/in, 100%, crop, Upscale 2×, Downscale 2×, Improve sharpness, Edit with an identity and Save as. Improve sharpness (framework Sharpen icon: focus brackets around a sharp triangle) edits the selected image with the default editing quality's model, Steps/CFG and mapped sampler/scheduler as a direct image.edit without qualityId, so the service adds no quality prefixes; it sends fixed complete English positive and negative prompts for a deliberately light, subtle pass that adds no detail and discourages over-processing (users repeat it for more; SDK submitEdit forwards a non-empty negativePrompt), never runs an LLM, shares the single-job guard and pill ("Improving sharpness"), is enabled with a selected image and an available default quality model, and appends/selects the result like an edit. Edit with an identity (framework IdentityEdit icon) asks for a .kiraid every click and immediately submits an edit of the selected image with the identity's references and edit prompt, leaving the edit bubble's draft untouched; it always uses the service's default editing quality (never the slider's) and follows Edit's availability for that quality apart from the instruction text. Downscale is local: it halves the selected image with Lanczos-3 resampling and appends/selects the result in history. Use shared Ardoise IconButtons/icons; disable image actions without a selection and Upscale when unavailable/busy. Crop maps a dragged rectangle to source pixels, appends/selects a new session image and preserves its source; Escape cancels and an empty rectangle does nothing. Save as reuses the main window save workflow. Keep New Image, Open, Save, Edit/View, Service Preferences and Quit standard shortcuts. Remove Clear, Available Models and About from the main window.
- The generation panel is a guided path without expanders: numbered steps 1 "Choose the look" (Preset and a ResolutionPicker), 2 "Frame the shot" (ShotPicker, marked optional), 3 "Reference images", 4 "Describe your image" (positive prompt) and 5 "What to avoid" (negative prompt, marked optional), with example placeholders. The ShotPicker is a strip of camera shot tiles (Free, extreme close-up, close-up, portrait, medium, cowboy, full body, wide), each a small frame cropping a drawn figure (Free is an empty dashed frame), with a gliding highlight, keyboard Left/Right, short labels and the selected name and description underneath. It defaults to Free, New Image resets it. Each shot except Free has fixed, non-configurable English positive and negative framing clauses (ShotPrompts.h), as Danbooru framing tags for tags-format models (close-up, portrait, upper body, cowboy shot, full body, wide shot; Danbooru has no extreme close-up tag); after translation Kaimera places them comma-separated before the submitted prompts (so after the service's style prefixes and before the user's text, the identity instruction still last). The translator never sees them, and the editors and restored drafts never show them. The ResolutionPicker is a view of the hidden resolution combo: a horizontal strip of to-scale ratio tiles (square plus the current orientation, sorted by elongation; labels show the target ratio, tooltips the full text including ≈), a landscape/portrait switch that morphs the tiles and selects the rotated ratio, a gliding selection highlight, keyboard Left/Right and Up/Down, and the selected name and pixel size. There is no quality control: the preset alone selects the model. The panel hides Steps, CFG and Seed: Steps/CFG follow the selected preset and the seed stays -1 (random). The identity panel reuses this guided parameters widget without the ResolutionPicker, taking its view count from the selected preset, and follows the same routing rules. Numbered step headers are shared through StepHeader. Do not restore the Advanced generation or Output workspace sections. Sampler/scheduler use the selected model’s recommendedOptions or shared service defaults. Clip Skip has no configuration control and Kaimera does not apply legacy recommendations for it. Both prompt editors show at least three text lines plus the counter; height accounts for font metrics. In landscape only, the positive editor takes the prompts column's spare height, so the negative editor's bottom aligns with the reference buttons'. Step headers share the small control height so headers with and without actions (step 4) align, as do the first fields of both columns. Step 3 "Reference images" (marked optional, under step 2 in the look column) has the edit bubble's square Paperclip button, which adds up to four reference images through the Qt image file dialog, with the shared ReferenceImageStrip (numbered, removable thumbnails, also used by the edit bubble) beside it. A Person "Attach an identity" button beside the paperclip loads a .kiraid through the Qt file dialog and adds its references to the strip; in the card, its generationPrompt becomes hidden draft state appended comma-separated to the final positive prompt (after the service's preset prefix) and never shown. The translator never sees it: Kaimera translates the editor prompts first (see Prompt translation), then submits them plus the instruction. It is never shown in the editors or restored drafts; New Image or removing every reference detaches it. Packages without prompts use Kaimera's default instructions. Only models that can edit (SDK canEdit, missing means true) use reference images, and the service rejects them for other models: when the selected preset's model cannot (GenerationParametersWidget::presetUsesReferences), the paperclip and identity buttons are disabled with a tooltip saying so, attached thumbnails stay but show as ignored (ReferenceImageStrip::setIgnored: dimmed, warning border and badge, the reason in their tooltip), and Generate leaves them and the identity's instruction out. They are in-memory draft state: Generate uploads them in order as SDK referenceImages (sd-cli `-r`, never with a base image), accepted submission keeps them, New Image clears them, and they never enable Generate by themselves. Prompt `/2000` counters are advisory and never truncate.
- Workspace generations queue on the service: enable Generate when the SDK can submit for the selected opaque model ID and no single job (translation, improvement, Sparkle plan, sharpen, upscale, portrait) is active; pending generations and edits never disable it, lock the preset or block other jobs. Presets have no prompt flows; LLM help comes only from the translator and enhancer roles. Submit the current prompts and side-panel values. Each accepted Generate appends a pending, unselectable history tile (progress ring with percentage, or spinning when unknown, inside an accent sweep around the tile border like the identity portrait slots; no caption, the translated stage/queue state only as tooltip; its prompt translation holds the single-job guard and shows "Translating the prompt…" there). The delivered image replaces the tile in place without selecting it or closing the New image card. Failures, disconnections and unreadable results turn the tile into a failed tile (danger ring, the full message with diagnostics as tooltip, context menu Copy error and Delete, remove X) and retain existing images. Navigation (keyboard, Previous/Next) skips pending and failed tiles. Edits (edit bubble, Edit with an identity, Kira Sparkle) queue the same way through CanvasServiceController::queueEdit: a pending "Edited image" tile (only the instruction's translation holds the single-job guard, shown in the tile), the result scaled to the source's size replacing it in place without being selected, failures as failed tiles. Sharpen, upscales and identity portraits keep one active job through image-byte delivery and select their results as before. Do not automatically export images or use the current preview as an image-to-image input.
- Basic parameters initialize Steps and CFG from the selected preset's saved snapshot (mapped-model recommendations/shared defaults as fallback). Session overrides survive same-preset catalogue refreshes and New Image; discard Steps/CFG overrides on preset or model changes, even when two presets share a model, and on restart. Unedited fields follow refreshed preset settings. Saved preset values do not follow later model recommendation changes. Resolution choices use recommended width × height as a maximum pixel budget and show translated names, target ratios and sizes in portrait/landscape. Choose the largest area within budget among adjacent multiples of 64 around the ideal ratio dimensions, breaking ties by ratio error; bound dimensions to 64–4096 and mark approximate ratios with ≈. Do not write these overrides to QSettings or configure the service model.
- Output format/folder belong in Service Preferences and are saved with its Save button under Output/Format and Output/Directory. Cancel/close discards output edits; New Image preserves preferences. These local export defaults initialize the explicit Save dialog. It never triggers automatic exports or directory scanning.
- Nukleus owns models, storage, downloads, execution and preset/editing model routing. Manage installation and routing through Nukleus Manager. Kaimera uses opaque IDs and never invokes `sd-cli`. Service Preferences contains connection, local output and default LLM settings in a fixed, non-resizable window; these do not configure service models.
- Available Models is a read-only `Ardoise::Dialog`: show all supported SDK categories and server-saved `recommendedOptions`, preserve selection by opaque model ID when refreshing, and show disconnected/empty states. Never synthesize defaults, configure/install/delete models, or apply recommendations to generation controls from this dialog.
- Service Preferences saves a local `Service/AllowExplicitModels` checkbox, default false, only on Save; Cancel discards edits. Kaimera currently applies no explicit-content filtering anywhere (generation, editing, upscaler, LLM); keep the checkbox and SDK `explicitContent` classification until the rule is revisited. Available Models displays Yes/No classification. The service owns classification; Kaimera never changes it.
- Service Preferences accepts IPv4 or `localhost` and constructs the protocol-v3 `ws://` endpoint on fixed port `9384`. LAN connections remain token-protected and must be trusted.
- The main window has a minimum and default size of 1300 × 700 logical pixels of window content, excluding the native title bar. Saved user geometry takes precedence within the minimum size.
- Save main-window geometry through Qt saveGeometry/restoreGeometry and QSettings. Saved user geometry takes precedence over defaults. `MainWindow/GenerationWorkspaceSplitter` and other splitter keys are retired: leave them untouched and never restore them.
- Settings use the organization and application name Kaimera. At startup, while no Kaimera setting exists, LegacySettings copies every key saved under the former "Kira Canvas" name and its AppData `danbooru.csv` (when Kaimera has none); the former settings and files are never modified or deleted.
- Recent image thumbnails are in-memory session history only. New Image resets the draft and preview selection but retains history;  Save exports the selected preview using QSaveFile. Thumbnail and preview context menus offer Open, Save (PNG), Delete in that order (with optional Upscale after Save, and Copy prompt before Delete for generated images). Generated images keep the service's final unprefixed prompts in memory: the thumbnail tooltip shows them (rich text, wrapped) and Copy prompt copies the positive prompt; double-click opens in the native image app through QDesktopServices using a temporary PNG retained for the workspace lifetime. Delete removes only the in-memory entry and clears the preview when selected. Hovering a thumbnail shows a top-right remove (X) button that removes the entry like Delete; the menu item stays. Do not scan directories, persist/restore image paths, add Trash workflows, or modify existing files and legacy Library settings except through explicit user-selected export.
- The prompt fragment library and its bundled SQLite database are removed; do not restore them or Qt SQL unless explicitly requested. Do not delete users' existing image files or saved profiles.
- The Danbooru dictionary is a separate, requested feature (not the retired dictionary): `src/danbooru/` is a self-contained QtCore-only `danbooru-dictionary` library meant to be copied into other projects, so keep it free of Kaimera headers, settings and UI and keep its README (copy steps, CSV format, API, measurements) accurate. Never bundle or download the CSV: the user places a tagcomplete-format `danbooru.csv` in `QStandardPaths::AppDataLocation`; Kaimera loads it lazily with tags of at least 100 posts. For tags-format models it normalizes enhancer and translator tag answers to Danbooru prompt form, retries an enhancement once naming unknown tags with up to four suggested existing General tags (then keeps them), and submits prompts made only of known tags without the translator. Without the file, nothing changes.
- English/French Qt Linguist catalogs follow the system locale. Qt LinguistTools is a build requirement.

## Service SDK Integration

Editing uses ModelCategory::ImageGeneration models with canEdit; generation and
identity use canGenerate. Missing flags default true. Workspace generation routes
through service-owned preset IDs; editing routes through the selected editing
quality ID (qualityId), without presetId or style prefixes. Use source
dimensions, the quality's model and Steps/CFG and mapped-model
sampler/scheduler/companions for editing. Remove the Default edit model preference
control; retain Service/DefaultEditionModel unused.

All model selectors and model listings use SDK displayLabel() with the required
server displayName. New installs and explicit renames require nonblank names. Keep opaque IDs for selection
and preferences; Kaimera does not edit names or rename model files.

Service Preferences saves a local Default LLM opaque model ID under
Service/DefaultLlmModel, with None,
refresh preservation and Save/Cancel rules; no Kaimera feature currently uses it.
After accepted Generate submission, snapshot the original positive/negative drafts in session memory and clear both editors. A discreet Restore icon at the end of the step 4 header (before Sparkles) restores the last submitted pair, replacing the current draft; New Image and job failures preserve this snapshot. Rejected submission leaves drafts untouched. Available Models displays LLM and its server-saved recommendedOptions read-only.
Service Manager marks at most one LLM as Default prompt enhancer and at most one as
Default prompt translator (possibly the same) through the exclusive
defaultPromptEnhancer/defaultPromptTranslator recommendedOptions. Kaimera reads them
through SDK defaultPromptEnhancerModelId()/defaultPromptTranslatorModelId(), shows
them in Available Models and never changes them.

Prompt improvement (Sparkles) uses the default prompt enhancer through SDK
prompt.enhance, one job per nonblank text (never cached; no temperature override, so the
enhancer's sampling saved in Service Manager applies), with fixed
instructions from PromptInstructions: make the text more detailed and precise about
what it already says and better structured, keep every element, invent nothing and
output only the improved text. Each text has a kind (image prompt, things to avoid,
edit instruction, face, hair) with its own ordered structure. Kaimera detects French or
English (accents and common words) and writes the instructions in that language,
naming it ("Réponds uniquement en français"); unknown texts get English instructions
that keep the text's own language. A result detected in another language than its
source fails the job and keeps the draft. Blank texts stay blank. Instructions never
show a weighting example (models copy it); attention weights such as "word:1.2" that a
translation or improvement adds to a source without weights are stripped. The
instructions forbid removing any element, even a seemingly redundant one. Edit
instructions state that the LLM cannot see the image: more precise, not longer, naming
no person, body part, object, colour or detail the text omits, and no "keep unchanged"
clause unless asked. The image prompt structure ends with style, never framing, and
every kind forbids adding framing, shot, camera angle, lens, format, resolution or image
quality terms (the application owns them), without quoting examples (models copy them).
The generation card's image prompt (not the negative, edit or identity texts) also gets a
composition context in the instructions' language: what the selected camera shot shows
(nothing for Free) and the orientation with the target ratio name, never pixels. It is
marked never to be written: it only decides where details matter (elaborate what is clearly
visible, stay brief on what is small, add no detail out of frame) without removing anything. Unlike the
other kinds, the image prompt is written in the selected preset model's SDK promptFormat()
(natural: fluent sentences for Flux; phrases, the default: comma-separated phrases for SDXL;
the negative and other texts keep their structure). For a tags model (Illustrious), Sparkles
on both generation prompts instead uses English-only Danbooru tag instructions whatever the
text's language (lowercase, comma-separated, spaces for underscores, exact Danbooru spelling;
subject count tags first; the same pose, composition, enrichment and clothing/nudity rules; the
image prompt also lists danbooruPoseVocabulary: fixed common Danbooru pose tags, checked against
the dictionary, grouped by the body parts the shot shows and ordered by posts; the negative
becomes tags without new elements). Those answers skip the same-language and
kept-words checks; only framing/quality tags the source never mentions (inventedTagWords)
trigger the retry and failure. It turns the idea into a complete prompt that pins down each
person's or animal's pose part by part to prevent model drift, listing only what the shot
shows (overall posture, head and gaze, shoulders for portraits, torso, arms and hands, hips
and legs for cowboy, legs and feet for full body/wide/Free; none for extreme close-up),
making the text's pose more precise and choosing a natural one consistent with the action
and setting where it says nothing, one position per part, never two contradictory ones. It may be enriched with plausible visible details (appearance,
clothing, materials, posture, gestures, expression, atmosphere) consistent with the text, never a new
subject or person, never changing what the text specifies; described clothing or nudity is
always respected (never dress a nude subject or change a described outfit).
Kaimera checks that every content word of the source (four letters
or more, not a function or generic verb, its first five letters found accent-insensitively
within an answer word) survives, and that the answer introduces no person, body part,
colour, clothing, framing or quality word (lexicon of the source's language) whose first four letters no
source word starts with (for the image prompt, only person, framing and quality words). On either, it retries once with instructions naming what was
dropped or made up, then fails the job listing it and keeps the draft. Any answer caught in a
loop (repeatsItself: a three-word sequence eight times or more) is never kept: an improvement
retries once, then fails like a translation, keeping the draft. Translation and
improvement both send penalizeRepeats false (service prompt.enhance override), so the
prompt-wide repetition penalty cannot push rewrites toward synonyms or English;
deploy the service first.
Flat Sparkles buttons sit at the end of the generation card's step 4 header (both
prompts, after Restore), in the edit bubble under the paperclip, left of the
instruction field (the instruction, same square size as the paperclip) and at the end of the identity
panel's step 2 header (face and hair descriptions). Each is enabled only with an
available enhancer, no active job and some text, with a tooltip explaining otherwise
(including where to choose the enhancer). Results replace the editors in place; they
share the single-job guard and the pill of their surface ("Improving the prompts",
"Improving the instruction", "Improving the descriptions"); failures keep the editors
and fail the pill. Improved text is translated like any other at submission.

Prompt translation: workspace generation (both
prompts), edit bubble instructions and identity portrait descriptions are translated
into English before submission by the default prompt translator through SDK
prompt.enhance (one job per nonblank text, temperature 0, fixed instructions to output
only the faithful English translation, nothing else). It shares the single-job guard
and the pill, and a translation failure fails the job without submitting an image job.
Without a translator, or when the service cannot run it, prompts are sent unchanged.
For a tags-format preset model, the translator instead converts both workspace prompts into
English Danbooru tags (faithful: nothing added or removed, existing tags unchanged), cached
separately from plain translations; edits and identity portraits always translate normally.
Edit with an identity (toolbar) sends the identity's English editPrompt directly with
no LLM. Identity generationPrompt instructions are appended after translation. An
in-memory session cache keyed by translator and trimmed text reuses translations;
editors and restored drafts always keep the original text. The service no longer
has preset prompt flows or a skipPromptFlow option (it rejects unknown options).

Presets have no quality layer. Service Manager owns the presets (1–16, added
and removed there; Photorealism and Illustration initially) and, for each
preset and for each fixed editing quality (Fast, Balanced, Quality; exactly one
the default), one model assignment with a saved Steps/CFG snapshot, plus the
presets' editable names and prefixes. Kaimera lists whatever presets the
service sends and falls back to the first when the selected one is removed. Transmit both name (English) and nameFr; Kaimera uses SDK
displayLabel with the system locale. Kaimera has no management UI. Choose the
first preset initially. Preserve the opaque preset selection on refresh,
reconnect and New Image. Keep an unassigned/unavailable/filtered preset
selected, disabling submission with an explanation on the preset selector and
Generate. Steps/CFG use the preset's saved defaults; resolution and sampler/
scheduler follow the mapped model. Reset Steps/CFG overrides on preset changes
even for the same model; same-preset refresh/New Image preserve manual edits.
Lock the preset selector during single jobs (not queued generations). Editing uses the selected quality's
values without preset prefixes (the service prepends the quality's own
prefixes to the sd-cli prompts; Kaimera never sees or shows them); an
unassigned/unavailable quality keeps
the bubble with a disabled Edit button explaining why. Service composes
generation prefixes once; editor text is unchanged.
Identity uses an independent preset selection with the same routing,
availability, refresh and job-locking rules as generation.
Service applies the selected preset's positive/negative prefixes to every view.
Preserve legacy style settings without applying them; require routing-capable service.

Kaimera defaults to the sibling `../nukleus` checkout and adds only
`nukleus/src/sdk`. Set `NUKLEUS_SOURCE_DIR` to override that path.
When it is not a valid checkout, FetchContent uses an installed `NukleusSdk`
0.1 package or clones the SDK of Nukleus tag `v0.1.0` (SOURCE_SUBDIR src/sdk), and
Kaimera links `Nukleus::SDK`. The SDK owns transport, protocol v3,
model metadata, jobs, progress, and image bytes. Kaimera owns QSettings, local
files, output paths, model-selection availability, and translated UI behavior.


## Ardoise Integration

Kaimera links `Ardoise::Widgets` in both the application and its UI test
executable through FetchContent: an installed Ardoise 0.1 package is used when
found, otherwise tag `v0.1.0` is cloned. A local checkout is selected with
`FETCHCONTENT_SOURCE_DIR_ARDOISE`.

Ardoise owns the shared theme, style, icons and reusable Qt Widgets
components. Kaimera owns screens, workflows, settings, files, and service
integration. Call `Ardoise::Theme::apply()` explicitly at startup. Keep framework
tests in its standalone build. See the framework's `docs/design-system.md` for
tokens, component APIs and platform behavior caveats.

## Planned Architecture

These are future areas only. Do not create their directories, types, or infrastructure until explicitly requested.

- General image-to-image controls; text-to-image generation and reference-image editing are implemented through the SDK.
- Dedicated upscale screens; context-menu 2× image upscaling is implemented through the SDK.
- Model-management UI - installed generation model discovery is already provided through the SDK.
- `settings` - application and generation settings.
- Nukleus owns backend execution; Kaimera must not invoke stable-diffusion.cpp or `sd-cli`.
- Direct stable-diffusion.cpp C/C++ integration belongs to Nukleus and is outside Kaimera scope.

## Build Integration

Kaimera requires CMake 3.24, Ardoise 0.1 (installed package or pinned tag), Qt SVG and LinguistTools.
The app uses the violet K artwork in `assets/icons/`. Embed PNG sizes through Qt resources and set the application window icon at startup. Linux uses `me.kodara.kaimera.desktop` and hicolor icons with the matching Qt desktop identifier. macOS bundles include `kaimera.icns`. Icon exports are committed; do not add runtime conversion dependencies. Linux launchers use the configure-time installation prefix.

The application derives from `Ardoise::MainWindow`, built on Qt Widgets. Its theme, controls and chrome are shared across platforms without platform UI APIs. Qt provides system move/resize/window operations. Native compositor and Windows/macOS behavior require on-device verification. The macOS target is a host-architecture developer `.app` bundle only; signing, notarization, universal binaries, and deployment tooling are outside the current scope.

## Workflow

Use simple command-line workflows:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/src/kaimera
```

Use the actual generated executable path rather than blindly preserving this example.

The project is MIT licensed. `kaimera --version` prints "Kaimera <CMake project version>"
(KAIMERA_VERSION). CI (`.github/workflows/ci.yml`) builds and tests on Arch Linux; a `v*`
tag matching the project version drafts a release (`release.yml`) with the source archive,
`scripts/get-kaimera.sh` and SHA256SUMS, notes from `docs/release-notes/<version>.md`.
get-kaimera.sh verifies the checksum, builds, runs the tests and only then installs under
~/.local (~/Applications on macOS); keep `tests/GetKaimeraTest.sh` in step with it.

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

## Local Todo Workflow

- Use `.codex/TODO.md` for the local implementation queue. It is intentionally git-ignored and must remain available only in the working copy.
- Add an item only when the user explicitly asks to add a todo. Reframe the request as one concise, imperative, implementation-ready checkbox line that preserves the requested scope and acceptance criteria.
- Keep one independent task per line. Do not add completed items, speculative backlog items, or implementation notes that are not needed to identify the work.
- When the user asks to implement a todo, match the request to its reframed line and treat that line as the task statement. In Plan mode, use it to produce the implementation plan; in Default mode, implement it directly after inspecting the repository.
- Build and run relevant tests after implementation. Remove the todo line only after the task is complete and validated. Leave an incomplete or blocked line in place and explain the blocker.

- Generation failure diagnostics display the service-provided sd-cli command, exit code and stderr in the failed progress pill as selectable plain text (workspace generations: in the failed history tile's tooltip and Copy error). Commands are diagnostic text and are never executed by Kaimera.

- Editing lives in a floating EditBubble overlaid on the bottom of the preview viewport (centered, max 680 px wide), not in the workspace layout. It contains a three-line instruction field between a column holding the square paperclip above the square Sparkles (together exactly the field's height) and a full-height Edit button, so all three blocks share the field's top and bottom edges with no gaps, with a three-stop QualityStepSlider in its header (one stop per service editing quality, labels under each stop; unavailable qualities flagged in warning color with the reason as tooltip, otherwise the model name). The slider is always shown; it starts on the service's default quality, preserves the selected quality ID on refresh and New Image, and locks during single jobs (not queued edits). Show the bubble, sliding/fading in, only when service routing is available (including an unassigned/unavailable quality, explained by the Edit button's tooltip) and an image is selected; it fades out otherwise. It starts collapsed as a compact "Edit image" pill; the pill expands it and focuses the field, and its chevron (or Escape in the field) collapses it again for the session. The preview's dimensions badge stays in the bottom-left corner and rises above the bubble only while they overlap. Enable Edit only for nonblank instructions, a selected image and an eligible model for the selected quality with SDK availability and no active single job (queued edits and generations never block it). Queue image.edit with the quality's qualityId and modelId, source dimensions, the quality's Steps/CFG and mapped-model sampler/scheduler behind a pending history tile that the result replaces without selection (see the generation queue rules), retaining its source. The paperclip adds up to four reference images through the Qt file dialog, shown as numbered removable thumbnails above the field; submit them in order after the source image in editImages and clear them with the instruction on Edit. The bubble has no identity button; identities are applied to edits through the toolbar's Edit with an identity. Submitting an edit (button or Enter) collapses the bubble back to its pill. References are in-memory draft state and never enable Edit by themselves.

- Kira Sparkle (KiraSparkle) is a creation/editing request bubble: a round Sparkles
  FloatingSurface bubble in the preview viewport's bottom-right corner (spacing16),
  offered with or without a selected image and hidden while the identity panel is
  open. The bubble stays visible and toggles a chat panel that grows (up and left) to
  its left, spacing8 away, at most 480 px wide (room for the base, four references, the engine button and Send) and as tall as the viewport allows
  (spacing16 from its top down to the bubble's bottom edge, following its resizes): header (Sparkles, "Kira Sparkle", no chevron, a flat Plus "New chat" button
  at the right that empties the conversation and its attachments and greets again,
  keeping the draft and history), a scrolling conversation
  kept at its newest message (Kira Sparkle's on the left on panelBackgroundHover,
  starting with "How can I help you?"; the user's on the right on accentMuted, text
  only; wrapped at four fifths of its width), a full-width
  two-line request field, then a toolbar: a square Plus attach button, the base image
  and the reference images on the left. The base image (at most one, the image to
  change) is a ReferenceImageStrip in base mode (setBase: unnumbered, accent border,
  removable); the references are the shared numbered ReferenceImageStrip (without its
  count, up to four, as in the card and the edit bubble). The attach button stays
  enabled and opens a menu above it, left-aligned: "Add a base image…" (framework
  Upload icon, one image through the Qt file dialog) and "Use the displayed image as
  base" (framework Image icon, the selected image), both replacing any current base;
  a separator; then "Add reference images…" (Upload), "Add the displayed image as
  reference" (Image) and "Add an identity as reference…" (Person, a .kiraid's
  references). Displayed-image choices are disabled with a tooltip without one;
  reference choices are disabled with a tooltip when four are attached; an addition
  that would exceed four references is rejected as a whole with a warning. A base
  image will make the request an edit, so the engine button dims the preset half
  (else the quality half). On the right come the engine button and an icon-only
  primary Send (framework Send arrow), all as tall as a thumbnail so attaching never
  moves the toolbar. The engine button
  (icon-button frame, Send's height, a thumbnail wide plus spacing16 on each side) shows
  the New image card's preset as a monogram tile (KiraSparkle::monograms: the name's
  first two letters, else its first two words' initials, else its first letter and a
  number, unique in service order; warning colour when it cannot generate), a divider,
  the edit bubble's quality as rising bars (one per quality, filled up to the selected
  one; warning when it cannot edit) and a chevron; its tooltip names both ("Creation: …
  Editing: …" with any reason). It opens a menu above it, right-aligned, with a
  "Creation" section (presets with their monogram icons) and an "Editing" section
  (qualities with their bars); the current choices are bold and those that cannot run
  read "Unavailable" on the right with the reason as tooltip, yet stay selectable like
  in the card and the bubble. Choosing selects the card's preset or the bubble's
  quality, and their changes show in the button; it locks during single jobs like the
  preset selector. Send moves the request into the conversation
  (clearing the field) and keeps the panel open. The attachments belong to the
  conversation, not to a message: they stay beside the attach button and go with every
  request until removed or a new chat starts. MainWindow answers through addReply (see
  the first answer below). The result bubbles (KiraSparkle::addResult and
  GenerationWorkspaceWidget::showSparkleResult) stay available for when creating and
  editing run again, each in its image's ratio (at most 220 px tall), keyed by its history
  ticket: the tile's progress ring with
  percentage (spinning when unknown) and status, then the delivered image (double-click
  opens it externally, right-click shows the thumbnail menu without Delete once the
  history entry is gone) or the failure (danger ring, message as tooltip). Enter (including keypad
  Enter) sends only when enabled and nonblank; Shift+Enter inserts a newline; Escape,
  the bubble or a click elsewhere in the window (not in dialogs it opens) collapses.
  The dimensions badge sits in the opposite (bottom-left) corner. The bubble
  paints its own token-coloured stars (the Sparkles icon's shapes, centred), a
  thick conic-gradient ring and a radial core: hover eases in a halo, thickens and
  spins the ring and fills and twinkles the stars in turn; a press dips it and sends
  a ripple; it stays lit while the panel is open; keyboard focus half-lights the halo. Send emits the request, the base image (null without one) and the
  references (workspace sparkleRequested), keeping them attached. Expanding it collapses the edit bubble and
  vice versa. Send is enabled only when the default prompt enhancer can run SDK
  text.chat and no single job is active (tooltip explains otherwise).
  First answer (SparklePlanner.h): the default prompt enhancer receives the request in
  one text.chat job (single-job guard; no pill: its progress, "Understanding your
  request…", shows as a spinning message in the chat until the plan is answered or
  fails; starting it dismisses a finished pill; no temperature override, penalizeRepeats false) with fixed English
  instructions describing the workspace (displayed image size and final prompt if
  any; an attached base image, the image to change; the reference count, as
  inspiration; it cannot see images), the user's selected preset when it can generate
  now (English name, positive prefix as style, model promptFormat), the fixed camera
  shots and ratios, a short fixed description of the application, and the request's
  language (SparklePlanner::requestLanguage: detected, else the application's French or
  English) for every text written to the user. Kira Sparkle never chooses the preset or
  the editing quality: the user does, in the engine button. A jsonSchema
  (service-validated) offers one anyOf branch per possible action, the action first and
  only its own fields, each text bounded (maxLength) so a rambling field closes instead
  of running to maxTokens: reply (message), ask (a follow-up question offering options),
  prompt (a prompt written as text in the preset's format and the request's language,
  English Danbooru tags for tags models, with a short message), generate (with an
  available selected preset, also for image ideas: shot, ratio, a description of the
  image in the request's language, avoid (what the user's words exclude, in that
  language, usually empty), then the English prompt of about 40 to 80 words in the
  preset's format without the preset's name, ratio, framing, quality terms or
  exclusions, and negativePrompt, the avoid terms in English), edit (whenever the
  selected quality can edit, except while a creation proposal is the latest answer, when
  a change such as "yes but at night" must become a new proposal: English instruction;
  it changes the base image, else the displayed image) and look (with the default image
  describer able to see images, SDK canChatWithImages, and some image: the images to
  show by name, base, reference n or displayed, and a self-contained describerQuestion
  in the user's language, named apart from ask's question). Kaimera validates the plan
  (unknown shot → Free; unknown ratio → current; unknown images dropped; blank texts
  fail; repeated comma-separated terms are kept once; a generate negativePrompt is
  dropped without avoid, and loses terms its prompt contains). Reply, ask and prompt
  show their text in the chat. Generate is a proposal (KiraSparkle::addProposal): the
  description, "Shot: … · Ratio: …" (translated shot name), any "Avoid: …" and the
  references note, with the English prompts as the text's tooltip and a primary "Create
  the image" button; a newer proposal disables the waiting one ("Replaced"). It runs
  only when accepted: its button, or a request that only accepts the latest proposal
  (SparklePlanner::isAcceptance: accepting words such as oui, ok, vas-y, ça me va,
  do it, with politeness words, and nothing else), which skips the LLM and is not
  remembered; the LLM never accepts (it took "yes but at night" for an acceptance). The
  button then reads "Created" and the creation queues like the
  card's Generate (pending history tile, Sparkle result bubble) with the card's preset,
  Steps/CFG and sampler, the plan's ratio size (resolutionForRatio, else the card's),
  shot framing and English prompts (queueGeneration without translation; tags models
  still get Danbooru spelling) and the references attached when it was proposed, in
  order (never the base); the New image card is not changed. The delivered image
  replaces Sparkle's base image, and while it stays the base the instructions say it
  was created from the accepted proposal, with its prompt. Right-clicking an image in
  the chat also offers "Use as base image" and "Add as reference" (Sparkle only, before
  Delete). When the preset's model cannot use references,
  the instructions tell the planner (describe everything, name no reference), the
  references are left out and the chat says so. In the panel, such a preset reads "No
  references" in the engine menu; attached references then show as ignored (they only
  serve edits), the engine button's preset half turns warning with a tooltip line, and
  the attach menu's reference choices are disabled only when neither the preset nor the
  editing quality can use them. Edit and look only show what would happen next
  ("Next step, not run yet: …" with the instruction or question) without queueing
  anything; an edit without a base or displayed image asks the user
  to attach the image to change as the base image. Short memory: MainWindow keeps the
  last six answered exchanges (the request, then the JSON plan as the assistant turn, so
  prompts and instructions come back exactly) and sends them between the instructions
  and the new request; failed requests are not remembered, and New chat
  (KiraSparkle::conversationCleared) forgets them and any waiting proposal. The
  instructions say a generate answer is only a proposal until accepted (its image then
  becomes the base) and edit and look answers did not run, that a request referring to one ("generate it",
  "the first one", "same but darker") reuses its prompt, instruction or idea with only the
  asked changes (English where needed), and that changing an earlier prompt or generate
  answer repeats that action, never an edit. A routing bench against the real enhancer
  (Qwen2.5 14B) validated the instructions on 65 cases (18 of them multi-turn,
  including proposals changed or accepted) in six workspaces. Planning failures answer in the chat and give the request back to an empty
  Sparkle field.
- Automatically use the first upscaler in the service catalogue; an empty list means no upscaler. Do not expose upscaler configuration in Service Preferences. Retain legacy Service/DefaultUpscalerModel and Service/DefaultEditionModel settings unchanged and unused. Show Upscale for an available eligible first upscaler and enable only with SDK availability and no active job. Upload the targeted image, retain it, append/select exactly twice its dimensions and smoothly resize native output when needed. Share the progress pill.


The toolbar's checkable Person icon toggles the IdentityPanel: a FloatingSurface
panel that slides and fades out from the toolbar's right edge,
matching its height, over the workspace. While open, the New image card and the
edit bubble step aside; the createPromptIdentityAction opens it. A header (Person
icon, "New identity", caption, close button) tops a guided path mirroring the
generation card: step 1 "Choose the style" (Preset only; the selected preset's
service-owned identityViews — 1, 2 or 4, set in Service Manager, default 1 —
sets the view count), step 2 "Describe the face" (required, Sparkles at the end of its header) and step 3 "Describe the hair" (optional). The footer has the
Ctrl+Return hint, Save identity… and the primary Generate portraits button
(Regenerate portraits once a set exists); Ctrl+Return/Ctrl+Enter click it only when
enabled and Escape closes the panel. The right column holds portrait slots with
the portraits' aspect ratio (delivered images, else the planned portrait size):
one, a pair (stacked when side by side does not fit), or a 2 × 2 grid. The panel
width follows the window: it is sized so the grid spans exactly the steps' height,
aligning with step 1 and the hair field, narrowing the controls from 380 to 300 px
before the grid would shrink (then centred). Slots glide to their layout, name their view when
empty, show a sweeping accent border on the slot being generated and fade in
fitted portraits with a view chip. Slots preview the selected view count until a
set delivers; a delivered set keeps its count until the next set delivers. Its
own progress pill floats over the portraits ("Generating portraits", then
"Generating portrait i of n"). Its prompt and parameters are independent of the
workspace. Keep portraits in memory outside session history, allow regeneration
and local export, and reuse the single-job lifecycle. Closing hides the panel
without canceling jobs and keeps its draft; a finished or failed set reopens it.
Save identity exports the SDK IdentityPackage atomically through QSaveFile as a
version-2 .kiraid ZIP: manifest.json (type kira-identity, version 2), reference.png
and optional ordered reference-2.png through reference-4.png. Store face/hair
prompts, seed metadata and fixed English generationPrompt/editPrompt instructions
telling generation and editing to use the references for face and hair. No extraction job, binary payload or toolbar export.
Saved identities are attached through the workspace's identity buttons and Edit
with an identity tool (see the generation panel and edit bubble rules). Views are front, three-quarter left, three-quarter right,
then full-profile left at 90° (two views: front and profile). Generate only the
selected views sequentially. Turned views look straight ahead in the direction of
the face, never back at the viewer. Snapshot the request and reuse its seed,
description and parameters; change only orientation prefixes and remove
conflicting three-quarter negatives. Saving requires all generated views from the
last successful portrait set and exports them together. Keep separate face
identity and scalp hair editors. Snapshot cleaned descriptions and seed with the
successful portrait set. Save these as package metadata alongside the selected
ordered PNG references.
There is no identity AI button or identity-specific LLM. Generate first translates
the composed face and hair description once (see Prompt translation; sharing the
single-job guard and the panel's pill), then every view submits the English
description without further translation. The translation cache reuses it on
regeneration. Translation failures release the job, keep the editors and portraits
and fail the pill.
Hide resolution, Steps and CFG in the panel; Steps/CFG follow preset snapshots
with mapped-model recommendations/shared defaults as fallback. Keep recommended
square/portrait dimensions; transpose landscape dimensions to portrait without
changing the pixel budget. Double-click opens a portrait in the native viewer
through QDesktopServices with a temporary PNG retained for the panel lifetime.
Use viewpoint/viewer terminology for orientation; never mention a camera in
the built-in positive prefix. Exclude cameras, photography equipment and handheld
props in the negative prefix. Apply framing, neutral expression and lighting to every
portrait request without a separate UI note.
Specify a camera directly in front at eye level, an upright forward-facing head,
level eyes and shoulders, and the entire head with background margins above and
on both sides so the face outline remains visible.
Frame from the upper chest to above the highest hair, with clear background
headroom and both shoulders visible; discourage tight framing and cropped crowns
or shoulders in the portrait negative prompt.
Every portrait request also includes a negative prompt discouraging tilted heads
or cameras, side/profile/three-quarter views, diverted gaze and cropped or
extreme-close-up faces.
Also exclude accessories, jewelry, headwear, face coverings, tattoos and
decorative skin markings, clothing and all makeup from identity portraits.
Omit matching comma-separated description traits, including refined ones, from
submitted portrait requests without changing the editor. Never add body-shape or
anatomy descriptions.
Exclude camera/REC overlays, viewfinder frames, interface indicators, borders,
text, watermarks, signatures and logos as well.
Specify adult, nude and bare shoulders in portrait prefixes, without adding body
shape details or expanding the head-and-shoulders crop. Describe viewpoint changes
around an aligned head and torso, rather than independent head turns. The 90°
profile must include side-on torso alignment, one visible eye and near shoulder;
remove shared framing instructions requiring both shoulders for that view.
Define left/right relative to image edges, with matching nose and gaze direction.
Do not mix subject-relative viewpoint directions with image-relative facing.
Hide seed controls and choose one random explicit seed for each portrait set.
Turned portrait requests include concise side-view tags and discourage frontal
faces in their negative prompts.
Build each turned view from an independent orientation prefix followed by compact
shared framing. Use image-relative 45-degree left/right views and a strict 90-degree
left profile with one visible eye. Discourage the opposite direction, looking back,
independent head turns and hands near the face; never inherit frontal symmetry or
viewer-facing gaze instructions into turned views.
Three-quarter views explicitly align the diagonal eye gaze with the nose, place
the nose tip on the intended side of the face center and retain a foreshortened
far eye; discourage eyes glancing back toward the viewer.
All portrait requests exclude hands, fingers and fingers touching the face. There
is no local view selector: the view count follows the selected identity preset
(no routing means 1) and the preset selector is locked during jobs; do not infer a
view count from model names. The count applies only to identity portraits, never to
workspace generation or editing. Export only a complete generated set, preserving its
count when the preset or its count changes until another set starts delivering images.

Available Models includes the Companion category and its VAE/mmproj subtype.
Show LLM mmprojModelId assignments by companion display name. Upscaler and
Companion entries have no explicit-content classification. Upscaler has no options.
