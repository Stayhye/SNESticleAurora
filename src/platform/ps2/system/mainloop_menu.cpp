#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "mainloop_install.h"
#include "mainloop_input.h"
#include "input.h"
#include "mainloop_menu.h"
#include "mainloop_iop.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "embedded_irx.h"
#include "memcard.h"

extern "C" {
#include "audio.h"
}

extern "C" int list_title_db(char *pPath);

int _MainLoopMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        switch (Type)
        {
                case 1:
                        {
                                char mc0[1024];
                                char mc1[1024];
                                char exploit_dir[256];
                                char **ppInstallFiles = _MainLoop_pInstallFiles;

                                _GetExploitDir(exploit_dir);

                                snprintf(mc0, sizeof(mc0), "mc0:/%s", exploit_dir);
                                snprintf(mc1, sizeof(mc1), "mc1:/%s", exploit_dir);

                                // hack in default destination name for elf
                                ppInstallFiles[0] = (char *)"BOOT.ELF"; // dest
                                switch (Parm1)
                                {
#if 0
                                        case 0:
                                                // cdrom->mc0
                                                ppInstallFiles[1] = VersionGetElfName(); // src
                                                InstallFiles(mc0, "cdrom0:\\", ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 1:
                                                ppInstallFiles[1] = VersionGetElfName(); // src
                                                InstallFiles(mc1, "cdrom0:\\", ppInstallFiles, _MainLoopInstallCallback);
                                                break;
#endif
                                        case 2:
                                                ppInstallFiles[1] = (char *)"SNESTICLE.ELF"; // src
                                                InstallFiles(mc0, (char *)"host:", ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 3:
                                                ppInstallFiles[1] = (char *)"BOOT.ELF"; // src
                                                InstallFiles(mc1, mc0, ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 4:
                                                ppInstallFiles[1] = (char *)"BOOT.ELF"; // src
                                                InstallFiles(mc0, mc1, ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 5:
                                                ppInstallFiles[0] = (char *)"SNESTICLE.ELF"; // dest
                                                ppInstallFiles[1] = (char *)"BOOT.ELF"; // src
                                                InstallFiles((char *)"host:", mc0, ppInstallFiles, _MainLoopInstallCallback);
                                                break;
                                        case 6:
                                                _DumpMemory();
                                                break;
                                        case 7:
                                                snprintf(mc0, sizeof(mc0), "mc0:/%s/TITLE.DB", exploit_dir);
                                                _AddTitleDB(mc0);
                                                break;
                                        case 8:
                                                snprintf(mc0, sizeof(mc0), "mc0:/%s/TITLE.DB", exploit_dir);
                                                list_title_db(mc0);
                                                break;
                                        case 9: // copy rom0:libsd -> host
                                                CopyFile((char *)"host:LIBSD.IRX", (char *)"rom0:LIBSD", NULL);
                                                break;
                                        default:
                                                return 0;
                                }
                                _MainLoop_pMenuScreen->SetText(0, "");
                                _MainLoop_pMenuScreen->SetText(1, "");
                                _MainLoop_pMenuScreen->SetText(2, "");
                        }
                        break;
        }

        return 0;
}


int _MainLoopLogEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        return 0;
}

int _MainLoopStateBrowserEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        (void)Parm1;
        (void)Parm2;

        /* State files are maintenance data, never executable content.
           CBrowserScreen's built-in Select menu still handles deletion. */
        return Type == 2 ? BROWSER_ENTRYTYPE_OTHER : 0;
}

enum MainLoopStateManagerStorageE
{
        MAINLOOP_STATEMANAGER_MASS0,
        MAINLOOP_STATEMANAGER_MASS1,
        MAINLOOP_STATEMANAGER_MASS,
        MAINLOOP_STATEMANAGER_MC0,
        MAINLOOP_STATEMANAGER_MC1,
        MAINLOOP_STATEMANAGER_MMCE0,
        MAINLOOP_STATEMANAGER_MMCE1,
        MAINLOOP_STATEMANAGER_HDD,

        MAINLOOP_STATEMANAGER_STORAGE_NUM
};

struct MainLoopStateManagerStorageT
{
        const char *pName;
        const char *pPath;
};

static const MainLoopStateManagerStorageT _MainLoop_StateManagerStorage[] =
{
        { "mass0:", "mass0:/SNESticle/states/" },
        { "mass1:", "mass1:/SNESticle/states/" },
        { "mass:",  "mass:/SNESticle/states/" },
        { "mc0:",   "mc0:/SNESticle/" },
        { "mc1:",   "mc1:/SNESticle/" },
        { "mmce0:", "mmce0:/SNESticle/" },
        { "mmce1:", "mmce1:/SNESticle/" },
        { "Internal HDD", "hdd0:" }
};

static Int32 _MainLoop_StateManagerStorageIndex =
        MAINLOOP_STATEMANAGER_MASS0;

static CScreen *_MainLoop_StateBrowserPreviousScreen = NULL;

static char _MainLoop_StateManagerBrowseEntry[] = "Browse State Files";
static char _MainLoop_StateManagerBrowseSramEntry[] = "Browse SRAM Files";
static char _MainLoop_StateManagerSramEntry[48];
static char _MainLoop_StateManagerStorageEntry[48];
static char _MainLoop_StateManagerSlotEntry[32];
static char _MainLoop_StateManagerResetEntry[] = "Ask Save Location Again";
static char _MainLoop_StateManagerStatus[64];

char *_MainLoopStateMenuEntries[] =
{
        _MainLoop_StateManagerBrowseEntry,
        _MainLoop_StateManagerBrowseSramEntry,
        _MainLoop_StateManagerSramEntry,
        _MainLoop_StateManagerStorageEntry,
        _MainLoop_StateManagerSlotEntry,
        _MainLoop_StateManagerResetEntry,
        NULL
};

static Bool _MainLoopStateManagerStorageAvailable(Int32 iStorage)
{
        switch (iStorage)
        {
                case MAINLOOP_STATEMANAGER_MASS0:
                case MAINLOOP_STATEMANAGER_MASS1:
                case MAINLOOP_STATEMANAGER_MASS:
                        return MassStorageIsEnabled() ? TRUE : FALSE;

                case MAINLOOP_STATEMANAGER_MMCE0:
                        return (MmceProbeAvailableSlots() & 1) ? TRUE : FALSE;

                case MAINLOOP_STATEMANAGER_MMCE1:
                        return (MmceProbeAvailableSlots() & 2) ? TRUE : FALSE;

                case MAINLOOP_STATEMANAGER_HDD:
                        return HddSupportIsEnabled() ? TRUE : FALSE;

                default:
                        return TRUE;
        }
}

static void _MainLoopStateManagerNormalizeStorage()
{
        Int32 i;

        if (_MainLoopStateManagerStorageAvailable(
                _MainLoop_StateManagerStorageIndex))
        {
                return;
        }

        for (i = 0; i < MAINLOOP_STATEMANAGER_STORAGE_NUM; i++)
        {
                if (_MainLoopStateManagerStorageAvailable(i))
                {
                        _MainLoop_StateManagerStorageIndex = i;
                        return;
                }
        }
}

static void _MainLoopStateManagerCycleStorage()
{
        Int32 i;

        for (i = 0; i < MAINLOOP_STATEMANAGER_STORAGE_NUM; i++)
        {
                _MainLoop_StateManagerStorageIndex++;
                if (_MainLoop_StateManagerStorageIndex >=
                    MAINLOOP_STATEMANAGER_STORAGE_NUM)
                {
                        _MainLoop_StateManagerStorageIndex = 0;
                }

                if (_MainLoopStateManagerStorageAvailable(
                        _MainLoop_StateManagerStorageIndex))
                {
                        return;
                }
        }
}

void _MainLoopStateMenuRefresh()
{
        const char *pQuickTarget;

        if (!_MainLoop_pStateScreen)
        {
                return;
        }

        _MainLoopStateManagerNormalizeStorage();
        snprintf(_MainLoop_StateManagerSramEntry,
                 sizeof(_MainLoop_StateManagerSramEntry),
                 "SRAM Storage: %s", MainLoopSramGetDeviceName());
        pQuickTarget = MainLoopStateHasDeviceChoice()
                ? MainLoopStateGetDeviceName()
                : "Not chosen";

        snprintf(
                _MainLoop_StateManagerStorageEntry,
                sizeof(_MainLoop_StateManagerStorageEntry),
                "Storage: %s",
                _MainLoop_StateManagerStorage[
                        _MainLoop_StateManagerStorageIndex
                ].pName
        );
        if (MainLoopStateGetDevice() == MAINLOOP_STATEDEVICE_AUTO)
        {
                snprintf(
                        _MainLoop_StateManagerSlotEntry,
                        sizeof(_MainLoop_StateManagerSlotEntry),
                        "Quick Slot: 1 (Auto)"
                );
        }
        else
        {
                snprintf(
                        _MainLoop_StateManagerSlotEntry,
                        sizeof(_MainLoop_StateManagerSlotEntry),
                        "Quick Slot: %d",
                        (int)MainLoopStateGetSlot() + 1
                );
        }

        snprintf(
                _MainLoop_StateManagerStatus,
                sizeof(_MainLoop_StateManagerStatus),
                "Quick target: %s",
                pQuickTarget
        );
        _MainLoop_pStateScreen->SetText(0, _MainLoop_StateManagerStatus);
        _MainLoop_pStateScreen->SetText(1, "X: open  SELECT: file menu");
        _MainLoop_pStateScreen->SetText(2, "Delete removes both state banks");
        _MainLoop_pStateScreen->SetText(3, "HDD: choose partition first");
}

int _MainLoopStateMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        (void)Parm2;
        if (Type != 1)
        {
                return 0;
        }

        switch (Parm1)
        {
                case 0:
    if ((_MainLoop_StateManagerStorageIndex ==
         MAINLOOP_STATEMANAGER_MC0 ||
         _MainLoop_StateManagerStorageIndex ==
         MAINLOOP_STATEMANAGER_MC1))
    {
        Int32 iPort =
            _MainLoop_StateManagerStorageIndex ==
            MAINLOOP_STATEMANAGER_MC0 ? 0 : 1;

        if (MemCardGetStatus(iPort) ==
            MEMCARD_STATUS_UNFORMATTED)
        {
            _MainLoopMemCardFormatPromptOpen(
                iPort,
                MAINLOOP_MEMCARDFORMAT_BROWSE
            );
            break;
        }
    }

    /* AURORA_RUNTIME_SAFE_STATE_BROWSER_V1_4_1
     * Reuse the browser allocated once in MainLoopInit. Reallocating it on
     * every visit leaked the old entry buffer and could exhaust EE RAM. */
    if (!_MainLoop_pStateBrowserScreen)
    {
        MainLoopStatusPrintf(180, "State browser unavailable.");
        break;
    }

    _MainLoop_StateBrowserPreviousScreen = _MainLoop_pStateScreen;
    _MainLoop_pStateBrowserScreen->SetMsgFunc(
        _MainLoopStateBrowserEvent
    );
    _MainLoop_pStateBrowserScreen->SetStateManager(TRUE);
    _MainLoop_pStateBrowserScreen->SetSramManager(FALSE);

    _MainLoopSetScreen(
        (CScreen *)_MainLoop_pStateBrowserScreen
    );

    _MainLoop_pStateBrowserScreen->SetDir(
        _MainLoop_StateManagerStorage[
            _MainLoop_StateManagerStorageIndex
        ].pPath
    );
    break;

                case 1:
{
    if (!_MainLoop_pStateBrowserScreen)
    {
        MainLoopStatusPrintf(180, "SRAM browser unavailable.");
        break;
    }

    const Char *pSystemDir =
        (_pSystem == _pNes) ? "NES" :
        ((_pSystem == _pSega) ? "SEGA" : "SNES");

    Char SramDir[1024];

    _MainLoop_StateBrowserPreviousScreen =
        _MainLoop_pStateScreen;

    _MainLoop_pStateBrowserScreen->SetStateManager(FALSE);
    _MainLoop_pStateBrowserScreen->SetSramManager(TRUE);

    snprintf(
        SramDir,
        sizeof(SramDir),
        "%s/%s/",
        MainLoopSramGetBrowseRoot(),
        pSystemDir
    );

    _MainLoopSetScreen(
        (CScreen *)_MainLoop_pStateBrowserScreen
    );

    _MainLoop_pStateBrowserScreen->SetDir(SramDir);
    break;
}

                case 2:
                        MainLoopSramCycleDevice();
                        VideoSettingsSave();
                        _MainLoopStateMenuRefresh();
                        break;

                case 3:
                        _MainLoopStateManagerCycleStorage();
                        _MainLoopStateMenuRefresh();
                        break;

                case 4:
                        MainLoopStateCycleSlot();
                        if (MainLoopStateHasDeviceChoice())
                        {
                                MainLoopStateSettingsSave();
                        }
                        _MainLoopStateMenuRefresh();
                        break;

                case 5:
                        MainLoopStateForgetDeviceChoice();
                        _MainLoopStateMenuRefresh();
                        MainLoopModalPrintf(
                                45,
                                "Next L2+X will ask the save location."
                        );
                        break;
        }

        return 0;
}

