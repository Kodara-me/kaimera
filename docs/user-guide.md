# Kaimera user guide

Everything Kaimera does, screen by screen. For installation and an overview, see the
[README](../README.md).

Generation and editing use image-generation models and their installed companions.
Configure each generation preset's model and each editing quality's model in Service Manager → Presets….
Kaimera selects a Preset for generation and an editing quality (Fast, Balanced or Quality) for editing.

Model selectors and listings show the required display name configured in
Nukleus Manager. New installs and explicit renames require a nonblank name.

Kaimera is a Qt Widgets desktop application with a shared Kira visual design targeting Linux, macOS and Windows.
Its generation workspace submits single-image text-to-image jobs to Nukleus.

## Using the app

The main window has a minimum and default size of 1300 × 700 logical pixels of window content,
excluding the native title bar. Window buttons follow the operating system. Previously saved geometry
takes precedence within the minimum size. The workspace contains a vertical editing toolbar and the image
preview with session thumbnails. The app starts on an empty preview. The
**New Image** toolbar button toggles a landscape **New image** card centred over
the preview, whatever it shows, that follows window resizes; press Ctrl+Return
(⌘↩ on macOS) in it to generate. Its close button or Escape puts it away, which
brings back editing for the selected image.
The panel guides you in three steps: describe your image, say what to avoid
(optional), then choose the look with the preset, a strip of to-scale format tiles
with a landscape/portrait switch. Steps, CFG and seed are not shown: they follow the
chosen preset and every image uses a random seed.
The dark graphite/violet content theme comes from Ardoise. The cog
button at the bottom of the toolbar opens Service Preferences directly. The interface uses French for French
system locales, English otherwise.

- **Generate** submits the prompts and side-panel values using the selected **Preset**.
  Once submission starts, both prompt fields clear. The discreet restore icon beside
  the prompt assistant restores the last submitted positive and negative prompts,
  including after a failed job or New Image. This snapshot lasts only for the session.
  Service Manager owns prefix prompts and, per preset, one model with its saved
  Steps/CFG; presets have no quality tiers. Presets are added and removed in
  Service Manager (**Photorealism / Photoréalisme** and **Illustration**
  initially); Kaimera falls back to the first preset when its selection is removed.
  Labels arrive in both languages; Kaimera uses the system locale. Assign models in
  Manager before submitting; choosing one applies its recommended Steps/CFG.
  The service applies
  generation prefixes once. The restore button keeps the original submitted text.
  The selected preset survives refreshes and New Image. An unassigned or
  unavailable preset model disables submission (the preset's tooltip explains
  why) without switching presets. Update Service first.
  Prompts may be written in any language: when Service Manager marks an LLM as
  **Default prompt translator**, Kaimera has it translate both prompts into English
  (and nothing else) before generating; the editors keep your original text and the
  same text reuses its translation for the session. Without a translator, prompts
  are sent unchanged. Presets have no prompt flows.
  The ✨ (Sparkles) button at the end of step 4 — also in the edit bubble and the
  identity panel — asks the LLM marked **Default prompt enhancer** to make the
  current text more detailed and better structured, without inventing anything and
  in the same language (French stays French, English stays English). For the image
  prompt it knows the selected camera shot and format, writes in the prompt format set
  on the preset's model in Service Manager (sentences for Flux, short phrases for SDXL;
  for Illustrious-style tag models it answers in English Danbooru tags, and Generate has
  the translator convert both prompts into tags; with an optional Danbooru dictionary,
  see below, tags get their exact Danbooru spelling and prompts of known tags skip the
  translator), turns your idea into a complete prompt that states the pose of each visible person or animal (posture, head and gaze,
  arms and hands, legs and feet, as far as the shot shows them) so the image model does
  not drift, and may enrich what will be
  visible (appearance, clothing, gestures, atmosphere) while respecting what you wrote,
  including described clothing or nudity; it never adds people, framing or quality
  words. The result replaces the text so you can review it before generating; an
  answer in another language, or one that still drops part of your text or invents
  what it must not (people, framing or quality words; for the other texts also body
  parts, colours or clothing) after one automatic retry, is refused and your text is kept. Requires an up-to-date Nukleus.
  New images queue on the service: Generate stays available while earlier images
  are pending (only a prompt translation, edit, upscale or improvement holds it).
  Each generation adds a tile to session history showing a progress ring, its
  percentage and stage ("Translating the prompt…", queue position, sampling…);
  the finished image replaces the tile without being selected, so the New image
  card stays open. A failed generation leaves a tile with the error, including the
  sd-cli diagnostic, as its tooltip; its context menu copies the error or deletes
  the tile. Pending and failed tiles cannot be selected.
  Edits, upscales, sharpening and prompt improvements run one at a time; their
  status, stages and percentage appear in a discreet floating pill at the top of
  the preview (a progress ring, the job, its stage and percentage). The pill widens
  to fit its text (up to 720 px) and does not shrink during a job; the New image
  card moves down to clear it when there is room. Success briefly shows a check and
  fades it out; failures expand it and stay until dismissed, including the attempted
  sd-cli command, exit code and backend error; the diagnostic text can be selected and copied.
