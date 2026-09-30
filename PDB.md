# PDB (debug symbols) — not a build requirement

**TL;DR.** `UnrealEditor-SplineArray.pdb` is a Microsoft debug-symbols file. It is
debug-only: it is **not** needed to build, load, run, or package a project that uses
this plugin, and the published releases intentionally omit it.

## What a `.pdb` is

When the linker produces `UnrealEditor-SplineArray.dll` it writes a matching
`UnrealEditor-SplineArray.pdb` that maps machine code back to source: file/line info,
symbol names, locals, call-stack data. A debugger pairs a `.pdb` with a `.dll` by a GUID
carried in the DLL's PE debug directory (the `RSDS` record) and in the `.pdb` header.
The engine never loads it; it only affects debugging.

## Why releases omit it

- **Size:** the editor module's `.pdb` is ~59 MB, against a ~15 MB zip for the whole plugin.
- **Use:** only needed to step through the plugin in a debugger.
- **Convention:** binary-only plugin distribution strips `*.pdb` from `Binaries/` for these reasons.

## It is not a build requirement

A precompiled module's target receipt (`<Project>/Binaries/Win64/UnrealEditor-<Target>.target`)
lists the module's products, including:

```json
{
    "Path": "$(ProjectDir)/Plugins/SplineArray/Binaries/Win64/UnrealEditor-SplineArray.pdb",
    "Type": "SymbolFile"
}
```

That entry is UnrealBuildTool recording the module's expected products from the module
rules; it is **not** an input the build validates, and its absence does not fail a build.

**Verified** on a real C++ project with only the pdb-less release installed:

| Command | Result |
| --- | --- |
| `UnrealBuildTool <EditorTarget> Win64 Development` | Succeeded |
| `RunUAT BuildCookRun -build -cook -stage -pak` | BUILD SUCCESSFUL |

The cooked and staged game linked the plugin (monolithic) and shipped with no `.pdb`
present and no error.

## If a build reports a missing `.pdb`

That is a stale build state, not a real dependency. Fix the state, not the symbol file:

1. Delete the project's `Intermediate/Build/` and rebuild.
2. Make sure the plugin ships the game-side objects it needs —
   `Intermediate/Build/Win64/x64/UnrealGame/**` (the `.obj` files plus the
   `.precompiled` manifest) — alongside `Binaries/Win64/UnrealEditor-SplineArray.dll`.
3. Do **not** "fix" it by restoring an old `.pdb`. A `.pdb` from a different build has a
   mismatched GUID, so a debugger ignores it and it only masks the underlying problem.

## If you actually want symbols

Build the plugin from source (a source checkout, `bUsePrecompiled` off) so the linker
emits a `.pdb` that matches your DLL locally. A shipped `.pdb` would only ever match the
shipped binary, so this is the only way to get usable symbols for your own build.