/* AURORA_STATE_CONFIRM_V1 */
static char _MainLoop_StateConfirmNoEntry[]  = "NO";
static char _MainLoop_StateConfirmYesEntry[] = "YES";
static char *_MainLoop_StateConfirmEntries[] =
{
        _MainLoop_StateConfirmNoEntry,
        _MainLoop_StateConfirmYesEntry,
        NULL
};
static Bool _MainLoop_StateConfirmSave = FALSE;
static Bool _MainLoop_StateConfirmArmed = FALSE;
/* AURORA_AUDIO_UI_SOFT_TRANSITION_V2_20260901
 * UI-only one-shot: tells the caller that YES already performed the complete
 * save/load audio transition, so it must not issue an extra UiResume(). */
static Bool _MainLoop_StateConfirmExecuted = FALSE;
static CScreen *_MainLoop_StateConfirmPreviousScreen = NULL;

static void _MainLoopStateConfirmPromptClose()
{
        CScreen *pReturn = _MainLoop_StateConfirmPreviousScreen;
        if (!pReturn || pReturn == (CScreen *)_MainLoop_pStateConfirmScreen)
                pReturn = (CScreen *)_MainLoop_pBrowserScreen;

        _MainLoopInputSuppressUntilRelease();
        _MainLoopSetScreen(pReturn);
        _MainLoop_StateConfirmPreviousScreen = NULL;
        _MainLoop_StateConfirmArmed = FALSE;
        /* AURORA_AUDIO_UI_SOFT_TRANSITION_V1_20260901
         * Isolated quick-state prompt: it never scheduled normal menu SRAM
         * work or entered menu BGM, so do not route its close through
         * _MenuEnable(FALSE), which performs a destructive audsrv hard-cut.
         *
         * AURORA_EMPTY_FRONTEND_INVARIANT_V1_20260914: with lazy cores, a System may no longer
         * exist. Never manufacture gameplay-without-core here. */
        if (_pSystem)
        {
                _bMenu = FALSE;
        }
        else
        {
                _MainLoopSetScreen((CScreen *)_MainLoop_pBrowserScreen);
                _bMenu = TRUE;
        }
}

