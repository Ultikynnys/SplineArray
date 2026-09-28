// Copyright (c) 2026. MIT License.

#include "SplineArrayMeshTools.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

namespace
{
	struct FMeshCutCorner
	{
		FVector3d Position = FVector3d::ZeroVector;
		TArray<FVector2f> UVs;
		int32 SourceVertex = INDEX_NONE;
	};

	void ClipPolygon(TArray<FMeshCutCorner>& Polygon, int32 AxisIndex, double Plane, bool bKeepGreater)
	{
		if (Polygon.Num() == 0)
		{
			return;
		}

		TArray<FMeshCutCorner> Clipped;
		Clipped.Reserve(Polygon.Num() + 1);
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FMeshCutCorner& A = Polygon[Index];
			const FMeshCutCorner& B = Polygon[(Index + 1) % Polygon.Num()];
			const double DistanceA = A.Position[AxisIndex] - Plane;
			const double DistanceB = B.Position[AxisIndex] - Plane;
			const bool bInsideA = bKeepGreater ? (DistanceA >= 0.0) : (DistanceA <= 0.0);
			const bool bInsideB = bKeepGreater ? (DistanceB >= 0.0) : (DistanceB <= 0.0);

			if (bInsideA)
			{
				Clipped.Add(A);
			}
			if (bInsideA != bInsideB)
			{
				const double T = DistanceA / (DistanceA - DistanceB);
				FMeshCutCorner C;
				C.Position = A.Position + T * (B.Position - A.Position);
				C.SourceVertex = INDEX_NONE;
				C.UVs.SetNum(A.UVs.Num());
				for (int32 Layer = 0; Layer < C.UVs.Num(); ++Layer)
				{
					C.UVs[Layer] = A.UVs[Layer] + static_cast<float>(T) * (B.UVs[Layer] - A.UVs[Layer]);
				}
				Clipped.Add(C);
			}
		}
		Polygon = MoveTemp(Clipped);
	}
}

