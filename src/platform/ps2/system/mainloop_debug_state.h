#pragma once

#include "types.h"

#if AURORA_SNES_DEBUG_STATE
/* AURORA_SNES_DEBUG_STATE_V34_FINAL_20261002
 * Manual, menu-only capture. No per-frame hooks or normal-state format changes. */
Bool MainLoopSaveSnesDebugState(void);

/* AURORA_SNES_DEBUG_STATE_V35_UI_FEEDBACK_20261002
 * Menu-only deferred writer. Request -> one visible menu frame -> write. */
Bool MainLoopRequestSnesDebugState(void);
void MainLoopSnesDebugStateUpdate(void);
void MainLoopCancelSnesDebugStateRequest(void);
#endif
