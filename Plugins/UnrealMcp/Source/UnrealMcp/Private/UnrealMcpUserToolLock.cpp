// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpUserToolLock.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace UnrealMcp::UserToolLock
{
	namespace
	{
		struct FPerToolGuard
		{
			FCriticalSection CriticalSection;
			uint32 OwnerThreadId = 0;
		};

		FRWLock GUserToolRWLock;
		FCriticalSection GPerToolGuardsLock;
		TMap<FString, FPerToolGuard*> GPerToolGuards;

		bool UserToolLockShouldKeepTrying(double StartSeconds, double TimeoutSeconds)
		{
			return TimeoutSeconds < 0.0 || (FPlatformTime::Seconds() - StartSeconds) < TimeoutSeconds;
		}

		FPerToolGuard* UserToolLockFindOrCreatePerToolGuard(const FString& ToolName)
		{
			FScopeLock MapLock(&GPerToolGuardsLock);
			if (FPerToolGuard** Existing = GPerToolGuards.Find(ToolName))
			{
				return *Existing;
			}

			FPerToolGuard* NewGuard = new FPerToolGuard();
			GPerToolGuards.Add(ToolName, NewGuard);
			return NewGuard;
		}
	}

	void AcquireExclusive()
	{
		GUserToolRWLock.WriteLock();
	}

	void ReleaseExclusive()
	{
		GUserToolRWLock.WriteUnlock();
	}

	void AcquireShared()
	{
		GUserToolRWLock.ReadLock();
	}

	void ReleaseShared()
	{
		GUserToolRWLock.ReadUnlock();
	}

	bool TryAcquireExclusive(double TimeoutSeconds)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		do
		{
			if (GUserToolRWLock.TryWriteLock())
			{
				return true;
			}
			FPlatformProcess::SleepNoStats(0.005f);
		}
		while (UserToolLockShouldKeepTrying(StartSeconds, TimeoutSeconds));

		return false;
	}

	bool TryAcquireShared(double TimeoutSeconds)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		do
		{
			if (GUserToolRWLock.TryReadLock())
			{
				return true;
			}
			FPlatformProcess::SleepNoStats(0.005f);
		}
		while (UserToolLockShouldKeepTrying(StartSeconds, TimeoutSeconds));

		return false;
	}

	bool SerializeSameToolExecution(const FString& ToolName, double TimeoutSeconds)
	{
		FPerToolGuard* PerToolGuard = UserToolLockFindOrCreatePerToolGuard(ToolName);
		const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
		const double StartSeconds = FPlatformTime::Seconds();
		do
		{
			{
				FScopeLock MapLock(&GPerToolGuardsLock);
				if (PerToolGuard->OwnerThreadId == CurrentThreadId)
				{
					return false;
				}
			}

			if (PerToolGuard->CriticalSection.TryLock())
			{
				FScopeLock MapLock(&GPerToolGuardsLock);
				PerToolGuard->OwnerThreadId = CurrentThreadId;
				return true;
			}
			FPlatformProcess::SleepNoStats(0.005f);
		}
		while (UserToolLockShouldKeepTrying(StartSeconds, TimeoutSeconds));

		return false;
	}

	void ReleaseSameToolExecution(const FString& ToolName)
	{
		FPerToolGuard* PerToolGuard = nullptr;
		{
			FScopeLock MapLock(&GPerToolGuardsLock);
			if (FPerToolGuard** Existing = GPerToolGuards.Find(ToolName))
			{
				PerToolGuard = *Existing;
				PerToolGuard->OwnerThreadId = 0;
			}
		}

		if (PerToolGuard)
		{
			PerToolGuard->CriticalSection.Unlock();
		}
	}
}
