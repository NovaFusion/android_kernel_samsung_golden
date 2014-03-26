/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
 
/*!
 * \brief Component Manager Macros.
 *
 * \defgroup CM_MACROS NMF Macros (ANSI C99)
 * The Component Manager Macros are provided to ease FromHost interface call and ToHost callback definition.
 * \attention <b>These macros are only ANSI C99 compliant</b> (ARM RVCT 2.x/3.x, GNU gcc 4.x, ...)
 * \ingroup CM_USER_API
 */

#ifndef __INC_CM_MACROS_H
#define __INC_CM_MACROS_H

/*
 * The next macros are supported only with C Ansi 99, so....
 */

/*
 * The Symbian environment dependency, computation which uses an old gnu cpp,
 * does not accept "..." parameters.
 * However the actual compiler (armcc) does.
 * So remove the macro definitions when computing dependencies.
 */
#if ( defined(__CC_ARM) && !defined(__STRICT_ANSI__) ) || !defined(__SYMBIAN32__)

/*
 * Only for skilled eyes ;)
 * The following macros are used to implement NMFCALL[VOID] and NMFMETH[VOID] macros in an elegant way
 */
#define WITH_PARAM(...)  __VA_ARGS__)
#define WITH_NOPARAM(...) )

/*!
 * \brief Macro to ease Host to Dsp interface calling
 *
 * \attention <b>This macro is only ANSI C99 compliant</b>
 *
 * The <i>NMFCALL</i> macro can be used to call one method of any previously FromHost bounded interface.\n
 * From Host side, today, we have no way to mask the multi-instance handling, so
 * this macro is provided to ease FromHost interface calling and to avoid any mistake into the THIS parameter passing.
 *
 * So, any fromHost interface method call like: \code
 * itf.method(itf.THIS, param1, param2, ...);
 * \endcode
 * can be replaced by: \code
 * NMFCALL(itf, method)(param1, param2, ...);
 * \endcode
 *
 * \warning Don't forget to use NMFCALLVOID macro when declaring a FromHost interface method having none application parameter,
 * else it will lead to erroneous C code expansion
 * \see NMFCALLVOID
 * \hideinitializer
 * \ingroup CM_MACROS
 */
#define NMFCALL(itfHandle, itfMethodName)  \
    (itfHandle).itfMethodName((itfHandle).THIS, WITH_PARAM

/*!
 * \brief Macro to ease Host to Dsp interface calling (method without any user parameter)
 *
 * \attention <b>This macro is only ANSI C99 compliant</b>
 *
 * The <i>NMFCALLVOID</i> macro can be used to call one method (those without any user parameter) of any previously FromHost bounded interface.\n
 * From Host side, today, we have no way to mask the multi-instance handling, so
 * this macro is provided to ease FromHost interface calling and to avoid any mistake into the THIS parameter passing.
 *
 * So, any FromHost interface method call without any application parameter like:\code
 * itf.method(itf.THIS);
 * \endcode
 * can be replaced by: \code
 * NMFCALLVOID(itf, method)();
 * \endcode
 * \see NMFCALL
 * \hideinitializer
 * \ingroup CM_MACROS
 */
#define NMFCALLVOID(itfHandle, itfMethodName)  \
    (itfHandle).itfMethodName((itfHandle).THIS WITH_NOPARAM

/*!
 * \brief Macro to ease Dsp to Host interface method declaration
 *
 * \attention <b>This macro definition is only ANSI C99 compliant</b>
 *
 * The <i>NMFMETH</i> macro can be used to ease the ToHost interface method declaration.\n
 * From Host side, today, we have no way to mask the multi-intance handling, so the user shall handle it by hand
 * by passing the "component" context as first parameter of each ToHost interface method through the void *THIS parameter.
 * This macro could avoid any mistake into the THIS parameter declaration when never used by the user code.
 *
 * So, any ToHost interface method declaration like:\code
 * void mynotify(void *THIS, mytype1 myparam1, mytype2 myparam2, ...) {
 * <body of the interface routine>
 * }
 * \endcode
 * can be replaced by: \code
 * void NMFMETH(mynotify)(mytype1 myparam1, mytype2 myparam2, ...) {
 * <body of the interface routine>
 * }
 * \endcode
 *
 * \warning Don't forget to use NMFMETHVOID macro when declaring a ToHost interface method having none application parameter,
 * else it will lead to erroneous C code expansion
 *
 * \see NMFMETHVOID
 * \hideinitializer
 * \ingroup CM_MACROS
 */
#define NMFMETH(itfMethodName) \
    itfMethodName(void *THIS, WITH_PARAM

/*!
 * \brief Macro to ease Dsp to Host interface method declaration (method without any user parameter)
 *
 * \attention <b>This macro is only ANSI C99 compliant</b>
 *
 * The <i>NMFMETHVOID</i> macro can be used to ease the ToHost interface method (those without any user parameter) declaration.\n
 * From Host side, today, we have no way to mask the multi-intance handling, so the user shall handle it by hand
 * by passing the "component" context as first parameter of each ToHost interface method through the void *THIS parameter.
 * This macro could avoid any mistake into the THIS parameter declaration when never used by the user code.
 *
 * So, any ToHost interface method declaration having none application parameter like:\code
 * void mynotify(void *THIS) {
 * <body of the interface routine>
 * }
 * \endcode
 * can be replaced by: \code
 * void NMFMETHVOID(mynotify)(void) {
 * <body of the interface routine>
 * }
 * \endcode
 *
 * \see NMFMETH
 * \hideinitializer
 * \ingroup CM_MACROS
 */
#define NMFMETHVOID(itfMethodName) \
    itfMethodName(void *THIS WITH_NOPARAM

#endif /* not Symbian environment or compiling with ARMCC and not in strict ANSI */

#endif /* __INC_CM_MACROS_H */

