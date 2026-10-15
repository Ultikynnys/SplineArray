// Copyright (c) 2026. MIT License.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "SplineArrayActor.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

#endif
