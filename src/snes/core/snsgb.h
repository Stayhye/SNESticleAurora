#ifndef _AURORA_SNSGB_H
#define _AURORA_SNSGB_H

#include "types.h"

/* AURORA_NO_SGB_ELF_V4_20260921
 * SGB is not part of the final Aurora ELF anymore.  This ownership-free shell
 * exists only so old SnesSystem call sites remain source-compatible while the
 * compiler/--gc-sections removes the unreachable branches.  It owns no GBHost,
 * ICD2, framebuffer, save buffer, callback pointer or dynamic allocation. */
class SNSuperGameBoy
{
public:
    enum ModelE { MODEL_SGB1 = 1, MODEL_SGB2 = 2 };

    SNSuperGameBoy() {}
    ~SNSuperGameBoy() {}

    Bool AttachGame(const Uint8 *pData, Uint32 nBytes, ModelE eModel,
                    const Uint8 *pBootRom, Uint32 nBootRomBytes)
    {
        (void)pData; (void)nBytes; (void)eModel;
        (void)pBootRom; (void)nBootRomBytes;
        return FALSE;
    }
    void Detach() {}
    void Reset() {}
    Bool IsActive() const { return FALSE; }
    ModelE GetModel() const { return MODEL_SGB1; }

    Uint8 Read(Uint32 uAddr, Uint8 uOpenBus)
    { (void)uAddr; return uOpenBus; }
    void Write(Uint32 uAddr, Uint8 uData)
    { (void)uAddr; (void)uData; }
    void AdvanceMasterClocks(Uint32 nClocks, Uint32 uSnesMasterHz)
    { (void)nClocks; (void)uSnesMasterHz; }
    void FlushClocks() {}
    void MixAudio(Int16 *pLeft, Int16 *pRight, Int32 nSamples, Uint32 uOutputHz)
    { (void)pLeft; (void)pRight; (void)nSamples; (void)uOutputHz; }

    Bool AttachSavedata(const Uint8 *pData, Uint32 nBytes)
    { (void)pData; return nBytes == 0U ? TRUE : FALSE; }
    Uint32 GetSavedataBytes() { return 0U; }
    Bool ExportSavedata(Uint8 *pData, Uint32 nCapacity, Uint32 *pActualBytes)
    {
        (void)pData; (void)nCapacity;
        if (pActualBytes) *pActualBytes = 0U;
        return TRUE;
    }
    Bool SavedataDirty() const { return FALSE; }
    void ClearSavedataDirty() {}

    Uint32 GetGameBytes() const { return 0U; }
    Uint32 GetGameCRC() const { return 0U; }
    Uint32 GetStateBytes() { return 0U; }
    Bool SaveState(void *pData, Uint32 nBytes)
    { (void)pData; (void)nBytes; return FALSE; }
    Bool RestoreState(const void *pData, Uint32 nBytes)
    { (void)pData; (void)nBytes; return FALSE; }
};

#endif
