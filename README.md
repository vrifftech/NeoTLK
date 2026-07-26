# NeoTLK

[![CI](https://github.com/vrifftech/NeoTLK/actions/workflows/ci.yml/badge.svg)](https://github.com/vrifftech/NeoTLK/actions/workflows/ci.yml)

C++17 TLK editor, CLI, and optional wxWidgets desktop GUI.

The reusable native TLK model, codecs, and text-encoding layer live in `neoshared::tlk`. NeoTLK retains the application-specific editor, search, interchange, and TSLPatcher/HoloPatcher workflows.

## Build

This repository consumes shared code from the separate `neoshared` repository. Clone the repositories as siblings:

```text
workspace/
  neoshared/
  NeoTLK/
```

CMake automatically detects `../neoshared`. For another layout, pass `--neoshared-root /path/to/neoshared` to `build.sh`, `-NeoSharedRoot C:\path\to\neoshared` to `build.ps1`, or set `NEOSHARED_ROOT` directly.


Linux GUI build:

```sh
./scripts/build.sh --wx ON --require-wx ON --jobs "$(nproc)"
```

Linux CLI/core-only build:

```sh
./scripts/build.sh --wx OFF --jobs "$(nproc)"
```

Windows GUI build with the shared, pinned wxWidgets 3.3.3 overlay:

```powershell
& ..\neoshared\scripts\install-wxwidgets.ps1 `
  -VcpkgRoot C:\vcpkg `
  -Triplet x64-windows-static `
  -CleanAfterBuild

.\scripts\build.ps1 `
  -Wx ON `
  -RequireWx ON `
  -VcpkgRoot C:\vcpkg `
  -VcpkgTriplet x64-windows-static `
  -Parallel ([Environment]::ProcessorCount)
```

Use `-Wx OFF` on Windows for a CLI/core-only build. The default build directory is `build/`.

## Supported TLK families and text encodings

NeoTLK opens, edits, and saves these native TLK families without converting the file to another layout:

- Classic BioWare `TLK V3.0` files, including dense StrRef tables with classic sound metadata.
- Jade Empire `TLK V4.0` files, whose compact records use numeric sound IDs.
- Dragon Age `GFF V4.0 / TLK V0.2` files, whose string IDs are sparse and whose records contain text rather than classic TLK sound/language metadata.

All text is normalized to UTF-8 while it is being edited. Classic V3.0 files are decoded per entry as UTF-8, Windows-1252, or Windows-1250, including mixed-encoding tables encountered in the field. On save, NeoTLK writes each classic entry back using its detected original encoding. An edit containing a character that cannot be represented by that entry's original Windows code page is rejected rather than silently replaced or corrupted.

For Dragon Age V0.2 files, NeoTLK preserves sparse string IDs and the native GFF-backed TLK structure. Language and sound controls that are not represented by that format are disabled. Adding a record prompts for its string ID, and deleting one does not renumber the remaining records.

The open dialog accepts both `.tlk` and uppercase `.TLK` filenames.

## Tabular import/export

`neotlk-cli` supports CSV, TSV, XML, and JSON export/import for TLK entries. CSV and TSV are table-style interchange paths and carry `StorageFormat`, `LanguageId`, and per-entry `TextEncoding` metadata alongside the text and sound fields. JSON and XML are complete semantic documents: they retain the native TLK family, language where applicable, sparse StrRefs, flags, text encoding, and all sound metadata supported by that family. XML uses `<tlk storageFormat="..." language="...">` with complete `<string ...>` children.

CSV/TSV imports merge rows by StrRef into the current/native table, while XML/JSON imports replace the complete semantic document. Structured XML/JSON export is never filtered, because a partial file could be mistaken for a complete importable TLK. Use CSV or TSV to export the currently visible filtered rows. A Dragon Age GFF-backed TLK can be imported into an already-open native Dragon Age TLK, preserving its backing structure, but NeoTLK will not synthesize that native GFF structure from a standalone interchange document.

The GUI provides matching **Import** and **Export** menus, filter/search terms, and copy/paste of entry text or pasted table rows.

## TSLPatcher/HoloPatcher output

NeoTLK can generate a `tslpatchdata` package directly from the active in-memory TLK and a clean original baseline. In the GUI, use:

```text
Export -> Export TSLPatcher Package...
Export -> Export HoloPatcher Package...
```

The stock TSLPatcher mode is append-only. New entries after the original TLK count are written to `append.tlk`, and `changes.ini` receives matching `StrRefN=N` entries in `[TLKList]`. Existing-entry edits, deletions, language changes, and truncation are rejected because stock TSLPatcher cannot merge those operations safely.

The HoloPatcher mode also supports edits to existing StrRefs. Those entries are written to `replace.tlk`, with `ReplaceFile0=replace.tlk` in `[TLKList]` and an accompanying `[replace.tlk]` map from destination StrRef to replacement-file index. This replacement extension is HoloPatcher-specific; a package containing `replace.tlk` is not compatible with the original TSLPatcher executable. Appended entries still use the stock-compatible `append.tlk` path.

Equivalent CLI commands are:

```sh
# Stock TSLPatcher-compatible append-only package
neotlk-cli diff-tslpatcher dialog_original.tlk dialog_modified.tlk tslpatchdata --tslpatcher

# HoloPatcher package, including existing-entry replacements
neotlk-cli diff-tslpatcher dialog_original.tlk dialog_modified.tlk tslpatchdata --holopatcher
```

NeoTLK deliberately does not expose an INI-only fragment mode: a usable TLK patch requires the generated `append.tlk` and/or `replace.tlk` payloads alongside `changes.ini`.

Patcher generation also accepts imported modified-side data through `--modified-format csv|tsv|xml|json|tlk|kotor|native|auto` or `diff-tslpatcher-import`. Imported tables, XML, or JSON are applied over the original TLK before the patch project is generated.

Each export writes:

- `changes.ini`;
- `append.tlk` when new entries were appended;
- `replace.tlk` when HoloPatcher mode found edits to existing StrRefs.

The generator preserves the per-entry text encoding selected by NeoTLK, including Windows-1250, Windows-1252, mixed-encoding files, and UTF-8 entries. It also refuses to overwrite either comparison input when a source TLK is itself named `append.tlk`, `replace.tlk`, or `changes.ini` inside the selected output location.

This workflow is intentionally limited to KotOR-style classic `TLK V3.0` files. Jade Empire `TLK V4.0` and Dragon Age GFF-backed TLKs do not use the KotOR `append.tlk` patching model and are rejected with an explicit diagnostic.

Interchange fields are native-family aware: Jade TLK V4 stores text plus numeric `SoundId`, while Dragon Age TLKs store sparse IDs and Unicode text. NeoTLK rejects imported values for fields the selected native family cannot store instead of silently discarding them.
