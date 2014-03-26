/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \brief OS Adaptation Layer API
 *
 * \defgroup CM_ENGINE_OSAL_API CM Engine OSAL (Operating System Abstraction Layer) API
 * \ingroup CM_ENGINE_MODULE
 */
#ifndef __INC_CM_OSAL_H
#define __INC_CM_OSAL_H

#include <cm/inc/cm_type.h>
#include <cm/engine/communication/inc/communication_type.h>
#include <cm/engine/component/inc/instance.h>

/*!
 * \brief Identifier of a trace channel (id in [0..255])
 * \ingroup CM_ENGINE_OSAL_API
 */
typedef t_uint8 t_nmf_trace_channel;

/*!
 * \brief Identifier of lock create by OSAL
 * \ingroup CM_ENGINE_OSAL_API
 */
typedef t_uint32 t_nmf_osal_sync_handle;

/*!
 * \brief Identifier of semaphore create by OSAL
 * \ingroup CM_ENGINE_OSAL_API
 */
typedef t_uint32 t_nmf_osal_sem_handle;

/*!
 * \brief Identifier of semaphore wait error return by semaphore OSAL API
 * \ingroup CM_ENGINE_OSAL_API
 */
typedef t_uint8 t_nmf_osal_sync_error;
#define SYNC_ERROR_TIMEOUT      ((t_nmf_osal_sync_error)-1)
#define SYNC_OK                 ((t_nmf_osal_sync_error)0)
#define SEM_TIMEOUT_NORMAL      3000
#define SEM_TIMEOUT_DEBUG       300000

/*!
 * \brief Operations used to support additionnal OS-specific debug feature
 * \ingroup CM_ENGINE_OSAL_API
 */
struct osal_debug_operations {
	void (*component_create)(t_component_instance *component);
	void (*component_destroy)(t_component_instance *component);
	void (*domain_create)(t_cm_domain_id id);
	void (*domain_destroy)(t_cm_domain_id id);
};

extern struct osal_debug_operations osal_debug_ops;

/*!
 * \brief Description of the Scheduling part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Support of uplink communication path (from Media Processors to Host (ARM))
 *
 * Post a function call outside of Host CPU Interrupt mode in order to minimize ISR execution time
 * \param[in] upLayerTHIS : this one provided by user when calling CM_ENGINE_BindComponentToCMCore() (first field of the interface context) \n
 * \param[in] methodIndex : index method to be called  \n
 * \param[in] anyPtr : internal NMF marshaled parameters block (to be passed as second parameter when calling the previous pSkeleton method) \n
 * \param[in] ptrSize : size of anyPtr in bytes \n
 *
 * Called by:
 *  - CM_ProcessMpcEvent() call (shall be bound by OS integrator to HSEM IRQ)
 *
 * \ingroup CM_ENGINE_OSAL_API
 */

PUBLIC void OSAL_PostDfc(
        t_nmf_mpc2host_handle   upLayerTHIS,
        t_uint32                    methodIndex,
        t_event_params_handle       anyPtr,
        t_uint32                    ptrSize);


/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to protect global variable against multiple call. Interrupt and scheduler function are use when
 *       we take hardware/local semaphore. Scheduler lock functions can have empty implementation but this may
 *       impact performance (dsp waiting semaphore because host thread was preempted whereas it has already take semaphore
 *       but not yet release it).
 *
 * \return handle of the Mutex created
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC t_nmf_osal_sync_handle OSAL_CreateLock(void);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to protect global variable against multiple call. Interrupt and scheduler function are use when
 *       we take hardware/local semaphore. Scheduler lock functions can have empty implementation but this may
 *       impact performance (dsp waiting semaphore because host thread was preempted whereas it has already take semaphore
 *       but not yet release it).
 *
 * \param[in] handle handle of the Mutex to be locked
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_Lock(
        t_nmf_osal_sync_handle handle);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to protect global variable against multiple call. Interrupt and scheduler function are use when
 *       we take hardware/local semaphore. Scheduler lock functions can have empty implementation but this may
 *       impact performance (dsp waiting semaphore because host thread was preempted whereas it has already take semaphore
 *       but not yet release it).
 *
 * \param[in] handle handle of the Mutex to be unlocked
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_Unlock(
        t_nmf_osal_sync_handle handle);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to protect global variable against multiple call. Interrupt and scheduler function are use when
 *       we take hardware/local semaphore. Scheduler lock functions can have empty implementation but this may
 *       impact performance (dsp waiting semaphore because host thread was preempted whereas it has already take semaphore
 *       but not yet release it).
 *
 * \param[in] handle handle of the Mutex to be destroyed
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_DestroyLock(
        t_nmf_osal_sync_handle handle);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to allow to synchronize with code running on mpc side.
 *
 * \param[in] value : Initial value of semaphore.
 *
 * \return handle of the Semaphore created
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC t_nmf_osal_sem_handle OSAL_CreateSemaphore(
        t_uint32 value);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to allow to synchronize with code running on mpc side. This function can be call under
 *       Irq context by CM.
 *
 * \param[in] handle handle of the Semaphore for which we increase value and so potentially wake up thread.
 *
 * \param[in] aCtx is a hint to indicate to os that we are in a none normal context (e.g under interruption).
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_SemaphorePost(
        t_nmf_osal_sem_handle handle,
        t_uint8 aCtx);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to allow to synchronize with code running on mpc side.
 *
 * \param[in] handle of the Semaphore for which we decrease value and so potentially block current thread.
 *
 * \param[in] timeOutInMs maximun time in ms after which the block thread is wake up. In this case function return SYNC_ERROR_TIMEOUT value.
 *
 * \return error number: SYNC_ERROR_TIMEOUT in case semaphore is not release withing timeOutInMs.
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC t_nmf_osal_sync_error OSAL_SemaphoreWaitTimed(
        t_nmf_osal_sem_handle handle,
        t_uint32 timeOutInMs);