void _MainLoopStateConfirmPromptOpen(Bool bSave)
{
        if (!_MainLoop_pStateConfirmScreen || _bMenu || !_pSystem)
                return;

        /* AURORA_AUDIO_UI_SOFT_TRANSITION_V1_20260901
         * Audio was already soft-muted by the quick-state input path. */
        _MainLoop_StateConfirmSave = bSave;
        _MainLoop_StateConfirmArmed = FALSE;
        _MainLoop_StateConfirmExecuted = FALSE;
        _MainLoop_StateConfirmPreviousScreen = _MainLoop_pScreen;
        _MainLoop_pStateConfirmScreen->SetEntries(_MainLoop_StateConfirmEntries);
        _MainLoop_pStateConfirmScreen->SetSelection(0);
        _MainLoop_pStateConfirmScreen->SetTitle(bSave ? "Save state?" : "Load state?");
        _MainLoop_pStateConfirmScreen->SetText(0, "Left/Right: choose   X: confirm");
        _MainLoop_pStateConfirmScreen->SetText(1, "Default: NO");
        _MainLoop_pStateConfirmScreen->SetText(2, "");
        _MainLoop_pStateConfirmScreen->SetText(3, "");

        /* Do not call _MenuEnable(TRUE): quick-state confirmation must not
           schedule the ordinary L2+R2 SRAM flush. */
        _bMenu = TRUE;
        _MainLoopSetScreen((CScreen *)_MainLoop_pStateConfirmScreen);
}

