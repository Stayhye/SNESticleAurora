
/*!

    \File    emumovie.cpp

    \Description
	    Description

    \Notes
	    None.

    \Copyright
	    (c) 2004 Icer Addis

*/


/*-- Include files -------------------------------------------------------------------------------*/

#include "types.h"
#include "emumovie.h"
#include <stdio.h>
using namespace Emu;

/*-- Preprocessor Defines ------------------------------------------------------------------------*/

/*-- Type Definitions ----------------------------------------------------------------------------*/

/*-- Private Implementation ----------------------------------------------------------------------*/

/*-- Public Implementation -----------------------------------------------------------------------*/

MovieClip::MovieClip(Uint32 uStateSize, Uint32 uMaxFrames)
{
    /* AURORA_STABLEINIT_V6_2_20260824
     * Movie capture is a DEBUG-only input feature, yet the old constructor
     * permanently reserved the largest emulator state plus 216,000 input
     * frames.  That consumed roughly 2.6 MiB before a ROM was selected and
     * starved the embedded reference emulator renderer.  Keep the exact capacity and
     * allocate it only when recording actually begins. */
    m_uStateSize    = 0;
    m_uMaxStateSize = uStateSize;
    m_pStateData    = NULL;

    m_pFrames    = NULL;
    m_uMaxFrames = uMaxFrames;

    m_bRecording        = FALSE;
    m_nRecordedFrames   = 0;

    m_bPlaying          = FALSE;
    m_uPlayFrame        = 0;
}

MovieClip::~MovieClip()
{
    Discard();
}

Bool MovieClip::EnsureStorage()
{
    void *pState;
    SysInputT *pFrames;
    size_t nFrameBytes;

    if (m_pStateData && m_pFrames)
        return TRUE;
    if (!m_uMaxStateSize || !m_uMaxFrames ||
        (size_t)m_uMaxFrames > ((size_t)-1) / sizeof(SysInputT))
        return FALSE;

    nFrameBytes = sizeof(SysInputT) * (size_t)m_uMaxFrames;
    pState = malloc((size_t)m_uMaxStateSize);
    pFrames = (SysInputT *)malloc(nFrameBytes);
    if (!pState || !pFrames)
    {
        free(pState);
        free(pFrames);
        printf("Movie: not enough memory for capture buffers\n");
        return FALSE;
    }

    free(m_pStateData);
    free(m_pFrames);
    m_pStateData = pState;
    m_pFrames = pFrames;
    return TRUE;
}

void MovieClip::Discard()
{
    m_bRecording = FALSE;
    m_bPlaying = FALSE;
    m_nRecordedFrames = 0;
    m_uPlayFrame = 0;
    m_uStateSize = 0;
    free(m_pStateData);
    free(m_pFrames);
    m_pStateData = NULL;
    m_pFrames = NULL;
}


void MovieClip::RecordBegin(System *pSystem)
{
    Int32 nStateSize;

    if (!pSystem || IsRecording() || IsPlaying())
        return;

    nStateSize = pSystem->GetStateSize();
    if (nStateSize <= 0)
    {
        printf("Movie: capture unavailable for this core/state size\n");
        return;
    }

    /* AURORA_WILDCARD_32MBIT_CORE_LIFECYCLE_V1_20260914
     * Learn capacity from the active core. A new recording replaces the
     * previous recording state, so growing capacity may discard the old
     * retained buffers before reallocating. */
    if ((Uint32)nStateSize > m_uMaxStateSize)
    {
        Discard();
        m_uMaxStateSize = (Uint32)nStateSize;
    }
    if (!EnsureStorage())
    {
        printf("Movie: capture unavailable for this core/state size\n");
        return;
    }
    m_uStateSize = (Uint32)nStateSize;

    // save the state
    pSystem->SaveState(m_pStateData, m_uStateSize);

    // reset recorded pointer
    m_nRecordedFrames = 0;
    m_bRecording      = TRUE;
}

void MovieClip::RecordEnd()
{
    m_bRecording      = FALSE;
}

Bool MovieClip::RecordFrame(SysInputT &input)
{
    if (m_bRecording && m_pFrames &&
        m_nRecordedFrames < m_uMaxFrames)
    {
        m_pFrames[m_nRecordedFrames] = input;
        m_nRecordedFrames++;
        return TRUE;
    }
    return FALSE;
}



void MovieClip::PlayBegin(System *pSystem)
{
    if (!pSystem || IsRecording() || IsPlaying() ||
        !m_pStateData || !m_pFrames || !m_uStateSize ||
        !m_nRecordedFrames)
        return;

    // restore the state
    pSystem->RestoreState(m_pStateData, m_uStateSize);

    // reset Played pointer
    m_uPlayFrame     = 0;
    m_bPlaying      = TRUE;
}
void MovieClip::PlayEnd()
{
    m_bPlaying      = FALSE;
}

Bool MovieClip::PlayFrame(SysInputT &input)
{
    if (m_bPlaying && m_pFrames && m_uPlayFrame < m_nRecordedFrames)
    {
        input = m_pFrames[m_uPlayFrame];
        m_uPlayFrame++;
        return TRUE;
    }
    return FALSE;
}