- The New image card guides five steps: 1 the look (preset and format), 2 the camera
  shot, 3 reference images, 4 the description and 5 what to avoid. Step 2 picks a
  framing (Free, extreme close-up, close-up, portrait, medium, cowboy, full body or
  wide), reset by New Image. Except for Free, each shot adds fixed English framing
  words to both prompts, after the preset's style and before your text; they are
  never translated nor shown in the editors.
- Step 3 "Reference images" (optional) attaches up to four images to the New image
  draft with its paperclip button and shows them beside it; they are uploaded in order with Generate and cleared by New Image.
  The person button beside it attaches a saved .kiraid identity: its references join
  the strip and its generation instruction is appended to the submitted prompt without
  appearing in the editor. The instruction is added after any translation so it
  reaches the image model unchanged. Only presets whose model can edit (**Can Edit
  (uses reference images)** in Nukleus Manager) use references: with another
  preset, both buttons are disabled and attached images are dimmed with a warning
  border, explained in their tooltip, and left out of Generate.
- **Open** adds an image to session history; selecting a thumbnail previews it.
- Images default to actual size (100%). The fit/actual-size choice is saved across
  image selections and application restarts. Manual zoom/100% exits fit mode.
  The icon toolbar above the
  preview offers fit/actual-size toggle, zoom out/in, 100%, crop, Upscale 2×,
  Downscale 2×, Improve sharpness, Animate, then a section with New identity and Edit with
  an identity, and Save as. Improve
  sharpness edits the selected image with the default editing quality's model and
  fixed prompts for a light, natural sharpening pass (no quality prefixes, no LLM),
  adding the result to history; run it again on the result for a stronger effect.
  Edit with an identity asks for a .kiraid and
  immediately edits the selected image with its references and edit instruction. Crop by dragging a rectangle; release adds
  a new cropped image to session history and preserves the original. Escape
  cancels crop mode. Upscale uses the configured service model and is disabled
  when unavailable or busy. Identity is a reserved, inactive button.
  The compact toolbar uses flat buttons. A translucent badge in the preview's
  lower-left corner shows native image dimensions and zoom; while cropping it
  also shows the selected native pixel dimensions. Clicking Edit clears its
  instruction field after capturing the submitted text.
- **Save** exports the preview in the selected output format (PNG, plus JPEG/WEBP
  when available). Output format and folder are saved in **Service Preferences**
  and initialize the Save dialog;
  exporting still requires an explicit file selection. View actions provide fit, actual size, zoom, and fullscreen;
  drag to pan and use Ctrl+wheel to zoom.
- Right-click a thumbnail or the preview for **Open**, **Save**, **Delete**. Open
  (also double-click) launches the default image app using a temporary PNG retained
  until the workspace closes. Save exports that image as PNG to a chosen destination.
  Delete removes it from session history and clears the preview if displayed; it never deletes saved files.
  Hovering a thumbnail also shows an **X** in its top-right corner that deletes it in one click.
  Generated images show the final prompt used by the service (after any
  translation) in their thumbnail tooltip, and offer **Copy prompt** before Delete.