void _MainLoopStateConfirmPromptCancel()
{
        if (_MainLoop_pScreen == (CScreen *)_MainLoop_pStateConfirmScreen)
                _MainLoopStateConfirmPromptClose();
}

/* AURORA_AUDIO_UI_SOFT_TRANSITION_V2_20260901 */
Bool _MainLoopStateConfirmPromptConsumeExecuted()
{
        const Bool executed = _MainLoop_StateConfirmExecuted;
        _MainLoop_StateConfirmExecuted = FALSE;
        return executed;
}

void _MainLoopStateConfirmPromptInput(Uint32 buttons, Uint32 trigger)
{
        const Uint32 NeutralMask = PAD_LEFT | PAD_RIGHT | PAD_CROSS |
            PAD_CIRCLE | PAD_START | PAD_L2 | PAD_R2;

        if (_MainLoop_pScreen != (CScreen *)_MainLoop_pStateConfirmScreen)
                return;

        /* Hard input flush: no choice is accepted until one completely
           neutral frame occurs after the opening L2+X/L2+O combination. */
        if (!_MainLoop_StateConfirmArmed)
        {
                if (!(buttons & NeutralMask))
                        _MainLoop_StateConfirmArmed = TRUE;
                return;
        }

        if (trigger & PAD_CIRCLE)
        {
                _MainLoopStateConfirmPromptClose();
                return;
        }
        _MainLoop_pStateConfirmScreen->Input(buttons, trigger);
}

