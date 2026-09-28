# Spline Array (Unreal Engine 5 plugin)

Repeats a static mesh along a spline: the Unreal equivalent of Blender's **Array + Curve**
modifiers. Select a mesh, edit the single Spline component, and copies bend along it.

Each copy is a `USplineMeshComponent`, deformed between two distances on the spline.
The mesh bounding-box length along `Forward Axis` determines the default interval.

* **Engine:** Unreal Engine 5.6.1. Prebuilt Win64 editor binaries are included; source builds
  require a C++ toolchain. Binary installations need only the `.uplugin` and `Binaries/`.
* **License:** MIT.

## Install

1. Copy the `SplineArray` folder into your project's `Plugins/` directory:
   ```
   <YourProject>/Plugins/SplineArray/
   ```
2. For the included Win64 editor build, launch UE 5.6.1. For a source build, compile the plugin using your own C++ toolchain.
3. Enable **Spline Array** under *Edit → Plugins* if it isn't already on.

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
