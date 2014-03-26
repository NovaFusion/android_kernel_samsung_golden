/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*
 *
 */
#include <cm/engine/elf/inc/mmdsp-loadmap.h>
#include <cm/engine/elf/inc/mmdsp.h>
#include <cm/engine/dsp/inc/semaphores_dsp.h>
#include <cm/engine/dsp/mmdsp/inc/mmdsp_hwp.h>
#include <cm/engine/os_adaptation_layer/inc/os_adaptation_layer.h>

#include <cm/engine/power_mgt/inc/power.h>

#include <cm/engine/utils/inc/string.h>
#include <cm/engine/trace/inc/trace.h>
#include <cm/engine/memory/inc/domain.h>
#include <cm/engine/component/inc/instance.h>
#include <cm/engine/component/inc/component_type.h>
#include <inc/nmf-limits.h>

#define LOADMAP_SEMAPHORE_USE_NB                        7

static t_memory_handle headerHandle[NB_CORE_IDS] = {INVALID_MEMORY_HANDLE, };
static struct LoadMapHdr *headerAddresses[NB_CORE_IDS] = {0, };
static t_uint32 headerOffsets[NB_CORE_IDS] = {0, };
static t_uint32 entryNumber[NB_CORE_IDS] = {0, };

#undef myoffsetof
#define myoffsetof(TYPE, MEMBER) ((unsigned int) &((TYPE *)0)->MEMBER)

