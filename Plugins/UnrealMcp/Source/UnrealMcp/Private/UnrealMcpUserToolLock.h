// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

namespace UnrealMcp::UserToolLock
{
	// Single-writer / multiple-reader lock guarding user tool registry + execution.
	// Implementation: FRWLock (UE's reader-writer lock primitive).
	//
	// Concurrency rules (v0.26 single-thread editor, but defensive):
	//   - Reload requires EXCLUSIVE lock (writer): no execution / no other reload in progress
	//   - Execute requires SHARED lock (reader): other executions OK in theory, but
	//     v0.26 BLOCKS same-tool concurrent execution via separate per-tool guard
	//     (see SerializeSameToolExecution)
	//
	// Tests must verify:
	//   - reload while idle: succeeds
	//   - reload while execute in progress: blocks until execute returns
	//   - execute while reload in progress: blocks until reload completes
	//   - same-tool concurrent execute: serialized (second waits)

	UNREALMCP_API void AcquireExclusive();
	UNREALMCP_API void ReleaseExclusive();
	UNREALMCP_API void AcquireShared();
	UNREALMCP_API void ReleaseShared();

	UNREALMCP_API bool TryAcquireExclusive(double TimeoutSeconds);
	UNREALMCP_API bool TryAcquireShared(double TimeoutSeconds);

	UNREALMCP_API bool SerializeSameToolExecution(const FString& ToolName, double TimeoutSeconds);
	UNREALMCP_API void ReleaseSameToolExecution(const FString& ToolName);

	struct FExclusiveGuard
	{
		FExclusiveGuard() { AcquireExclusive(); }
		~FExclusiveGuard() { ReleaseExclusive(); }
	};

	struct FSharedGuard
	{
		FSharedGuard() { AcquireShared(); }
		~FSharedGuard() { ReleaseShared(); }
	};
}
