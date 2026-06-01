#include "UnrealMcpUserToolListVersion.h"

namespace UnrealMcp
{
	static std::atomic<uint64> GUserToolListVersion { 1 };

	uint64 GetUserToolListVersion()
	{
		return GUserToolListVersion.load(std::memory_order_acquire);
	}

	uint64 BumpUserToolListVersion()
	{
		return GUserToolListVersion.fetch_add(1, std::memory_order_acq_rel) + 1;
	}
}