/*!
 * \brief Description of the Synchronization part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Use by CM to allow to synchronize with code running on mpc side.
 *
 * \param[in] handle handle of the Semaphore to be destroyed
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_DestroySemaphore(
        t_nmf_osal_sem_handle handle);

/*!
 * \brief Description of the System Memory Allocator part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Allocate CM some cacheable and bufferable memory (SDRAM) for internal usage \n
 * This memory will be accessed only by Host CPU (ARM)
 *
 * This function provide a simple, general-purpose  memory  allocation. The
 * OSAL_Alloc macro returns a pointer to a block of at least size bytes
 * suitably aligned for any use. If there  is  no  available  memory, this
 * function returns a null pointer.
 *
 * \param[in] size   size in bytes, of memory to be allocated
 * \return pointer on the beginning of the allocated memory
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void* OSAL_Alloc(
        t_cm_size size);

/*!
 * \brief Description of the System Memory Allocator part of the OS Adaptation Layer with memory set to zero
 *
 * Compare to \see OSAL_Alloc, same allocation is done but memory is set with zero before returning.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void* OSAL_Alloc_Zero(
        t_cm_size size);

/*!
 * \brief Description of the System Memory Allocator part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Free CM some cacheable and bufferable memory (SDRAM) for internal usage \n
 * This memory will be accessed only by Host CPU (ARM)
 *
 * \param[in] pHandle  pointer on the begining of the memory previously allocated
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void  OSAL_Free(
        void *pHandle);

/*!
 * \brief Clean data cache in DDR in order to be accessible from peripheral.
 *
 * This method must be synchronized with MMDSP Code cache attribute.
 *   Strongly Ordered -> nothing
 *   Shared device -> dsb + L2 Write buffer drain
 *   Non cacheable, Bufferable -> dsb + L2 Write buffer drain
 *   WT or WB -> Flush cache range + dsb + L2 Write buffer drain
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_CleanDCache(
        t_uint32                startAddr,          //!< [in] Start data address of range to clean
        t_uint32                Size                //!< [in] Size of range to clean
        );

/*!
 * \brief Flush write buffer.
 *
 * This method must be synchronized with MMDSP Data cache attribute.
 *   Strongly Ordered -> nothing
 *   Shared device -> dsb + L2 Write buffer drain
 *   Non cacheable, Bufferable -> dsb + L2 Write buffer drain
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_mb(void);

/*!
 * \brief Description of the System Memory part of the OS Adaptation Layer
 *
 * <B>Goal:</B> Copy some cacheable and bufferable memory (SDRAM) provided by a client to\n
 * internal memory.
 *
 * \param[in] dst  : pointer on the begining of the internal memory previously allocated
 * \param[in] src  : pointer on the begining of the client's memory
 * \param[in] size : The size of the data to copy
 *
 * Called by:
 *  - CM_ENGINE_PushComponent()
 *
 * \note This API is mainly provided for the OS were the client application does execute in the same
 *       address space as the CM.
 *       For example in Linux or Symbian, the client's address space is userland but the CM execute in
 *       kernel space. Thus, 'dst' is supposed to be a kernel address but src is supposed to be a user
 *       space address
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC t_cm_error OSAL_Copy(
        void *dst,
	const void *src,
	t_cm_size size);

/*!
 * \brief Description of the internal log traces configuration of the Component Manager
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_Log(
        const char *format,
        int param1,
        int param2,
        int param3,
        int param4,
        int param5,
        int param6);

/*!
 * \brief Generate an OS-Panic. Called in from CM_ASSERT().
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_Panic(void);

/*!
 * \brief Description of the configuration of the trace features
 *
 * (trace output itself is provided by user through his custom implementation of the generic APIs)
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_Write64(
        t_nmf_trace_channel channel,
        t_uint8 isTimestamped,
        t_uint64 value);

/*!
 * \brief Power enabling/disabling commands description.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
typedef enum
{
    CM_OSAL_POWER_SxA_CLOCK,     //!< SxA Power & Clock, firstParam contains Core ID
    CM_OSAL_POWER_SxA_AUTOIDLE,  //!< SxA AutoIdle, firstParam contains Core ID
    CM_OSAL_POWER_SxA_HARDWARE,  //!< SxA Hardware Power, firstParam contains Core ID
    CM_OSAL_POWER_HSEM,          //!< HSEM Power
    CM_OSAL_POWER_SDRAM,         //!< SDRAM memory, firstParam contains physical resource address, secondParam contains size
    CM_OSAL_POWER_ESRAM          //!< ESRAM memory, firstParam contains physical resource address, secondParam contains size
} t_nmf_power_resource;

/*!
 * \brief Description of the Power Management part of the OS Adaptation Layer
 *
 * Use by CM engine to disable a logical power domain  (see \ref t_nmf_power_resource)
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_DisablePwrRessource(
        t_nmf_power_resource    resource,           //!< [in] Describe the domain which must be disabled
        t_uint32                firstParam,         //!< [in] Eventual first parameter to power to disable
        t_uint32                secondParam         //!< [in] Eventual second parameter to power to disable
        );

/*!
 * \brief Description of the Power Management part of the OS Adaptation Layer
 *
 * Use by CM engine to enable a logical power domain  (see \ref t_nmf_power_resource)
 *
 * \return
 *   - \ref CM_OK
 *   - \ref CM_PWR_NOT_AVAILABLE A specified power domain is not managed (see returned value in aPowerMask)
 *
 * Called by:
 *  - any CM API call
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC t_cm_error OSAL_EnablePwrRessource(
        t_nmf_power_resource    resource,           //!< [in] Describing the domains which must be enabled
        t_uint32                firstParam,         //!< [in] Eventual first parameter to power to disable
        t_uint32                secondParam         //!< [in] Eventual second parameter to power to disable
        );


/*!
 * \brief return prcmu timer value.
 *
 * This is need for perfmeter api  (see \ref t_nmf_power_resource)
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC t_uint64 OSAL_GetPrcmuTimer(void);

/*!
 * \brief Disable the service message handling (panic, etc)
 *
 * It must disable the handling of all service messages
 * If a service message is currently handled, it must wait till the end
 * of its managment before returning.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_DisableServiceMessages(void);

/*!
 * \brief Enable the service message handling (panic, etc)
 *
 * It enables the handling of all service messages
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_EnableServiceMessages(void);

/*!
 * \brief Generate 'software' panic due to dsp crash
 *
 * We request that the os part generate a panic to notify cm users
*  that a problem occur but not dsp panic has been sent (for example
*  a dsp crash)
 *
 * \param[in] t_nmf_core_id  : core_id is the id of dsp for which we need to generate a panic.
 * \param[in] reason  : additional information. Today only 0 is valid.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
PUBLIC void OSAL_GeneratePanic(t_nmf_core_id coreId, t_uint32 reason);

extern /*const*/ t_nmf_osal_sync_handle lockHandleApi;
extern /*const*/ t_nmf_osal_sync_handle lockHandleCom;
extern /*const*/ t_nmf_osal_sem_handle semHandle;

