# Distribution

This folder holds the **shipped** module descriptor for the binary release.

In the release zip, `Source/SplineArray/SplineArray.Build.cs` is a copy of
[`SplineArray.Build.cs`](SplineArray.Build.cs) from this folder — **not** the repository's
`Source/SplineArray/SplineArray.Build.cs`.

The two differ by exactly one line:

```csharp
bUsePrecompiled = true;
```

The release carries prebuilt `Binaries/` and no source, so UnrealBuildTool must link the
precompiled module instead of rebuilding it (without the line it discards the precompiled
objects, tries to compile absent source, and leaves the module out of a packaged game —
see the README's "For plugin builders" section).

The line must **not** be added to `Source/SplineArray/SplineArray.Build.cs`: a source
checkout has no `Binaries/`, and `bUsePrecompiled` would stop UnrealBuildTool compiling the
module, so the plugin would never build from source.

`.githooks/pre-commit` enforces both halves of that contract (the shipped file sets the
line, the source file does not, and the shipped file matches the source otherwise).

## Packaging a release

1. Build the plugin for Win64, e.g.
   `RunUAT BuildPlugin -Plugin=... -Package=<out> -TargetPlatforms=Win64 -Rocket`.
2. In the packaged output, replace `Source/SplineArray/SplineArray.Build.cs` with
   [`SplineArray.Build.cs`](SplineArray.Build.cs) from this folder.
3. Delete `Binaries/Win64/*.pdb` (see [../PDB.md](../PDB.md)).
4. Zip the plugin folder and attach it to the GitHub release.
