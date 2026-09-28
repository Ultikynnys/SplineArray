# Spline Array (Unreal Engine 5 plugin)

Repeats a static mesh along a spline — the Unreal equivalent of Blender's **Array + Curve**
modifiers. Drop a mesh in, draw a spline, and the mesh is arrayed along it with alignment,
spacing, fitting and optional randomisation.

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
2. Assign **Source Mesh**.
3. Shape the **Spline** component (move/add points, or close the loop).
4. Pick a **Distribution** mode and tune the rest.

### Distribution modes

| Mode | Behaviour |
| --- | --- |
| **End To End** (default) | Tiles the mesh's measured axis length; `Length Offset` adds ±5% gap/overlap. |
| **Fit Along Spline** | Tiles the measured mesh length with an additional `Gap`. |
| **By Count** | Spreads the chosen number of full-length copies over the spline. |
| **By Spacing** | Uses a manual start-to-start spacing (`Spacing` + `Gap`). |

The final copy is trimmed to end at the spline endpoint. `Gap` adds extra space in the spacing/fit modes. `Start Offset` /
`End Offset` trim the usable length at each end of the spline.

### Mapping to Blender

| Blender | This plugin |
| --- | --- |
| Array modifier — Count | `Distribution = By Count`, `Count` |
| Array modifier — Relative/CONSTANT offset | `End To End` + `Length Offset`, or `By Spacing` |
| Array modifier — Fit Length / Fit Curve | `End To End`, or `Fit Along Spline` + `Gap` |
| Curve modifier — axis | `Forward Axis` |
| Curve modifier bends copies along the path | Spline mesh deformation |
| Object transform applied to each copy | `Rotation Offset`, `Location Offset`, `Scale` |

## Properties

* **Source** — `Source Mesh`, `Forward Axis`, read-only `Mesh Axis Length`.
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
