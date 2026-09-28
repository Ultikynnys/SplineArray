# Spline Array (Unreal Engine 5 plugin)

Repeats a static mesh along a spline — the Unreal equivalent of Blender's **Array + Curve**
modifiers. Drop a mesh in, draw a spline, and the mesh is arrayed along it with alignment,
spacing, fitting and optional randomisation.

Everything is rendered through a single `UInstancedStaticMeshComponent`, so large counts
are cheap.

* **Engine:** Unreal Engine 5.6 (code plugin — your project must be a C++ project, or the
  editor must have C++/compiler tooling available so the plugin can build).
* **License:** MIT.

## Install

1. Copy the `SplineArray` folder into your project's `Plugins/` directory:
   ```
   <YourProject>/Plugins/SplineArray/
   ```
2. Regenerate project files and build (or just launch the editor and let it compile the plugin).
3. Enable **Spline Array** under *Edit → Plugins* if it isn't already on.

## Usage

1. Place a **Spline Array Actor** in the level (*Place Actors → All Classes → Spline Array Actor*).
2. Assign **Source Mesh**.
3. Shape the **Spline** component (move/add points, or close the loop).
4. Pick a **Distribution** mode and tune the rest.

### Distribution modes

| Mode | Behaviour |
| --- | --- |
| **By Count** | Places an exact number of copies, spread evenly. |
| **By Spacing** | Places a copy every `Spacing` cm. |
| **Fit Along Spline** | Fits as many copies of `Item Length` cm as possible. |

`Gap` adds extra space between copies in the two fit/spacing modes. `Start Offset` /
`End Offset` trim the usable length at each end of the spline.

### Mapping to Blender

| Blender | This plugin |
| --- | --- |
| Array modifier — Count | `Distribution = By Count`, `Count` |
| Array modifier — Relative/CONSTANT offset | `By Spacing`, or `bUseMeshLengthForSpacing` + `Gap` |
| Array modifier — Fit Length / Fit Curve | `Distribution = Fit Along Spline`, `Item Length` |
| Curve modifier — axis | `Forward Axis` |
| Curve modifier follows the curve | `bAlignToTangent` |
| Object transform applied to each copy | `Rotation Offset`, `Location Offset`, `Scale` |

## Properties

* **Source** — `Source Mesh`, `Forward Axis` (which mesh axis points along the spline),
  `Use Mesh Length As Spacing`.
* **Distribution** — see above.
* **Orientation** — `Align To Tangent`, `Rotation Offset`, `Location Offset`, `Scale`.
* **Randomisation** — `Randomize Yaw` (+ range), `Randomize Scale` (+ range), `Random Seed`.

The layout rebuilds automatically when the actor is constructed, when any property changes,
and (in the editor) whenever the spline geometry changes. You can also force it with the
**Rebuild** button / `Rebuild()` function.

## Extending

The intended place to add per-copy variation is `ASplineArrayActor::Rebuild()`. A common next
step is sampling a mesh **socket** or a **curve width** to offset each copy, or driving
per-instance **custom data floats** for materials.

## Support

Open an issue on the repository. Source is a single runtime module (`Source/SplineArray`).
