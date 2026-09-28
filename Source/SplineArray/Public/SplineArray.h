// Copyright (c) 2026. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for the SplineArray plugin.
 * The actual logic lives in ASplineArrayActor; this module only registers itself.
 */
class FSplineArrayModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