int _MainLoopStateConfirmMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        Bool bSave;
        (void)Parm2;
        if (Type != 1 || Parm1 > 1)
                return 0;

        bSave = _MainLoop_StateConfirmSave;
        if (Parm1 == 0)
        {
                _MainLoopStateConfirmPromptClose();
                return 0;
        }

        _MainLoopStateConfirmPromptClose();
        _MainLoopQuickStateExecuteConfirmed(bSave);
        _MainLoop_StateConfirmExecuted = TRUE;
        return 0;
}

static char _MainLoop_StateDeviceAutoEntry[] = "Auto";
static char _MainLoop_StateDeviceUSBEntry[] = "USB / mass";
static char _MainLoop_StateDeviceMCEntry[] = "Memory Card";
static char _MainLoop_StateDeviceMMCEEntry[] = "MMCE";
static char _MainLoop_StateDeviceHDDEntry[] = "Internal HDD";
static char *_MainLoop_StateDeviceEntries[MAINLOOP_STATEDEVICE_NUM + 1];
static MainLoopStateDeviceE
        _MainLoop_StateDeviceMap[MAINLOOP_STATEDEVICE_NUM];
static Int32 _MainLoop_StateDeviceEntryCount = 0;

static CScreen *_MainLoop_StateDevicePreviousScreen = NULL;
void _MainLoopStateBrowserReturn(void)
{
    if (_MainLoop_StateBrowserPreviousScreen)
    {
        _MainLoopSetScreen(
            _MainLoop_StateBrowserPreviousScreen
        );
        _MainLoop_StateBrowserPreviousScreen = NULL;
    }

    if (_MainLoop_pStateBrowserScreen)
    {
        _MainLoop_pStateBrowserScreen->SetStateManager(FALSE);
        _MainLoop_pStateBrowserScreen->SetSramManager(FALSE);
    }
}
static void _MainLoopStateDeviceAddEntry(
        MainLoopStateDeviceE eDevice,
        char *pEntry)
{
        /* A default is a user preference, not a probe result. Show every
           writable storage class even when no device is attached right now;
           an unavailable target will report a normal save error later. */
        if (_MainLoop_StateDeviceEntryCount >= MAINLOOP_STATEDEVICE_NUM)
        {
                return;
        }

        _MainLoop_StateDeviceEntries[
                _MainLoop_StateDeviceEntryCount
        ] = pEntry;
        _MainLoop_StateDeviceMap[
                _MainLoop_StateDeviceEntryCount
        ] = eDevice;
        _MainLoop_StateDeviceEntryCount++;
}

static void _MainLoopStateDevicePromptClose()
{
        CScreen *pReturnScreen = _MainLoop_StateDevicePreviousScreen;

        if (!pReturnScreen ||
            pReturnScreen == (CScreen *)_MainLoop_pStateDeviceScreen)
        {
                pReturnScreen = (CScreen *)_MainLoop_pBrowserScreen;
        }

        /* Cross/Start confirms and Circle cancels. Keep that face-button
           press out of the first resumed emulation frame. */
        _MainLoopInputSuppressUntilRelease();
        _MainLoopSetScreen(pReturnScreen);
        _MainLoop_StateDevicePreviousScreen = NULL;
        _MenuEnable(FALSE);
}

