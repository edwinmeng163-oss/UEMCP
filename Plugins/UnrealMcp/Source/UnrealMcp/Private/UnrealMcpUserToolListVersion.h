#pragma once

#include "CoreTypes.h"

#include <atomic>

namespace UnrealMcp
{
	/** Monotonic counter bumped whenever the user-tool registry surface mutates.
	 *  External MCP clients can compare versions across tools/list,
	 *  task_atlas_list_made_tools, and user_registry_introspect because the
	 *  transport does not emit notifications/tools/list_changed.
	 */
	uint64 GetUserToolListVersion();
	uint64 BumpUserToolListVersion();
}
