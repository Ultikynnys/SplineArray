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

From source, compile the plugin with a C++ toolchain. To avoid needing a toolchain, download the
release zip and drop its `SplineArray` folder into the same location; the precompiled editor
binaries make the plugin load in the editor immediately.

### Packaging a game

A blueprint-only project cannot include this plugin in a packaged build. UE compiles and links a
C++ plugin's runtime module into the game only when the project itself has C++ source, so on a
blueprint-only project the editor loads the plugin but the packaged game reports
`module SplineArray could not be found`.

To package a game with Spline Array, one of these must hold:

* The project is a C++ project, so UE builds and links the plugin module into the game. Adding any C++ class converts a blueprint-only project to a C++ project, and this needs a C++ toolchain.
* The plugin is installed under the engine instead of the project, for example `Engine/Plugins/Marketplace/SplineArray`, so the engine includes the module in its game target. This needs write access to the engine directory.

No plugin-side setting changes this; it is how UE builds the game target.

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
