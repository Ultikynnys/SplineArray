// Copyright (c) 2026. MIT License.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/**
 * Self-contained mesh manipulation used by the spline array. No external plugin
 * dependencies: the bisection is built from the engine's own mesh description and
 * dynamic-mesh conversion modules.
 */
namespace SplineArrayMeshTools
{
	/**
	 * Returns a static mesh that is SourceMesh clipped to the slab
	 * [Start, Start + Length] centimetres along its local AxisIndex.
	 *
	 * When the slab already spans the whole mesh, SourceMesh is returned unchanged.
	 * Generated meshes are cached in Cache (keyed by mesh + slab) so repeated
	 * rebuilds reuse them.
	 */
	UStaticMesh* BisectMesh(UStaticMesh* SourceMesh, int32 AxisIndex, float Start, float Length, TMap<FString, TObjectPtr<UStaticMesh>>& Cache);
}
