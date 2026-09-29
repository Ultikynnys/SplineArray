// Copyright (c) 2026. MIT License.

#include "SplineArrayMeshTools.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "SplineArray.h"

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

	void SliceOneLOD(UStaticMesh* SourceMesh, const FMeshDescription* SourceDescription, int32 AxisIndex, double Lower, double Upper, FMeshDescription& OutDescription)
	{
		UE::Geometry::FDynamicMesh3 Source;
		{
			FStaticMeshConstAttributes SourceAttributes(*SourceDescription);
			const auto SourceSlotNames = SourceAttributes.GetPolygonGroupMaterialSlotNames();
			TArray<int32> GroupToMaterialIndex;
			GroupToMaterialIndex.Init(0, SourceDescription->PolygonGroups().GetArraySize());
			for (const FPolygonGroupID GroupID : SourceDescription->PolygonGroups().GetElementIDs())
			{
				int32 MaterialIndex = SourceMesh->GetMaterialIndexFromImportedMaterialSlotName(SourceSlotNames[GroupID]);
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = SourceMesh->GetMaterialIndex(SourceSlotNames[GroupID]);
				}
				if (MaterialIndex == INDEX_NONE)
				{
					MaterialIndex = SourceMesh->GetStaticMaterials().IsValidIndex(GroupID.GetValue()) ? GroupID.GetValue() : 0;
				}
				GroupToMaterialIndex[GroupID.GetValue()] = MaterialIndex;
			}
			FMeshDescriptionToDynamicMesh Converter;
			Converter.SetPolygonGroupToMaterialIndexMap(GroupToMaterialIndex);
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
		ResultAttributes->EnableMaterialID();
		const UE::Geometry::FDynamicMeshMaterialAttribute* SourceMaterialIDs = Source.Attributes()->GetMaterialID();
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

			double TriangleMin = TNumericLimits<double>::Max();
			double TriangleMax = TNumericLimits<double>::Lowest();
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const double Coordinate = Source.GetVertex(SourceCorners[Corner])[AxisIndex];
				TriangleMin = FMath::Min(TriangleMin, Coordinate);
				TriangleMax = FMath::Max(TriangleMax, Coordinate);
			}
			if (TriangleMax < Lower || TriangleMin > Upper)
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
				if (SourceMaterialIDs)
				{
					int32 MaterialID = 0;
					SourceMaterialIDs->GetValue(TriangleID, &MaterialID);
					ResultAttributes->GetMaterialID()->SetValue(NewTriangleID, &MaterialID);
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

		if (ResultAttributes && ResultAttributes->PrimaryNormals())
		{
			UE::Geometry::FMeshNormals Normals(&Result);
			Normals.ComputeVertexNormals();
			Normals.CopyToOverlay(ResultAttributes->PrimaryNormals());
		}

		FStaticMeshAttributes OutStaticAttributes(OutDescription);
		OutStaticAttributes.Register();
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(&Result, OutDescription);
		}
		FStaticMeshAttributes CutAttributes(OutDescription);
		auto CutSlotNames = CutAttributes.GetPolygonGroupMaterialSlotNames();
		const TArray<FStaticMaterial>& SourceMaterials = SourceMesh->GetStaticMaterials();
		for (const FPolygonGroupID GroupID : OutDescription.PolygonGroups().GetElementIDs())
		{
			if (SourceMaterials.IsValidIndex(GroupID.GetValue()))
			{
				CutSlotNames[GroupID] = SourceMaterials[GroupID.GetValue()].MaterialSlotName;
			}
		}
	}
}

