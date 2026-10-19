# Danbooru dictionary

A self-contained Danbooru tag dictionary for prompts written for Danbooru-trained image models
(Illustrious, NoobAI, Pony and similar). It reads a tagcomplete-format CSV and:

- finds a tag from any spelling or alias (`Long Hair`, `long_hair`, `longhair` → `long_hair`);
- rewrites comma-separated prompts in Danbooru prompt form (spaces for underscores, escaped
  parentheses), keeping attention weights such as `(tag:1.2)`;
- lists the tags it does not know, so callers can retry an LLM or skip a translation;
- suggests the closest existing tags for an unknown one (`hand on the hip` → `hand on own hip`),
  by words in common, then popularity;
- exposes each tag's category and post count (how often Danbooru uses it).

It depends only on QtCore and knows nothing about Kaimera.

## Copying it into another project

1. Copy this folder (`DanbooruDictionary.h`, `DanbooruDictionary.cpp`, `CMakeLists.txt`,
   `README.md`) into the project, for example as `src/danbooru/`.
2. In the parent `CMakeLists.txt`, after `find_package(Qt6 ... COMPONENTS Core ...)`:

   ```cmake
   add_subdirectory(danbooru)
   target_link_libraries(your-target PRIVATE danbooru-dictionary)
   ```

3. Include `DanbooruDictionary.h`; the include directory comes with the target.

Nothing else is required: no resource, no generated file and no other Kaimera header.

## The CSV

The dictionary is not bundled: its data comes from Danbooru and has no clear license, so each
user downloads it. The format is the one of the
[tagcomplete](https://github.com/DominikDoom/a1111-sd-webui-tagcomplete) extension (MIT):

```text
1girl,0,6008644,"1girls,sole_female"
long_hair,0,4350743,"/lh,longhair"
```

Columns: Danbooru name (underscores), category (0 general, 1 artist, 3 copyright, 4 character,
5 meta), post count, then quoted comma-separated aliases. `/lh`-style entries are autocomplete
shortcuts and are ignored; an alias never shadows a real tag. The file is sorted by post count.

```sh
curl -L -o danbooru.csv \
  https://raw.githubusercontent.com/DominikDoom/a1111-sd-webui-tagcomplete/main/tags/danbooru.csv
```

## Usage

```cpp
DanbooruDictionary dictionary;
QString error;
// Tags with fewer than 100 posts are poorly learned; leaving them out halves the memory.
if (!dictionary.load(path, 100, &error))
    qWarning() << error;
const auto result = dictionary.normalize("1girls, Long Hair, (sitting:1.2), rock seat");
// result.prompt  == "1girl, long hair, (sitting:1.2), rock seat"
// result.unknown == {"rock seat"}
for (const auto* tag : dictionary.suggestions("hand on the hip"))
    qDebug() << DanbooruDictionary::promptForm(tag->name); // "hand on own hip", "hands on own hips"…
```

`suggestions` compares the words of tag names (ignoring linking words such as `on`, `the`, `own`
and plural endings) within one category, General by default, ranking by words in common and then
by posts. It is meant for English text, such as an LLM's tag answer.

Measured with the January 2025 `danbooru.csv` (140,782 tags), including the suggestion index:

| minimumPosts | Tags kept | Load time | Memory |
| --- | --- | --- | --- |
| 0 | 140,782 | ~250 ms | ~56 MB |
| 100 | 57,170 | ~130 ms | ~26 MB |
| 500 | 18,190 | ~65 ms | ~9 MB |

Load once and keep the object; `find` and `normalize` are hash lookups and `suggestions` takes
well under a millisecond.

## How Kaimera uses it

Kaimera reads `danbooru.csv` from its application data folder (`QStandardPaths::AppDataLocation`,
for example `~/.local/share/Kaimera/Kaimera/` on Linux) with `minimumPosts` 100, on first
use. For image models whose prompt format is `tags`, it normalizes the prompt enhancer's and the
translator's tag answers, retries an enhancement once naming its unknown tags with their
suggestions, and sends prompts
made only of known tags without translating them. Without the file, everything works, without
normalization.
