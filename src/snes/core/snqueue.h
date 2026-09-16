
#ifndef _SNQUEUE_H
#define _SNQUEUE_H

#define SNQUEUE_SIZE (512)
#define SNPPU_QUEUE_SIZE (4096)

/* AURORA_REVIVE_005CEE_PPU_RING_20260829
 * Revive 005cee6d6731: mantém as filas SPC com 512 entradas, mas dá ao PPU
 * uma fila circular dedicada de 4096. Jogos raster/HDMA pesados podem emitir
 * milhares de writes por frame; evitar o "full" elimina SyncPPU forçado sem
 * mudar ordem, ciclo ou conteúdo de nenhum write. */
struct SNQueueElementT
{
	Uint32	uCycle;
	Uint16	uAddr;
	Uint8	uData;
	Uint8	uPad;
};

template <Int32 t_nSize>
class SNQueueT
{
public:
    SNQueueT()
    {
        Reset();
    }

	inline void Reset()
	{
		m_iHead = m_iTail = m_nCount = 0;
	}

	inline Bool IsEmpty()
	{
		return m_nCount == 0;
	}

	/* AURORA_PPU_MEMORY_V3_QUEUE_20260915
	 * SNQueueElementT already has one padding byte. PPU writes reuse it for
	 * memory-bus phase metadata, so the 4096-entry queue stays exactly the
	 * same size. SPC callers keep the default zero metadata. */
	inline Bool Enqueue(Uint32 uCycle, Uint32 uAddr, Uint8 uData, Uint8 uMeta = 0)
	{
		if (m_nCount < t_nSize)
		{
			SNQueueElementT *pElement = &m_Elements[m_iTail];
			if (++m_iTail == t_nSize)
				m_iTail = 0;
			m_nCount++;

			pElement->uCycle = uCycle;
			pElement->uAddr  = uAddr;
			pElement->uData = uData;
			pElement->uPad  = uMeta;
			return TRUE;
		}

		return FALSE;
	}

	inline SNQueueElementT *Dequeue(Uint32 uCycle)
	{
		if (m_nCount > 0 && (uCycle > m_Elements[m_iHead].uCycle))
		{
			SNQueueElementT *pElement = &m_Elements[m_iHead];
			if (++m_iHead == t_nSize)
				m_iHead = 0;
			m_nCount--;
			return pElement;
		}
		return NULL;
 	}

	inline SNQueueElementT *Dequeue()
	{
		if (m_nCount > 0)
		{
			SNQueueElementT *pElement = &m_Elements[m_iHead];
			if (++m_iHead == t_nSize)
				m_iHead = 0;
			m_nCount--;
			return pElement;
		}
		return NULL;
	}

private:
    Int32			m_iHead;
    Int32			m_iTail;
	Int32			m_nCount;
	SNQueueElementT	m_Elements[t_nSize];
};

typedef SNQueueT<SNQUEUE_SIZE> SNQueue;
typedef SNQueueT<SNPPU_QUEUE_SIZE> SNPPUQueue;

#endif