/*!
 * \brief Take a lock before entering critical section. Can suspend current thread if lock already taken. \n
 *        Use this macro in api function. For com function use OSAL_LOCK_COM.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
#define OSAL_LOCK_API() OSAL_Lock(lockHandleApi)

/*!
 * \brief Release lock before leaving critical section.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
#define OSAL_UNLOCK_API() OSAL_Unlock((lockHandleApi))

/*!
 * \brief Take a lock before entering critical section. Can suspend current thread if lock already taken. \n
 *        Use this macro in com function. For com function use OSAL_LOCK_API.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
#define OSAL_LOCK_COM() OSAL_Lock(lockHandleCom)

/*!
 * \brief Release lock before leaving critical section.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
#define OSAL_UNLOCK_COM() OSAL_Unlock((lockHandleCom))

/*!
 * \brief Go to sleep untill post done on semaphore or timeout expire. In that case SYNC_ERROR_TIMEOUT is return.
 *
 * \ingroup CM_ENGINE_OSAL_API
 */
#define OSAL_SEMAPHORE_WAIT_TIMEOUT(semHandle) OSAL_SemaphoreWaitTimed(semHandle, (cm_PWR_GetMode() == NORMAL_PWR_MODE)?SEM_TIMEOUT_NORMAL:SEM_TIMEOUT_DEBUG)

/****************/
/* Generic part */
/****************/
t_cm_error cm_OSAL_Init(void);
void cm_OSAL_Destroy(void);

#endif /* __INC_CM_OSAL_H */