void _MainLoopStateDevicePromptOpen()
{
        if (!_MainLoop_pStateDeviceScreen || _bMenu)
        {
                return;
        }

        _MainLoop_StateDeviceEntryCount = 0;
        _MainLoopStateDeviceAddEntry(
                MAINLOOP_STATEDEVICE_AUTO,
                _MainLoop_StateDeviceAutoEntry
        );
        _MainLoopStateDeviceAddEntry(
                MAINLOOP_STATEDEVICE_USB,
                _MainLoop_StateDeviceUSBEntry
        );
        _MainLoopStateDeviceAddEntry(
                MAINLOOP_STATEDEVICE_MEMCARD,
                _MainLoop_StateDeviceMCEntry
        );
        _MainLoopStateDeviceAddEntry(
                MAINLOOP_STATEDEVICE_MMCE,
                _MainLoop_StateDeviceMMCEEntry
        );
        _MainLoopStateDeviceAddEntry(
                MAINLOOP_STATEDEVICE_HDD,
                _MainLoop_StateDeviceHDDEntry
        );
        _MainLoop_StateDeviceEntries[
                _MainLoop_StateDeviceEntryCount
        ] = NULL;

        _MainLoop_pStateDeviceScreen->SetEntries(
                _MainLoop_StateDeviceEntries
        );
        _MainLoop_pStateDeviceScreen->SetSelection(0);
        _MainLoop_pStateDeviceScreen->SetText(
                0,
                "Choose quick-save location"
        );
        _MainLoop_pStateDeviceScreen->SetText(
                1,
                "X: choose, save, return"
        );
        _MainLoop_pStateDeviceScreen->SetText(2, "Circle: cancel");
        _MainLoop_pStateDeviceScreen->SetText(
                3,
                "Change later in State Manager"
        );

        _MainLoop_StateDevicePreviousScreen = _MainLoop_pScreen;
        if (_MainLoop_bAudioReady)
        {
                Aud_Setvol(0);
        }

        /* Pause without _MenuEnable(TRUE): entering this one-time chooser
           must not flush SRAM. L2+R2 remains the dedicated menu/SRAM path. */
        _bMenu = TRUE;
        _MainLoopSetScreen((CScreen *)_MainLoop_pStateDeviceScreen);
}

void _MainLoopStateDevicePromptCancel()
{
        if (_MainLoop_pScreen == (CScreen *)_MainLoop_pStateDeviceScreen)
        {
                _MainLoopStateDevicePromptClose();
        }
}

int _MainLoopStateDeviceMenuEvent(
        Uint32 Type,
        Uint32 Parm1,
        void *Parm2)
{
        Bool bOK;

        (void)Parm2;
        if (Type != 1 ||
            Parm1 >= (Uint32)_MainLoop_StateDeviceEntryCount)
        {
                return 0;
        }

        /* AURORA_FINAL_V1_1_UI_CD_STORAGE_BARRIER_20260901
         * The chooser UI is intentionally instant. Quiesce only after X
         * confirms a device, before state.cfg or state payload touches disk. */
        if (!MainLoopCdUiQuiesce())
        {
                MainLoopStatusPrintf(
                        120, "CD I/O busy; save deferred.");
                return 1;
        }

        MainLoopStateSetDevice(_MainLoop_StateDeviceMap[Parm1]);
        MainLoopStateSettingsSave();

        MainLoopModalPrintf(
                1,
                "Saving state slot %d...",
                (int)MainLoopStateGetSlot() + 1
        );
        bOK = _MainLoopSaveState();
        MainLoopCdUiResume();
        _MainLoopStateMenuRefresh();

        if (!bOK && MainLoopStateGetUnformattedCard() >= 0)
        {
                Int32 iPort = MainLoopStateGetUnformattedCard();

                _MainLoopStateDevicePromptClose();
                _MainLoopMemCardFormatPromptOpen(
                        iPort,
                        MAINLOOP_MEMCARDFORMAT_STATE_SAVE
                );
                return 1;
        }

        _MainLoopStateDevicePromptClose();
        MainLoopStatusPrintf(
                bOK ? 90 : 180,
                "%s",
                MainLoopStateGetLastMessage()
        );
        return 1;
}

static char _MainLoop_MemCardFormatEntry[48];
static char _MainLoop_MemCardFormatCancelEntry[] = "No - Cancel";
static char *_MainLoop_MemCardFormatEntries[] =
{
        _MainLoop_MemCardFormatEntry,
        _MainLoop_MemCardFormatCancelEntry,
        NULL
};
static Int32 _MainLoop_MemCardFormatPort = -1;
static MainLoopMemCardFormatActionE _MainLoop_MemCardFormatAction =
        MAINLOOP_MEMCARDFORMAT_STATE_SAVE;
