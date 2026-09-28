// Copyright (c) 2026. MIT License.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

namespace SplineArrayMeshTools
{
	UStaticMesh* BisectMesh(UStaticMesh* SourceMesh, int32 AxisIndex, float Start, float Length, TMap<FString, TObjectPtr<UStaticMesh>>& Cache);
}
