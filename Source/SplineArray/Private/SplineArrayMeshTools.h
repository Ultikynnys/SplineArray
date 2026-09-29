// Copyright (c) 2026. MIT License.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
struct FMeshDescription;

namespace SplineArrayMeshTools
{
	FString BisectCacheKey(UStaticMesh* SourceMesh, int32 AxisIndex, double Lower, double Upper);

	/** True when the requested slab covers the mesh's axis extent and no cut is needed; out params receive the clamped range. */
	bool IsWholeMeshBisect(UStaticMesh* SourceMesh, int32 AxisIndex, float Start, float Length, double& OutLower, double& OutUpper);

	/** Pure geometry: slices every available source LOD. Callable from worker threads. */
	TArray<TUniquePtr<FMeshDescription>> SliceMeshLODs(UStaticMesh* SourceMesh, int32 AxisIndex, double Lower, double Upper);

	/** Game thread only: creates a target mesh carrying the source's materials; pass the map package to bake it, or null for a transient mesh. */
	UStaticMesh* CreateBisectTarget(UStaticMesh* SourceMesh, UObject* Outer);

	/** Game thread only: commits the sliced LOD0 and the source's reduction settings for LODs 1..N. */
	bool PrepareBisectedMesh(UStaticMesh* Target, UStaticMesh* SourceMesh, const FMeshDescription& LOD0Description, int32 DesiredLODCount, int32 AxisIndex, double Lower, double Upper);

	/** Game thread only: batch-builds all prepared meshes, running the engine's per-mesh builds in parallel. */
	void BuildBisectedMeshes(const TArray<UStaticMesh*>& Targets);
}