t_cm_error cm_DSPABI_AddLoadMap(
        t_cm_domain_id domainId,
        const char* templateName,
        const char* localname,
        t_memory_handle *memories,
        void *componentHandle)
{
    t_nmf_core_id coreId = cm_DM_GetDomainCoreId(domainId);
    int count=0;
    struct LoadMapItem* curItem = NULL;

    if (headerHandle[coreId] == 0) /* Create loadmap header */
    {
        headerHandle[coreId] = cm_DM_Alloc(domainId, SDRAM_EXT16,
                                           sizeof(struct LoadMapHdr)/2, CM_MM_ALIGN_2WORDS, TRUE);
        if (headerHandle[coreId] == INVALID_MEMORY_HANDLE) {
            ERROR("CM_NO_MORE_MEMORY: Unable to allocate loadmap in cm_DSPABI_AddLoadMap()\n", 0, 0, 0, 0, 0, 0);
            return CM_NO_MORE_MEMORY;
        }

        headerAddresses[coreId] = (struct LoadMapHdr*)cm_DSP_GetHostLogicalAddress(headerHandle[coreId]);

        headerAddresses[coreId]->nMagicNumber = LOADMAP_MAGIC_NUMBER;
        headerAddresses[coreId]->nVersion = (LOADMAP_VERSION_MSB<<8)|(LOADMAP_VERSION_LSB);
        headerAddresses[coreId]->nRevision = 0;
        headerAddresses[coreId]->pFirstItem = 0;

        //Register Header into XRAM:2
        cm_DSP_GetDspAddress(headerHandle[coreId], &headerOffsets[coreId]);
        cm_DSP_WriteXRamWord(coreId, 2, headerOffsets[coreId]);
    }

    // update Header nRevision field
    headerAddresses[coreId]->nRevision++;

    /*
     * Build loadmap entry
     */
    {
        t_memory_handle handle;
        struct LoadMapItem* pItem;
        t_uint32 dspentry;
        unsigned char* pos;
        t_uint32 fnlen, lnlen;
        t_uint32 fnlenaligned, lnlenaligned;
        t_uint32 address;
        t_uint32 postStringLength;
        int i;

        postStringLength = cm_StringLength(".elf", 16);
        fnlenaligned = fnlen = cm_StringLength(templateName, MAX_COMPONENT_FILE_PATH_LENGTH) + postStringLength + 2;
        if((fnlenaligned % 2) != 0) fnlenaligned++;
        lnlenaligned = lnlen = cm_StringLength(localname, MAX_TEMPLATE_NAME_LENGTH);
        if((lnlenaligned % 2) != 0) lnlenaligned++;

        // Allocate new loap map
        handle = cm_DM_Alloc(domainId, SDRAM_EXT16,
                             sizeof(struct LoadMapItem)/2 + (1 + fnlenaligned/2) + (1 + lnlenaligned/2),
                             CM_MM_ALIGN_2WORDS, TRUE);
        if (handle == INVALID_MEMORY_HANDLE) {
            ERROR("CM_NO_MORE_MEMORY: Unable to allocate loadmap entry in cm_DSPABI_AddLoadMap\n", 0, 0, 0, 0, 0, 0);
            return CM_NO_MORE_MEMORY;
        }

        pItem = (struct LoadMapItem*)cm_DSP_GetHostLogicalAddress(handle);
        cm_DSP_GetDspAddress(handle, &dspentry);
	count++;
	entryNumber[coreId]++;

        // Link this new loadmap with the previous one
        if(headerAddresses[coreId]->pFirstItem == NULL)
            headerAddresses[coreId]->pFirstItem = (struct LoadMapItem *)dspentry;
        else
        {
            const t_dsp_desc* pDspDesc = cm_DSP_GetState(coreId);
            t_uint32 endSegmentAddr = SDRAMMEM16_BASE_ADDR + pDspDesc->segments[SDRAM_DATA_USER].size / 2;
            struct LoadMapItem* curItem, *prevItem = NULL;
            t_uint32  curItemDspAdress;

            if(
                    ((t_uint32)headerAddresses[coreId]->pFirstItem < SDRAMMEM16_BASE_ADDR) ||
                    ((t_uint32)headerAddresses[coreId]->pFirstItem > endSegmentAddr))
            {
                ERROR("Memory corruption in MMDSP: at data DSP address=%x or ARM address=%x\n",
                        headerOffsets[coreId], &headerAddresses[coreId]->pFirstItem, 0, 0, 0, 0);

                return CM_INVALID_DATA;
            }
            curItemDspAdress = (t_uint32)headerAddresses[coreId]->pFirstItem;
            curItem = (struct LoadMapItem*)((curItemDspAdress - headerOffsets[coreId]) * 2 + (t_uint32)headerAddresses[coreId]); // To ARM address
            count++;
            while(curItem->pNextItem != NULL)
            {
                if(((t_uint32)curItem->pNextItem < SDRAMMEM16_BASE_ADDR) || ((t_uint32)curItem->pNextItem > endSegmentAddr))
                {
			if (prevItem == NULL)
				ERROR("AddLoadMap: Memory corruption in MMDSP: at data DSP address=%x or ARM address=%x\n",
				      curItemDspAdress + myoffsetof(struct LoadMapItem, pNextItem), &curItem->pNextItem,
				      0, 0, 0, 0);
			else
				ERROR("AddLoadMap: Memory corruption in MMDSP: at data DSP address=%x or ARM address=%x\n",
				      curItemDspAdress + myoffsetof(struct LoadMapItem, pNextItem), &curItem->pNextItem,
				      0, 0, 0, 0);
			return CM_INVALID_DATA;
                }
                curItemDspAdress = (t_uint32)curItem->pNextItem;
		prevItem = curItem;
                curItem = (struct LoadMapItem*)((curItemDspAdress - headerOffsets[coreId]) * 2 + (t_uint32)headerAddresses[coreId]); // To ARM address
		count++;
            }
            curItem->pNextItem = (struct LoadMapItem *)dspentry;
        }

        // DSP Address of the string at the end of the load map
        pos = (unsigned char*)pItem + sizeof(struct LoadMapItem);

        /*
         * Set SolibFilename address information
         *   ->  string = "./origfilename"
         */
        pItem->pSolibFilename = (char*)(dspentry + sizeof(struct LoadMapItem) / 2);
        *(t_uint16*)pos = fnlen;
        pos += 2;
        *pos++ = '.';
        *pos++ = '\\';
        for(i = 0; i < fnlen - 2 - postStringLength; i++)
        {
            *pos++ = (templateName[i] == '.') ? '\\' : templateName[i];
        }
        *pos++ = '.';
        *pos++ = 'e';
        *pos++ = 'l';
        *pos++ = 'f';
        // add padding if needed
        if ((t_uint32)pos & 1)
            *pos++ = '\0';

        /*
         * Set Component Name  address information
         */
        if (lnlen != 0)
        {
            pItem->pComponentName = (char*)(dspentry + sizeof(struct LoadMapItem) / 2 + 1 + fnlenaligned / 2);

            *(t_uint16*)pos = lnlen;
            pos += 2;
            for(i = 0; i < lnlenaligned; i++)
            {
                // If not aligned null ending copied
                *pos++ = localname[i];
            }
        }
        else
        {
            pItem->pComponentName = 0;
        }

        /*
         * Set PROG information
         */
        if(memories[CODE_MEMORY_INDEX] == INVALID_MEMORY_HANDLE)
            address = 0;
        else
            cm_DSP_GetDspAddress(memories[CODE_MEMORY_INDEX], &address);
        pItem->pAddrProg = (void*)address;

        /*
         * Set ERAMCODE information
         */
        if(memories[ECODE_MEMORY_INDEX] == INVALID_MEMORY_HANDLE)
            address = 0;
        else
            cm_DSP_GetDspAddress(memories[ECODE_MEMORY_INDEX], &address);
        pItem->pAddrEmbProg = (void*)address;

        /*
         * Set THIS information
         */
        if(memories[PRIVATE_DATA_MEMORY_INDEX] != INVALID_MEMORY_HANDLE) {
            // Standard component
            cm_DSP_GetDspAddress(memories[PRIVATE_DATA_MEMORY_INDEX], &address);
        } else if(memories[SHARE_DATA_MEMORY_INDEX] != INVALID_MEMORY_HANDLE) {
            // Singleton component where data are shared (simulate THIS with shared memory)
            cm_DSP_GetDspAddress(memories[SHARE_DATA_MEMORY_INDEX], &address);
        } else {
            // Component without data (take unique identifier -> arbitrary take host component handle)
		address = (t_uint32)componentHandle;
        }
        pItem->pThis = (void*)address;

        /*
         * Set ARM THIS information
         */
        pItem->pARMThis = componentHandle;

        /*
         * Set Link to null (end of list)
         */
        pItem->pNextItem = 0;

        /*
         * Set XROM information
         */
        if(memories[XROM_MEMORY_INDEX] == INVALID_MEMORY_HANDLE)
            address = 0;
        else
            cm_DSP_GetDspAddress(memories[XROM_MEMORY_INDEX], &address);
        pItem->pXROM = (void*)address;

        /*
         * Set YROM information
         */
        if(memories[YROM_MEMORY_INDEX] == INVALID_MEMORY_HANDLE)
            address = 0;
        else
            cm_DSP_GetDspAddress(memories[YROM_MEMORY_INDEX], &address);
        pItem->pYROM = (void*)address;

        /*
         * Set memory handle (not used externally)
         */
        ((t_component_instance *)componentHandle)->loadMapHandle = handle;
    }

    OSAL_mb();

    if (count != entryNumber[coreId]) {
	    ERROR("AddLoadMap: corrumption, number of component differs: count=%d, expected %d (last item @ %p)\n",
		  count, entryNumber[coreId], curItem, 0, 0, 0);
	    return CM_INVALID_DATA;
    }
    return CM_OK;
}

