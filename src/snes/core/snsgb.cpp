/* AURORA_NO_SGB_ELF_V4_20260921
 * No SGB implementation is linked. These two no-op diagnostics satisfy old
 * SnesSystem references without owning state or pulling GBHost/ICD2 into ELF. */
#include "types.h"

extern "C" void AuroraSgbBootTrace(const char *pText)
{
    (void)pText;
}

extern "C" Bool AuroraSgbDebugFBConsumed(void)
{
    return FALSE;
}