- When the service provides routing and an image is selected, a floating
  edit pill slides in over the bottom of the preview. Click it to open a
  three-line edit instruction field, the paperclip and ✨ stacked on its left and a
  full-height **Edit** on its right; collapse it again with its
  chevron or Escape. Its **Fast / Balanced / Quality** slider picks the editing
  quality, starting on the default quality set in Manager; each quality uses the
  edit-capable model and Steps/CFG assigned to it under **Editing** in Manager.
  Until the selected quality has one, **Edit** stays disabled and its tooltip
  explains why. **Edit with an identity** always uses the default quality.
  Edit instructions are translated into English first when a default prompt
  translator is set; **Edit with an identity** sends its English instruction directly.
  No preset prefixes are applied; the service only places the selected quality's
  optional prefixes (set in Manager) before the instruction it passes to sd-cli. Enter submits when enabled; Shift+Enter inserts a newline.
  The paperclip button attaches up to four reference images, shown as numbered
  thumbnails; they are sent in order after the source image and cleared on Edit.
  To edit with an identity, use **Edit with an identity** in the toolbar.
  Submitting an edit collapses the panel back to its pill.
  Unassigned or unavailable mappings cannot submit.
  Edits queue like new images: each adds a progress tile to session history that
  the edited image (at the source's resolution) replaces without being selected, so
  you can keep editing; a failed edit leaves a tile with the error. The source image
  is retained.
- **Animate** (after Improve sharpness in the toolbar; **Animer** in French) turns the
  selected image into a short video. It needs a selected still image, a Nukleus 0.3
  service that offers `image.animate` with MP4 output (ffmpeg on the service) and an
  animation model with all its files; otherwise it is disabled and its tooltip says what
  is missing. It toggles a floating form over the bottom of the preview, in place of the
  edit bubble (the New image card closes): describe the motion (Enter submits,
  Shift+Enter adds a line, Escape closes and keeps the draft), and optionally attach one
  end image with the paperclip for the video to move towards. **Loop** (shown when the
  service can make ping-pong loops) plays the motion forward then backward for a
  seamless loop; it starts at the model's default. With several usable animation models,
  a selector picks one. **Short**, **Medium** and **Long** (33, 41 and 81 frames) set the
  length; the form shows the frame count and duration (and the loop's, with Loop on) at the
  model's frame rate, 16 fps without one. The choice starts at the model's default length
  and stays as you set it until another model is selected. Steps and the rest come from
  the model's defaults set in Nukleus Manager; the video keeps the image's aspect ratio at the model's default
  pixel area. The description is translated into English first, like edit instructions.
  A job takes minutes: it queues behind a progress tile in session history, and you can
  keep working meanwhile. The finished animation's tile shows its first frame with a
  play mark; selecting it plays the video in a loop in the preview (click to pause or
  resume), with its size, frame count and frames per second in the badge. Image tools
  (crop, upscale, downscale, sharpen, edit, animate) are disabled on an animation.
  **Save as** writes the MP4 video exactly as received; right-click offers Open, Save,
  Copy prompt and Delete. Animations stay in memory until saved.
- **Kira Sparkle** is the ✨ bubble in the preview's lower-right corner. It opens a
  chat to its left that greets you with "How can I help you?": the conversation, a
  two-line request field (Enter sends, Shift+Enter adds a line; Escape, the bubble or
  a click outside closes it) and, under it, the **+** attach button, the base image
  and up to four reference images on the left. Its menu sets the **base image** (the
  one to change: from the computer or the displayed image, replacing any current one,
  shown first with an accent border and no number) or adds **reference images** (from
  the computer, the displayed image or an identity); references that do not fit are
  refused. Then come the engine button and the **Send** arrow on the right. The
  engine button shows the creation preset as a monogram ("Ph" for Photorealism) and
  the editing quality as bars (two of three for Balanced); its menu chooses them, in
  sync with the New image card and the edit bubble. Your request joins the conversation;
  the attachments stay beside **+** for the whole conversation until you remove them
  or start a new chat. **+** (New chat) empties the conversation and its attachments
  and starts over. The LLM marked **Default prompt enhancer** reads the request with
  its context (the displayed image and what it was generated from, the base image and
  the references) and answers in your language: it replies, asks a follow-up question
  when the request is vague, or writes a prompt you asked for, right in the chat. When
  the request means creating an image (or asks for an image idea), it first proposes
  it in your language (what the image shows, the shot and ratio, what to avoid; the
  English prompts are in the tooltip). Nothing runs until you click **Create the
  image** or answer with a plain "oui", "ok", "vas-y"…; any change ("yes but at night")
  makes a new proposal. The image then queues with your preset and the references
  (left out, and said so, when the preset's model cannot use them), showing its
  progress and the result in the chat and the history (the New image card is left as
  it was), and becomes the base image so you can keep working on it. Right-click an
  image in the chat to use it as the base image or add it as a reference. When
  it means editing the base (or displayed) image, or looking at the images, which only
  the LLM marked **Default image describer** can see, **Kira Sparkle only says for now
  what it would do next** (the instruction or the question). It remembers the last six
  exchanges of the conversation, so "generate it" or "same but darker" continue from
  its previous answer; **+** (New chat) makes it forget them. The preset and editing
  quality are always yours. A failure keeps your request in the panel. Opening it collapses the edit panel, and vice versa. Requires
  Nukleus with text chat and an enhancer able to chat.
- **New Image** resets the draft and preview selection, retaining session history.

Service Preferences includes **Allow models with explicit content**, unchecked
by default and saved only with Save. It currently filters nothing: all models
remain selectable until the rule is revisited.

Configure an IPv4 address (or `localhost`) and the shared LAN token in
**Service Preferences** using the toolbar cog, a fixed-size dialog. Kaimera connects to
Nukleus on port `9384` and displays installed generation models. Manage model
installation and model inspection through Nukleus Manager.
Generation uses Steps and CFG from the selected
preset's saved values, with mapped-model recommendations as fallback, and follows
updated preset settings; model catalogue changes do not overwrite saved preset values.
Resolution choices use the mapped model's recommendations and
show name, target ratio and dimensions, with square, portrait and landscape
choices including 3:4, 3:5 and widescreen. Dimensions are aligned to 64 pixels;
the fullest nearby size within the model’s pixel budget is chosen. Approximate
ratios are marked with ≈. No choice exceeds the budget.
Models without recommendations use the shared service defaults. Legacy local
styles remain saved but are no longer applied or editable.
The advanced generation section is removed. Sampler and scheduler
come from the selected model’s recommendations (shared service defaults when absent).
Generate is available when the service supports generation and the selected model
is installed and assigned to the selected preset. Failures appear in their history tile and leave the previous preview intact.
Backend failures include the latest stderr/error line when available. Nukleus
supports complete checkpoints and standalone diffusion models with shared VAE and
LLM text encoder assignments configured in the service manager;
choose **Standalone diffusion model (UNet)** for split UNet GGUF releases. Kaimera
submits routing IDs and the resolved opaque model ID; the service resolves its files.
The Manager's **Can Generate** and **Can Edit** checkboxes control the generation
and editing model assignments independently. At least one must be enabled.
Existing models without these flags support both operations. LLM models appear
in the Manager's text encoder dropdown only when marked **Can be used as text encoder**.
The Manager also marks one LLM as **Default prompt enhancer** and one as
**Default prompt translator** (possibly the same); Kaimera shows these roles in
Available Models, uses the translator for prompts and the enhancer for Sparkles.

For Danbooru-tag models (prompt format **Tags** in Service Manager), you can add a
Danbooru dictionary so tags get their exact Danbooru spelling. Download the
tagcomplete `danbooru.csv` into Kaimera's data folder (on Linux):

```sh
curl -L -o ~/.local/share/Kaimera/Kaimera/danbooru.csv \
  https://raw.githubusercontent.com/DominikDoom/a1111-sd-webui-tagcomplete/main/tags/danbooru.csv
```

Kaimera was named Kira Canvas. On its first start it copies the settings and the
`danbooru.csv` saved under that name, and leaves the originals in place.

Kaimera loads it on first use, keeping tags with at least 100 posts. The dictionary
code is self-contained in [src/danbooru/](../src/danbooru/README.md) for reuse elsewhere.

Window geometry and service/output preferences
use QSettings. Images and thumbnails remain in memory for the current session;
Kaimera does not scan output directories or restore saved image paths. Existing
images and legacy Library settings remain untouched. Scene Builder, Character
Library, the retired prompt dictionary, model management and Trash remain removed. Upscale is available through image context menus.

## Application icon and launcher

The violet K icon is embedded in Qt resources, including for direct launches
from the build directory. macOS bundles include the corresponding ICNS icon.
Linux installation adds a desktop launcher and icons for KDE/GNOME, including
Wayland desktop identification; the launcher uses the installation's absolute
executable path. Installation and building are described in the [README](../README.md)
and [CONTRIBUTING.md](../CONTRIBUTING.md).

## Upscale and identities

Kaimera automatically uses the first upscaler in the service catalogue; an empty list makes upscale unavailable. The legacy default-upscaler setting is retained but unused. Editing instead uses the service-owned editing quality models; the legacy default-edit setting is retained but unused. Upscale retains the targeted source and adds a new selected image at exactly twice its dimensions, smoothly resizing native output when needed.


The toolbar's person icon toggles the **New identity** panel, which slides out
beside the toolbar over the workspace (the New image card and edit bubble step
aside while it is open). It follows the same guided path as image creation:

1. **Choose the style** — the preset, which also sets how many views to generate.
   Each preset's **Identity portraits** count is set in Nukleus Manager:
   1 view (front, the default), 2 views (front and profile) or 4 views (front,
   three-quarter left, three-quarter right and 90° profile).
2. **Describe the face** — natural facial traits, required to generate.
3. **Describe the hair** — optional.

**Generate portraits** (or Ctrl+Return) generates the preset's views sequentially
with one random seed and the same description and parameters. With a default
prompt translator, the face and hair description is first translated into English
once, exactly as for image creation; every view then reuses the translation, and
regenerating the same description reuses it for the session.
Progress shows in a pill over the portraits, and the slot being generated shows a
sweeping accent border. Escape, the close button or the toolbar icon closes the
panel without canceling the job; it keeps its draft and reopens when a set
finishes or fails.

The panel resizes with the window so the portrait grid spans exactly the height
of the steps, its corners aligned with step 1 and the hair field. Slots take the
portraits' aspect ratio; two views stack when they do not fit side by side.
Portrait slots preview the selected layout (one, a pair, or a 2 × 2 grid); a
delivered set keeps its layout until the next set delivers. Double-click a portrait
to open it in the native image viewer through a temporary PNG. Portraits stay
outside session history. **Save identity…** requires the complete set and exports
a .kiraid ZIP with manifest.json (type kira-identity, version 2), the ordered PNG
references, the seed, the face/hair descriptions and generation/edit prompts that
explain how to use the references.

Portrait requests add adult, nude, bare-shoulder head-and-shoulders framing,
neutral expression and lighting. Turned views use image-left/image-right
directions around an aligned head and torso; the profile shows one eye and the
near shoulder. Negatives exclude makeup, clothing, accessories, headwear,
tattoos, cameras and overlays, text, watermarks and hands. Comma-separated
makeup, clothing and accessory traits are omitted from submitted descriptions,
including refined ones, without changing the editors. The service adds the
preset's prefixes to every view. Steps and CFG follow saved preset values;
resolution keeps the recommended square/portrait size or turns landscape sizes to
portrait with the same pixel budget. Orientation still depends on the model's
prompt adherence.

The read-only model browser includes Companion entries (VAE and MMPROJ).
LLM companion assignments are managed in Service Manager. Upscalers have no
model options or explicit-content classification.