t_cm_error cm_DSPABI_RemoveLoadMap(
        t_cm_domain_id domainId,
        const char* templateName,
        t_memory_handle *memories,
        const char* localname,
        void *componentHandle)
{
    struct LoadMapItem **prevItemReference;
    t_uint32  prevItemReferenceDspAddress, curItemDspAdress;
    t_nmf_core_id coreId = cm_DM_GetDomainCoreId(domainId);
    const t_dsp_desc* pDspDesc = cm_DSP_GetState(coreId);
    t_uint32 endSegmentAddr = SDRAMMEM16_BASE_ADDR + pDspDesc->segments[SDRAM_DATA_USER].size / 2;
    struct LoadMapItem* curItem = NULL;

    CM_ASSERT (headerHandle[coreId] != INVALID_MEMORY_HANDLE);

    /* parse list until we find this */
    prevItemReferenceDspAddress = 0x2;                          // DSP address of load map head pointer
    prevItemReference = &headerAddresses[coreId]->pFirstItem;
    curItemDspAdress = (t_uint32)*prevItemReference;
    while(curItemDspAdress != 0x0)
    {
        if((curItemDspAdress < SDRAMMEM16_BASE_ADDR) || (curItemDspAdress > endSegmentAddr))
        {
            ERROR("Memory corruption in MMDSP: at data DSP address=%x or ARM address=%x\n",
                    prevItemReferenceDspAddress, prevItemReference, 0, 0, 0, 0);

	    /* free the entry anyway to avoid leakage */
            cm_DM_Free(((t_component_instance *)componentHandle)->loadMapHandle, TRUE);

	    return CM_OK;
        }

        curItem = (struct LoadMapItem*)((curItemDspAdress - headerOffsets[coreId]) * 2 + (t_uint32)headerAddresses[coreId]); // To ARM address

        if(curItem->pARMThis == componentHandle)
        {
            // Remove component from loadmap

            /* take local semaphore */
            cm_DSP_SEM_Take(coreId,LOADMAP_SEMAPHORE_USE_NB);

            /* remove element from list */
            *prevItemReference = curItem->pNextItem;

            /* update nRevision field in header */
            headerAddresses[coreId]->nRevision++;

            /* If this is the last item, deallocate !!! */
            if(headerAddresses[coreId]->pFirstItem == NULL)
            {
                // Deallocate memory
                cm_DM_Free(headerHandle[coreId], TRUE);
                headerHandle[coreId] = INVALID_MEMORY_HANDLE;

                //Register Header into XRAM:2
                cm_DSP_WriteXRamWord(coreId, 2, 0);
            }

            /* deallocate memory */
            cm_DM_Free(((t_component_instance *)componentHandle)->loadMapHandle, TRUE);

            /* be sure memory is updated before releasing local semaphore */
            OSAL_mb();

            /* release local semaphore */
            cm_DSP_SEM_Give(coreId,LOADMAP_SEMAPHORE_USE_NB);

	    entryNumber[coreId]--;

            return CM_OK;
        }

        prevItemReferenceDspAddress = curItemDspAdress + myoffsetof(struct LoadMapItem, pNextItem);
        prevItemReference = &curItem->pNextItem;
        curItemDspAdress = (t_uint32)*prevItemReference;
    };

    ERROR("Memory corruption in MMDSP: component not in LoadMap %s\n", localname, 0, 0, 0, 0, 0);

    /* free the entry anyway to avoid leakage */
    cm_DM_Free(((t_component_instance *)componentHandle)->loadMapHandle, TRUE);

    return CM_OK;
}

