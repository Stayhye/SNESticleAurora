

#include "types.h"
#include "snspcbrr.h"
#include "prof.h"

#define SNSPCBRR_CLAMP FALSE

/*
ldl		t0    // load 16 nibbles
ldr		t0		   // T0 = OPMN KLIJ GHEF CDAB
pextlb  t1,t0,r0   // t1 = OP00 MN00 KL00 IJ00 GH00 EF00 CD00 AB00
psllh   t2,t1,4    // t2 = P000 N000 L000 J000 H000 F000 D000 B000
psrah   t1,t1,12   // t1 = OOOO MMMM KKKK IIII GGGG EEEE CCCC AAAA
psrah   t2,t2,12   // t2 = PPPP NNNN LLLL JJJJ HHHH FFFF DDDD BBBB
pcpyld  t3,t2,t2   // t3 = HHHH FFFF DDDD BBBB HHHH FFFF DDDD BBBB
pcpyud  t4,t1,t1   // t4 = OOOO MMMM KKKK IIII OOOO MMMM KKKK IIII 
pinth   t0,t3,t0   // t0 = HHHH GGGG FFFF EEEE DDDD CCCC BBBB AAAA
pinth   t2,t4,t2   // t2 = PPPP OOOO NNNN MMMM LLLL KKKK JJJJ IIII
*/

/* AURORA_SPC700_MEGA_ACCURACY_V1_20260916 */


#if 0
typedef void (*SNSpcBRRDecodeFuncT)(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 eFilterType, Int32 iPrev0, Int32 iPrev1);

static void _SNSpcBRRFilter(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 eFilterType, Int32 iPrev0, Int32 iPrev1);
/* AURORA_SAFE_CODE_PERF_V1_BRR
 * Preserve the exact Int16 output conversion, but reuse that converted value
 * for filter history instead of storing it and immediately loading it back. */
static void _SNSpcBRRFilter3(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 eFilterType, Int32 iPrev0, Int32 iPrev1)
{
	while (nSamples-- > 0)
	{
		Int32 s = ((Int32)*pIn++) >> 1;
		Int32 p2 = iPrev1 >> 1;
		switch (eFilterType & 3)
		{
		case 0: break;
		case 1: s += iPrev0 >> 1; s += (-iPrev0) >> 5; break;
		case 2: s += iPrev0; s -= p2; s += p2 >> 4; s += (iPrev0 * -3) >> 6; break;
		default: s += iPrev0; s -= p2; s += (iPrev0 * -13) >> 7; s += (p2 * 3) >> 4; break;
		}
		if (s > 32767) s = 32767;
		else if (s < -32768) s = -32768;
		Int16 out = (Int16)((Uint32)(Uint16)(Int16)s << 1);
		*pOut++ = out;
		iPrev1 = iPrev0;
		iPrev0 = out;
	}
}

#endif

#if 0

static void _SNSpcBRRFilter4(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 eFilterType, Int32 iPrev0, Int32 iPrev1)
{
	Int32 iFilter0, iFilter1;
	Int32 iMin = -0x8000;
	Int32 iMax = 0x7FFF;

	iFilter0 = _SNSpcDsp_FilterParm[eFilterType][0];
	iFilter1 = _SNSpcDsp_FilterParm[eFilterType][1];

	while (nSamples > 0)
	{
		Int32 iSample;

		iSample  = *pIn;
		iSample>>=1;

		iSample += ((iPrev0 * iFilter0)>>9) + ((iPrev1 * iFilter1) >> 9);

		// rotate sample queue
		iPrev1 = iPrev0;
		iPrev0 = iSample << 1;

		// clamp sample to 16-bit range
		if (iSample >  iMax) iSample = iMax;
		if (iSample <  iMin) iSample = iMin;
		iSample<<=1;

		// write sample
		*pOut = iSample;

		pOut++;
		pIn++;
		nSamples--;
	}
}

#endif




/* AURORA_BRR_FILTER3_ACTIVE_RESTORE_V1_20260916 */
static void _SNSpcBRRFilter3(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 eFilterType, Int32 iPrev0, Int32 iPrev1)
{
    while (nSamples-- > 0)
    {
        Int32 s = ((Int32)*pIn++) >> 1;
        Int32 p2 = iPrev1 >> 1;
        switch (eFilterType & 3)
        {
        case 0: break;
        case 1: s += iPrev0 >> 1; s += (-iPrev0) >> 5; break;
        case 2: s += iPrev0; s -= p2; s += p2 >> 4; s += (iPrev0 * -3) >> 6; break;
        default: s += iPrev0; s -= p2; s += (iPrev0 * -13) >> 7; s += (p2 * 3) >> 4; break;
        }
        if (s > 32767) s = 32767;
        else if (s < -32768) s = -32768;
        Int16 out = (Int16)((Uint32)(Uint16)(Int16)s << 1);
        *pOut++ = out;
        iPrev1 = iPrev0;
        iPrev0 = out;
    }
}


Uint8 SNSpcBRRDecode(Uint8 *pBRRBlock, Int16 *pOut, Int32 iPrev0, Int32 iPrev1)
{
	Uint8 uHeader;
	Uint32 uRange;
	Int16 Decode[16];
	Int16 *pDecode;
	Int32 iByte;

	uHeader = *pBRRBlock++;
	uRange = uHeader >> 4;
	pDecode = Decode;
	for (iByte=0; iByte<8; iByte++)
	{
		Int32 d=*pBRRBlock++, hi=(d>>4)&15, lo=d&15;
		if (hi&8) hi-=16;
		if (lo&8) lo-=16;
		if (uRange<=12) { Int32 scale=1<<uRange; hi*=scale; lo*=scale; }
		else { hi=(hi<0)?-4096:0; lo=(lo<0)?-4096:0; }
		pDecode[0]=(Int16)hi;
		pDecode[1]=(Int16)lo;
		pDecode+=2;
	}

	// apply filter
//	_SNSpcBRR_pDecodeFunc(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);
//	_SNSpcBRRFilter(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);
//	_SNSpcBRRFilter_9x(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);
//	_SNSpcBRRFilter2(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);

	_SNSpcBRRFilter3(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);
//	_SNSpcBRRFilter4(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);


	// return end and loop bits
	return uHeader & 3;
}



void SNSpcBRRClear(Int16 *pOut, Int16 iPrev)
{
	Int32 nSamples = 16;
	while (nSamples > 0)
	{
		pOut[0]=iPrev;
		pOut[1]=iPrev;
		pOut[2]=iPrev;
		pOut[3]=iPrev;
		pOut+=4;
		nSamples-=4;
	}
}

