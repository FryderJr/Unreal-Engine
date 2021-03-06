// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Utilities.h"
#include "Chaos/Matrix.h"

namespace Chaos
{
	// @todo(ccaulfield): should be in ChaosCore, but that can't actually include its own headers at the moment (e.g., Matrix.h includes headers from Chaos)
	const FMatrix33 FMatrix33::Zero = FMatrix33(0, 0, 0);
	const FMatrix33 FMatrix33::Identity = FMatrix33(1, 1, 1);
}
