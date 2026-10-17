// Copyright (c) 2026. MIT License.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "SplineArrayActor.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplineArrayRepeatRebuildTest,
	"SplineArray.Rebuild.RepeatedForceRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSplineArrayRepeatRebuildTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Source cube"), Mesh))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	ASplineArrayActor* Actor = World->SpawnActor<ASplineArrayActor>();
	if (!TestNotNull(TEXT("Spline actor"), Actor))
	{
		World->DestroyWorld(false);
		return false;
	}
	FObjectPropertyBase* MeshProperty = FindFProperty<FObjectPropertyBase>(ASplineArrayActor::StaticClass(), TEXT("SourceMesh"));
	if (!TestNotNull(TEXT("Source mesh property"), MeshProperty))
	{
		World->DestroyWorld(false);
		return false;
	}
	MeshProperty->SetObjectPropertyValue_InContainer(Actor, Mesh);
	USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
	Spline->SetSplinePoints({FVector::ZeroVector, FVector(100.0, 0.0, 0.0)}, ESplineCoordinateSpace::Local);
	Actor->ForceRebuild();

	for (const double Length : {1000.0, 550.0, 1000.0})
	{
		Spline->SetLocationAtSplinePoint(1, FVector(Length, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
		for (int32 RebuildIndex = 0; RebuildIndex < 3; ++RebuildIndex)
		{
			Actor->ForceRebuild();
			TInlineComponentArray<USplineMeshComponent*> Segments(Actor);
			TestEqual(TEXT("Segment count covers the edited spline"), Segments.Num(), FMath::CeilToInt32(Length / 100.0));
			TestTrue(TEXT("Distance table reflects edited points"), FMath::IsNearlyEqual(static_cast<double>(Spline->GetSplineLength()), Length, 0.01));
			double CoveredLength = 0.0;
			bool bReachesEnd = false;
			for (USplineMeshComponent* Segment : Segments)
			{
				TestTrue(TEXT("Segment has a valid mesh"), IsValid(Segment->GetStaticMesh()));
				const FVector Start = Segment->GetStartPosition();
				const FVector End = Segment->GetEndPosition();
				CoveredLength += FVector::Distance(Start, End);
				bReachesEnd |= End.Equals(FVector(Length, 0.0, 0.0), 0.01);
			}
			TestTrue(TEXT("Segments cover the full length"), FMath::IsNearlyEqual(CoveredLength, Length, 0.01));
			TestTrue(TEXT("Final segment reaches the spline end"), bReachesEnd);
		}
	}

	World->DestroyWorld(false);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplineArrayCutNormalsTest,
	"SplineArray.Rebuild.CutMeshPreservesHardEdgeNormals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSplineArrayCutNormalsTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Source cube"), Mesh))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	ASplineArrayActor* Actor = World->SpawnActor<ASplineArrayActor>();
	if (!TestNotNull(TEXT("Spline actor"), Actor))
	{
		World->DestroyWorld(false);
		return false;
	}
	FObjectPropertyBase* MeshProperty = FindFProperty<FObjectPropertyBase>(ASplineArrayActor::StaticClass(), TEXT("SourceMesh"));
	MeshProperty->SetObjectPropertyValue_InContainer(Actor, Mesh);
	USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
	// 250 cm over the 100 cm cube length leaves a shortened final segment, which is the bisected (cut) case.
	Spline->SetSplinePoints({FVector::ZeroVector, FVector(250.0, 0.0, 0.0)}, ESplineCoordinateSpace::Local);
	Actor->ForceRebuild();

	TInlineComponentArray<USplineMeshComponent*> Segments(Actor);
	int32 CutMeshCount = 0;
	int32 NonAxisAlignedNormals = 0;
	for (USplineMeshComponent* Segment : Segments)
	{
		UStaticMesh* SegmentMesh = Segment->GetStaticMesh();
		if (!SegmentMesh || SegmentMesh == Mesh)
		{
			continue; // whole-mesh copy: uses the source mesh and its untouched normals
		}
		TestFalse(TEXT("Cut mesh build settings keep the authored normals"), SegmentMesh->GetSourceModel(0).BuildSettings.bRecomputeNormals);
		const FMeshDescription* Description = SegmentMesh->GetMeshDescription(0);
		if (!Description)
		{
			continue;
		}
		++CutMeshCount;
		FStaticMeshConstAttributes Attributes(*Description);
		TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		for (const FVertexInstanceID VertexInstanceID : Description->VertexInstances().GetElementIDs())
		{
			const FVector3f Normal = Normals.Get(VertexInstanceID);
			const float MaxAbs = FMath::Max3(FMath::Abs(Normal.X), FMath::Abs(Normal.Y), FMath::Abs(Normal.Z));
			const float SumAbs = FMath::Abs(Normal.X) + FMath::Abs(Normal.Y) + FMath::Abs(Normal.Z);
			// A box face normal is axis aligned (one component ~1, the rest ~0); averaging across a
			// shared corner would rotate it toward the diagonal and trip this check.
			if (!(MaxAbs > 0.99f && SumAbs < 1.01f))
			{
				++NonAxisAlignedNormals;
			}
		}
	}

	TestTrue(TEXT("A cut mesh was generated for the shortened segment"), CutMeshCount > 0);
	TestEqual(TEXT("Cut-mesh normals stay axis-aligned (hard edges preserved)"), NonAxisAlignedNormals, 0);

	World->DestroyWorld(false);
	return !HasAnyErrors();
}

#endif