static CScreen *_MainLoop_MemCardFormatPreviousScreen = NULL;
static Bool _MainLoop_MemCardFormatResumeGame = FALSE;

static void _MainLoopMemCardFormatPromptFinish(CScreen *pNextScreen)
{
        Bool bResumeGame = _MainLoop_MemCardFormatResumeGame;

        if (!pNextScreen ||
            pNextScreen == (CScreen *)_MainLoop_pMemCardFormatScreen)
        {
                pNextScreen = _MainLoop_MemCardFormatPreviousScreen;
        }
        if (!pNextScreen)
        {
                pNextScreen = (CScreen *)_MainLoop_pBrowserScreen;
        }

        _MainLoopInputSuppressUntilRelease();
        _MainLoopSetScreen(pNextScreen);
        _MainLoop_MemCardFormatPreviousScreen = NULL;
        _MainLoop_MemCardFormatPort = -1;
        _MainLoop_MemCardFormatResumeGame = FALSE;

        if (bResumeGame)
        {
                _MenuEnable(FALSE);
        }
}

void _MainLoopMemCardFormatPromptOpen(
        Int32 iPort,
        MainLoopMemCardFormatActionE eAction)
{
        if (!_MainLoop_pMemCardFormatScreen || iPort < 0 || iPort > 1)
        {
                return;
        }

        _MainLoop_MemCardFormatPort = iPort;
        _MainLoop_MemCardFormatAction = eAction;
        _MainLoop_MemCardFormatPreviousScreen = _MainLoop_pScreen;
        _MainLoop_MemCardFormatResumeGame = _bMenu ? FALSE : TRUE;

        snprintf(
                _MainLoop_MemCardFormatEntry,
                sizeof(_MainLoop_MemCardFormatEntry),
                "YES - Format mc%d:",
                (int)iPort
        );
        _MainLoop_pMemCardFormatScreen->SetEntries(
                _MainLoop_MemCardFormatEntries
        );
        /* Destructive action is never the default selection. */
        _MainLoop_pMemCardFormatScreen->SetSelection(1);
        _MainLoop_pMemCardFormatScreen->SetText(
                0,
                iPort == 0
                        ? "mc0: is not formatted"
                        : "mc1: is not formatted"
        );
        _MainLoop_pMemCardFormatScreen->SetText(
                1,
                "FORMATTING ERASES THE ENTIRE CARD"
        );
        _MainLoop_pMemCardFormatScreen->SetText(
                2,
                "Select YES, then press X"
        );
        _MainLoop_pMemCardFormatScreen->SetText(
                3,
                "Circle: cancel safely"
        );

        if (_MainLoop_MemCardFormatResumeGame)
        {
                if (_MainLoop_bAudioReady)
                {
                        Aud_Setvol(0);
                }
                /* Pause without triggering the menu's SRAM-save path. */
                _bMenu = TRUE;
        }
        _MainLoopSetScreen((CScreen *)_MainLoop_pMemCardFormatScreen);
}

void _MainLoopMemCardFormatPromptCancel()
{
        if (_MainLoop_pScreen ==
            (CScreen *)_MainLoop_pMemCardFormatScreen)
        {
                _MainLoopMemCardFormatPromptFinish(NULL);
        }
}