UStaticMesh* SplineArrayMeshTools::BisectMesh(UStaticMesh* SourceMesh, int32 AxisIndex, float Start, float Length, TMap<FString, TObjectPtr<UStaticMesh>>& Cache)
{
#if WITH_EDITOR
	if (!SourceMesh || Length <= KINDA_SMALL_NUMBER)
	{
		return SourceMesh;
	}

	const FBox Bounds = SourceMesh->GetBoundingBox();
	const double AxisMin = Bounds.Min[AxisIndex];
	const double AxisMax = Bounds.Max[AxisIndex];
	if (AxisMax - AxisMin <= KINDA_SMALL_NUMBER)
	{
		return SourceMesh;
	}

	const double Lower = FMath::Max<double>(AxisMin, Start);
	const double Upper = FMath::Min<double>(AxisMax, Start + Length);
	const bool bCoversWhole = Lower <= AxisMin + KINDA_SMALL_NUMBER && Upper >= AxisMax - KINDA_SMALL_NUMBER;
	if (Upper - Lower <= KINDA_SMALL_NUMBER || bCoversWhole)
	{
		return SourceMesh;
	}

	const FString Key = FString::Printf(TEXT("%s|%d|%.3f|%.3f"), *SourceMesh->GetPathName(), AxisIndex, Lower, Upper);
	if (const TObjectPtr<UStaticMesh>* Existing = Cache.Find(Key))
	{
		if (UStaticMesh* Cached = Existing->Get())
		{
			return Cached;
		}
	}

	const FMeshDescription* SourceDescription = SourceMesh->GetMeshDescription(0);
	if (!SourceDescription)
	{
		return SourceMesh;
	}

	UE::Geometry::FDynamicMesh3 Source;
	{
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(SourceDescription, Source);
	}
	if (!Source.HasAttributes())
	{
		Source.EnableAttributes();
	}

	TArray<const UE::Geometry::FDynamicMeshUVOverlay*> SourceUVLayers;
	if (Source.Attributes())
	{
		const int32 SourceUVLayerCount = Source.Attributes()->NumUVLayers();
		SourceUVLayers.Reserve(SourceUVLayerCount);
		for (int32 Layer = 0; Layer < SourceUVLayerCount; ++Layer)
		{
			SourceUVLayers.Add(Source.Attributes()->GetUVLayer(Layer));
		}
	}

	UE::Geometry::FDynamicMesh3 Result;
	Result.EnableAttributes();
	Result.EnableTriangleGroups();
	UE::Geometry::FDynamicMeshAttributeSet* ResultAttributes = Result.Attributes();
	TArray<UE::Geometry::FDynamicMeshUVOverlay*> ResultUVLayers;
	if (ResultAttributes && SourceUVLayers.Num() > 0)
	{
		ResultAttributes->SetNumUVLayers(SourceUVLayers.Num());
		ResultUVLayers.Reserve(SourceUVLayers.Num());
		for (int32 Layer = 0; Layer < SourceUVLayers.Num(); ++Layer)
		{
			ResultUVLayers.Add(ResultAttributes->GetUVLayer(Layer));
		}
	}

	TMap<int32, int32> VertexMap;
	TArray<FMeshCutCorner> Polygon;
	TArray<int32> PolygonVertices;

	for (const int32 TriangleID : Source.TriangleIndicesItr())
	{
		const UE::Geometry::FIndex3i SourceTriangle = Source.GetTriangle(TriangleID);
		const int32 SourceCorners[3] = { SourceTriangle.A, SourceTriangle.B, SourceTriangle.C };
		const int32 NumLayers = SourceUVLayers.Num();

		int32 InsideCount = 0;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const double Coordinate = Source.GetVertex(SourceCorners[Corner])[AxisIndex];
			if (Coordinate >= Lower && Coordinate <= Upper)
			{
				++InsideCount;
			}
		}
		if (InsideCount == 0)
		{
			continue;
		}

		Polygon.Reset();
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			FMeshCutCorner CutCorner;
			CutCorner.Position = Source.GetVertex(SourceCorners[Corner]);
			CutCorner.SourceVertex = SourceCorners[Corner];
			CutCorner.UVs.SetNum(NumLayers);
			for (int32 Layer = 0; Layer < NumLayers; ++Layer)
			{
				const UE::Geometry::FIndex3i TriangleUV = SourceUVLayers[Layer]->GetTriangle(TriangleID);
				const int32 ElementIndex = Corner == 0 ? TriangleUV.A : Corner == 1 ? TriangleUV.B : TriangleUV.C;
				CutCorner.UVs[Layer] = ElementIndex >= 0 ? SourceUVLayers[Layer]->GetElement(ElementIndex) : FVector2f::ZeroVector;
			}
			Polygon.Add(CutCorner);
		}

		ClipPolygon(Polygon, AxisIndex, Lower, true);
		ClipPolygon(Polygon, AxisIndex, Upper, false);
		if (Polygon.Num() < 3)
		{
			continue;
		}

		PolygonVertices.Reset();
		for (const FMeshCutCorner& CutCorner : Polygon)
		{
			int32 VertexID = INDEX_NONE;
			if (CutCorner.SourceVertex != INDEX_NONE)
			{
				if (int32* Found = VertexMap.Find(CutCorner.SourceVertex))
				{
					VertexID = *Found;
				}
				else
				{
					VertexID = Result.AppendVertex(CutCorner.Position);
					VertexMap.Add(CutCorner.SourceVertex, VertexID);
				}
			}
			else
			{
				VertexID = Result.AppendVertex(CutCorner.Position);
			}
			PolygonVertices.Add(VertexID);
		}

		for (int32 Fan = 1; Fan + 1 < Polygon.Num(); ++Fan)
		{
			const int32 NewTriangleID = Result.AppendTriangle(
				UE::Geometry::FIndex3i(PolygonVertices[0], PolygonVertices[Fan], PolygonVertices[Fan + 1]), 0);
			if (NewTriangleID < 0)
			{
				continue;
			}
			for (int32 Layer = 0; Layer < ResultUVLayers.Num(); ++Layer)
			{
				UE::Geometry::FDynamicMeshUVOverlay* ResultUV = ResultUVLayers[Layer];
				const int32 E0 = ResultUV->AppendElement(Polygon[0].UVs[Layer]);
				const int32 E1 = ResultUV->AppendElement(Polygon[Fan].UVs[Layer]);
				const int32 E2 = ResultUV->AppendElement(Polygon[Fan + 1].UVs[Layer]);
				ResultUV->SetTriangle(NewTriangleID, UE::Geometry::FIndex3i(E0, E1, E2));
			}
		}
	}

	if (Result.TriangleCount() == 0)
	{
		return SourceMesh;
	}

	if (ResultAttributes && ResultAttributes->PrimaryNormals())
	{
		UE::Geometry::FMeshNormals Normals(&Result);
		Normals.ComputeVertexNormals();
		Normals.CopyToOverlay(ResultAttributes->PrimaryNormals());
	}

	FMeshDescription ResultDescription;
	FStaticMeshAttributes ResultStaticAttributes(ResultDescription);
	ResultStaticAttributes.Register();
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Result, ResultDescription);
	}

	UStaticMesh* GeneratedMesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	GeneratedMesh->SetFlags(RF_Transient);

	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&ResultDescription);
	GeneratedMesh->BuildFromMeshDescriptions(Descriptions);

	Cache.Add(Key, GeneratedMesh);
	return GeneratedMesh;
#else
	return SourceMesh;
#endif
}