namespace SplineArrayMeshTools
{
FString BisectCacheKey(UStaticMesh* SourceMesh, int32 AxisIndex, double Lower, double Upper)
{
	return FString::Printf(TEXT("%s|%d|%.3f|%.3f"), *SourceMesh->GetPathName(), AxisIndex, Lower, Upper);
}

bool IsWholeMeshBisect(UStaticMesh* SourceMesh, int32 AxisIndex, float Start, float Length, double& OutLower, double& OutUpper)
{
	if (!SourceMesh || Length <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const FBox Bounds = SourceMesh->GetBoundingBox();
	const double AxisMin = Bounds.Min[AxisIndex];
	const double AxisMax = Bounds.Max[AxisIndex];
	if (AxisMax - AxisMin <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const double Lower = FMath::Max<double>(AxisMin, Start);
	const double Upper = FMath::Min<double>(AxisMax, Start + Length);
	if (Upper - Lower <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	OutLower = Lower;
	OutUpper = Upper;
	return Lower <= AxisMin + KINDA_SMALL_NUMBER && Upper >= AxisMax - KINDA_SMALL_NUMBER;
}

TArray<TUniquePtr<FMeshDescription>> SliceMeshLODs(UStaticMesh* SourceMesh, int32 AxisIndex, double Lower, double Upper)
{
	TArray<TUniquePtr<FMeshDescription>> CutDescriptions;
	if (!SourceMesh)
	{
		return CutDescriptions;
	}
#if !WITH_EDITOR
	return CutDescriptions;
#else

	const int32 NumLODs = SourceMesh->GetNumLODs();
	CutDescriptions.Reserve(NumLODs);
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		const FMeshDescription* SourceDescription = SourceMesh->GetMeshDescription(LODIndex);
		if (!SourceDescription && LODIndex > 0 && CutDescriptions.Num() > 0)
		{
			CutDescriptions.Emplace(MakeUnique<FMeshDescription>(*CutDescriptions.Last()));
			continue;
		}
		if (!SourceDescription)
		{
			CutDescriptions.Reset();
			return CutDescriptions;
		}

		TUniquePtr<FMeshDescription> Result = MakeUnique<FMeshDescription>();
		SliceOneLOD(SourceMesh, SourceDescription, AxisIndex, Lower, Upper, *Result);
		if (Result->Polygons().Num() == 0 && LODIndex == 0)
		{
			CutDescriptions.Reset();
			return CutDescriptions;
		}
		CutDescriptions.Add(MoveTemp(Result));
	}
	return CutDescriptions;
#endif
}

UStaticMesh* CreateBisectTarget(UStaticMesh* SourceMesh, UObject* Outer)
{
	UObject* ValidOuter = Outer ? Outer : GetTransientPackage();
	UStaticMesh* GeneratedMesh = NewObject<UStaticMesh>(ValidOuter, NAME_None, ValidOuter == GetTransientPackage() ? RF_Transient : RF_NoFlags);
	if (ValidOuter == GetTransientPackage())
	{
		GeneratedMesh->SetFlags(RF_Transient);
	}
	GeneratedMesh->GetStaticMaterials() = SourceMesh->GetStaticMaterials();
	GeneratedMesh->bAllowCPUAccess = true;
	return GeneratedMesh;
}

bool PrepareBisectedMesh(UStaticMesh* Target, UStaticMesh* SourceMesh, const FMeshDescription& LOD0Description, int32 DesiredLODCount, int32 AxisIndex, double Lower, double Upper)
{
#if WITH_EDITOR
	UStaticMesh::FCommitMeshDescriptionParams CommitParams;
	CommitParams.bMarkPackageDirty = false;

	Target->SetNumSourceModels(1);
	Target->CreateBodySetup();
	UBodySetup* TargetBody = Target->GetBodySetup();
	const UBodySetup* SourceBody = SourceMesh->GetBodySetup();
	bool bConvexComplete = SourceBody && SourceBody->AggGeom.ConvexElems.Num() > 0;
	if (bConvexComplete)
	{
		for (const FKConvexElem& SourceConvex : SourceBody->AggGeom.ConvexElems)
		{
			TArray<FPlane> Planes;
			SourceConvex.GetPlanes(Planes);
			if (Planes.Num() == 0)
			{
				bConvexComplete = false;
				break;
			}
			const FTransform ConvexTransform = SourceConvex.GetTransform();
			for (FPlane& Plane : Planes)
			{
				Plane = Plane.TransformBy(ConvexTransform.ToMatrixWithScale());
			}
			FVector Axis = FVector::ZeroVector;
			Axis[AxisIndex] = 1.0;
			Planes.Emplace(Axis * Lower, -Axis);
			Planes.Emplace(Axis * Upper, Axis);
			FKConvexElem Clipped;
			TArray<FVector> Vertices;
			Vertices.Reserve(SourceConvex.VertexData.Num());
			for (const FVector& Vertex : SourceConvex.VertexData)
			{
				Vertices.Add(ConvexTransform.TransformPosition(Vertex));
			}
			if (Clipped.HullFromPlanes(Planes, Vertices))
			{
				Clipped.UpdateElemBox();
				TargetBody->AggGeom.ConvexElems.Add(MoveTemp(Clipped));
			}
		}
	}
	if (!bConvexComplete || TargetBody->AggGeom.ConvexElems.Num() == 0)
	{
		TargetBody->AggGeom.ConvexElems.Reset();
		TargetBody->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}
	else
	{
		TargetBody->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	}
	if (SourceMesh->GetNumSourceModels() > 0)
	{
		Target->GetSourceModel(0).BuildSettings = SourceMesh->GetSourceModel(0).BuildSettings;
	}
	Target->CreateMeshDescription(0, LOD0Description);
	Target->CommitMeshDescription(0, CommitParams);

	const int32 NumExtraLODs = FMath::Min(DesiredLODCount, SourceMesh->GetNumSourceModels());
	for (int32 LODIndex = 1; LODIndex < NumExtraLODs; ++LODIndex)
	{
		FStaticMeshSourceModel& NewModel = Target->AddSourceModel();
		const FStaticMeshSourceModel& SourceModel = SourceMesh->GetSourceModel(LODIndex);
		NewModel.ReductionSettings = SourceModel.ReductionSettings;
		NewModel.ScreenSize = SourceModel.ScreenSize;
	}
	return true;
#else
	return false;
#endif
}

void BuildBisectedMeshes(const TArray<UStaticMesh*>& Targets)
{
#if WITH_EDITOR
	if (Targets.Num() == 0)
	{
		return;
	}
	UStaticMesh::BatchBuild(Targets, true);
#endif
}
}