#if 0
t_cm_error cm_DSPABI_CheckLoadMap_nolock(t_nmf_core_id coreId)
{
    int count=0;
    static int dump = 5;
    struct LoadMapItem* curItem = NULL;

    if (!dump)
            return CM_OK;
    if (headerHandle[coreId] == 0) /* No load map yet */
            return CM_OK;

    {
        // No entry in loadmap
        if(headerAddresses[coreId]->pFirstItem == NULL)
            return CM_OK;

        {
            const t_dsp_desc* pDspDesc = cm_DSP_GetState(coreId);
            t_uint32 endSegmentAddr = SDRAMMEM16_BASE_ADDR + pDspDesc->segments[SDRAM_DATA_USER].size / 2;
            struct LoadMapItem *prevItem=NULL;
            t_uint32  curItemDspAdress;

            if (((t_uint32)headerAddresses[coreId]->pFirstItem < SDRAMMEM16_BASE_ADDR) ||
		((t_uint32)headerAddresses[coreId]->pFirstItem > endSegmentAddr))
            {
                ERROR("CheckLoadMap: Memory corruption in MMDSP at first item: at data DSP address=%x or ARM address=%x\n",
                      headerOffsets[coreId], &headerAddresses[coreId]->pFirstItem, 0, 0, 0, 0);
                dump--;
                return CM_INVALID_COMPONENT_HANDLE;
            }
            curItemDspAdress = (t_uint32)headerAddresses[coreId]->pFirstItem;
            curItem = (struct LoadMapItem*)((curItemDspAdress - headerOffsets[coreId]) * 2 + (t_uint32)headerAddresses[coreId]);
	    count++;
            while(curItem->pNextItem != NULL)
            {
                if(((t_uint32)curItem->pNextItem < SDRAMMEM16_BASE_ADDR) || ((t_uint32)curItem->pNextItem > endSegmentAddr))
                {
                    if (!prevItem)
                        ERROR("CheckLoadMap: Memory corruption in MMDSP (count=%d): at data DSP address=%x or ARM address=%x\n"
                        "Previous (first) component name %s<%s>\n",
                        count,
                        curItemDspAdress + myoffsetof(struct LoadMapItem, pNextItem), &curItem->pNextItem,
                        (char*)(((t_component_instance *)&curItem->pARMThis)->pathname),
                        (char*)(((t_component_instance *)&curItem->pARMThis)->Template->name), 0);
                    else
                        ERROR("CheckLoadMap: Memory corruption in MMDSP (count=%d): at data DSP address=%x or ARM address=%x\n"
                        "Previous valid component name %s<%s>",
                        count,
                        curItemDspAdress + myoffsetof(struct LoadMapItem, pNextItem), &curItem->pNextItem,
                        (char*)(((t_component_instance *)&prevItem->pARMThis)->pathname),
                        (char*)(((t_component_instance *)&prevItem->pARMThis)->Template->name), 0);
                    dump--;
                return CM_INVALID_COMPONENT_HANDLE;
                }
                curItemDspAdress = (t_uint32)curItem->pNextItem;
                prevItem = curItem;
                curItem = (struct LoadMapItem*)((curItemDspAdress - headerOffsets[coreId]) * 2 + (t_uint32)headerAddresses[coreId]); // To ARM address
		count++;
            }
        }

    }

    if (count != entryNumber[coreId]) {
	    ERROR("CheckLoadMap: number of component differs: count=%d, expected %d (last item @ %p)\n", count, entryNumber[coreId],
		  curItem, 0, 0, 0);
	    dump--;
	    return CM_INVALID_COMPONENT_HANDLE;
    }
    return CM_OK;
}

t_cm_error cm_DSPABI_CheckLoadMap(t_nmf_core_id coreId)
{
	t_cm_error error;
	OSAL_LOCK_API();
	error = cm_DSPABI_CheckLoadMap_nolock(coreId);
	OSAL_UNLOCK_API();
	return error;
}
#endif
