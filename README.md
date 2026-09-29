# Spline Array (Unreal Engine 5 plugin)

Repeats a static mesh along a spline: the Unreal equivalent of Blender's **Array + Curve**
modifiers. Select a mesh, edit the single Spline component, and copies bend along it.

Each copy is a `USplineMeshComponent`, deformed between two distances on the spline.
The mesh bounding-box length along `Forward Axis` determines the default interval.

* **Engine:** Unreal Engine 5.6.1 (Win64).
* **Distribution:** this repository is source only. A precompiled Win64 build is published on the
  Releases page for users without a C++ toolchain.
* **License:** MIT.

## Install

Copy the `SplineArray` folder into your project's `Plugins/` directory, then enable **Spline Array** under *Edit → Plugins*:

```
<YourProject>/Plugins/SplineArray/
```

There are two ways to get it there, and the right one depends on whether the machine has a C++ toolchain:

* **Precompiled (no toolchain).** Download the release zip and copy its `SplineArray` folder to the path above. The bundled `Binaries/` make the plugin load in the editor immediately.
* **From source (needs a toolchain).** Clone this repository to the path above. Opening the project compiles the plugin, so this requires a C++ toolchain (Visual Studio Build Tools).

Do not hand a source clone to someone without a toolchain: opening the project triggers a compile that will fail.

### Who needs what

A C++ toolchain is only ever needed to compile and link code. It is never needed to open the editor or to run a packaged game.

| Who | Editor and Play-In-Editor | Package a game |
| --- | --- | --- |
| Level designer, artist, other non-programmers (no toolchain) | Yes, with the precompiled release | No |
| Programmer or builder (with toolchain) | Yes | Yes, from a C++ project or an engine install |
| Player of a packaged game | n/a | Runs it, needs nothing |

So a mixed team works like this: the non-technical users install the precompiled release and use the plugin in the editor and PIE with no toolchain, and one programmer with a toolchain packages the game.

### Packaging a game

A blueprint-only project cannot include this plugin in a packaged build. UE compiles and links a
C++ plugin's runtime module into the game only when the project itself has C++ source, so on a
blueprint-only project the editor loads the plugin but the packaged game reports
`module SplineArray could not be found`.

To package a game with Spline Array, one of these must hold:

* The project is a C++ project, so UE builds and links the plugin module into the game. Adding any C++ class converts a blueprint-only project to a C++ project. Whoever does this needs a C++ toolchain.
* The plugin is installed under the engine instead of the project, for example `Engine/Plugins/Marketplace/SplineArray`, so the engine includes the module in its game target. This needs write access to the engine directory, and the engine's game binaries still have to be relinked to include the plugin.

Neither route removes the toolchain requirement from whoever packages; they differ only in where the plugin lives.

### For plugin builders (binary distribution)

A precompiled plugin is only linked into a game when UnrealBuildTool is told to use the precompiled binaries instead of rebuilding. In the shipped module's `Build.cs`:

```csharp
bUsePrecompiled = true;
```

Without it, UnrealBuildTool discards the precompiled objects and tries to build from source, which fails when the source is absent, and the module is silently left out of the game. Related pieces:

* `PrecompileForTargets = PrecompileTargetsType.Any;` is the build-time switch that produces the game artifacts in the first place.
* Keep `Intermediate/Build/Win64/x64/UnrealGame/` (the game-side `.obj` plus the `.precompiled` manifest). Delete it and the plugin still works in the editor, but packaging fails.
* `"Installed": true` in the `.uplugin` marks the plugin as installed and prebuilt, so the engine does not compile it from source and treats it as not authored in the project. On its own it does not put the module into the game.

The published releases since 1.0.2 ship with `bUsePrecompiled = true`, so a C++ project links the precompiled module directly. This line was missing from release 1.0.1.

## Usage

1. Place a **Spline Array Actor** in the level (*Place Actors → All Classes → Spline Array Actor*).
2. Set **Mesh**, **Axis** (+X/+Y/+Z or -X/-Y/-Z), **Mesh Scale**, **Material**, and **Axis Offset (%)** in the actor's **Spline Array** category.
3. Select the **Spline** component to edit its points. Generated meshes rebuild automatically.

### Fit along spline

The actor fits repeated, spline-deformed copies using the source mesh's length along the selected axis. Choose a negative axis to reverse the mesh along the curve. `Axis Offset (%)` adjusts each start-to-start step by -5% to +5% of the mesh's scaled axis length: negative values overlap successive segments, positive values leave a gap. The final copy is trimmed to the spline endpoint.

### Mapping to Blender

| Blender | This plugin |
| --- | --- |
| Array modifier (Relative offset) | `Axis Offset (%)` |
| Array modifier (Fit Curve) | Automatic mesh-length tiling |
| Curve modifier (deform axis) | `Axis` (+ or -) |
| Curve modifier (curve path) | `Spline` component |

## Properties

The actor's **Spline Array** category has five controls: Mesh, Axis, Mesh Scale, Material, and Axis Offset (%). Leave Material empty to use the source mesh's original materials; assigning a material overrides every material slot on the generated copies. The layout rebuilds when properties or spline points change. Generated mesh components and saved legacy properties are not shown in the actor Details controls.

## Extending

The intended place to add per-copy variation is `ASplineArrayActor::Rebuild()`. A common next
step is sampling a mesh **socket** or a **curve width** to offset each copy, or driving
per-instance **custom data floats** for materials.

## Support

Open an issue on the repository. Source is a single runtime module (`Source/SplineArray`).
