#pragma once

#include "CoreMinimal.h"

namespace UnrealMcp::HashUtils
{
	UNREALMCP_API FString Sha256LowerHex(const TArray<uint8>& Bytes);
	UNREALMCP_API FString Sha256LowerHexFromUtf8(const FString& Text);
}