int _MainLoopMemCardFormatMenuEvent(
        Uint32 Type,
        Uint32 Parm1,
        void *Parm2)
{
        Char SaveDirectory[32];
        Bool bOK = FALSE;
        Bool bCdIoHeld = FALSE; /* AURORA_FINAL_V1_2_STORAGE_CLOSURE_20260901 */
        CScreen *pNextScreen = NULL;
        Int32 iPort = _MainLoop_MemCardFormatPort;

        (void)Parm2;
        if (Type != 1 || iPort < 0 || iPort > 1)
        {
                return 0;
        }

        if (Parm1 != 0)
        {
                _MainLoopMemCardFormatPromptCancel();
                return 1;
        }

        /* AURORA_FINAL_V1_2_STORAGE_CLOSURE_20260901
         * If this prompt came directly from gameplay (quick-save failure),
         * the previous state attempt already released its temporary CD hold.
         * Re-acquire only for the destructive storage transaction. If the
         * prompt came from the normal menu, that menu already owns quiesce
         * and must keep owning it across this nested screen. */
        if (_MainLoop_MemCardFormatResumeGame)
        {
                if (!MainLoopCdUiQuiesce())
                {
                        MainLoopStatusPrintf(
                                120, "CD I/O busy; format deferred.");
                        return 1;
                }
                bCdIoHeld = TRUE;
        }

        MainLoopModalPrintf(1, "Formatting mc%d:...", (int)iPort);
        if (!MemCardFormat(iPort))
        {
                if (bCdIoHeld)
                {
                        MainLoopCdUiResume();
                        bCdIoHeld = FALSE;
                }
                MainLoopStatusPrintf(
                        180,
                        "Could not format mc%d:.",
                        (int)iPort
                );
                _MainLoop_pMemCardFormatScreen->SetText(
                        3,
                        "Format failed - Circle: cancel"
                );
                return 1;
        }

        snprintf(
                SaveDirectory,
                sizeof(SaveDirectory),
                "mc%d:/SNESticle",
                (int)iPort
        );
        MemCardCreateSave(
                SaveDirectory,
                _MainLoop_SaveTitle,
                TRUE
        );

        switch (_MainLoop_MemCardFormatAction)
        {
                case MAINLOOP_MEMCARDFORMAT_STATE_SAVE:
                        MainLoopStateSettingsSave();
                        bOK = _MainLoopSaveState();
                        _MainLoopStateMenuRefresh();
                        break;

                case MAINLOOP_MEMCARDFORMAT_SRAM_SAVE:
                        bOK = _MainLoopSaveSRAM(TRUE);
                        if (bOK)
                        {
                                _MainLoop_SRAMUpdated = FALSE;
                        }
                        break;

                case MAINLOOP_MEMCARDFORMAT_BROWSE:
                        _MainLoop_pStateBrowserScreen->SetDir(
                                iPort == 0
                                        ? "mc0:/SNESticle/"
                                        : "mc1:/SNESticle/"
                        );
                        pNextScreen =
                                (CScreen *)_MainLoop_pStateBrowserScreen;
                        bOK = TRUE;
                        break;
        }

        if (bCdIoHeld)
        {
                MainLoopCdUiResume();
                bCdIoHeld = FALSE;
        }

        _MainLoopMemCardFormatPromptFinish(pNextScreen);
        MainLoopStatusPrintf(
                bOK ? 120 : 180,
                "%s",
                _MainLoop_MemCardFormatAction ==
                        MAINLOOP_MEMCARDFORMAT_STATE_SAVE
                        ? MainLoopStateGetLastMessage()
                        : (bOK
                           ? "Memory card formatted."
                           : "Memory card formatted, but save failed.")
        );
        return 1;
}


const char *_MainLoopMenuEntries[]=
{
        (char *)"Copy cdrom0: -> mc0:",
        (char *)"Copy cdrom0: -> mc1:",
        (char *)"Copy host: -> mc0:",
        (char *)"Copy mc0: -> mc1:",
        (char *)"Copy mc1: -> mc0:",
        (char *)"Copy mc0: -> host:",
        (char *)"Dump memory -> host:",
        (char *)"Add PSX CD to mc0:title.db",
        (char *)"Dump mc0:title.db -> tty0:",
        (char *)"Copy rom0:libsd -> host:",
        NULL
};


char *_MainLoop_pInstallFiles[] =
{
        (char *)"BOOT.ELF", (char *)"BOOT.ELF",
        (char *)"TITLE.DB", (char *)"TITLE.DB",
        (char *)"ICON.SYS", (char *)"ICON.SYS",
        (char *)"PS2IP.IRX", (char *)"PS2IP.IRX",
        (char *)"PS2IPS.IRX", (char *)"PS2IPS.IRX",
        (char *)"PS2LINK.IRX", (char *)"PS2LINK.IRX",
        (char *)"PS2SMAP.IRX", (char *)"PS2SMAP.IRX",
        (char *)"CDVD.IRX", (char *)"CDVD.IRX",
        (char *)"SJPCM2.IRX", (char *)"SJPCM2.IRX",
        (char *)"MCSAVE.IRX", (char *)"MCSAVE.IRX",
        (char *)"NETPLAY.IRX", (char *)"NETPLAY.IRX",
        NULL
};
