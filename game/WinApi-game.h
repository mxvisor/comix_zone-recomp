/*
 *  Comix Zone Win32 import shim - generated from the import table of COMIX.EXE
 *  by SR-SDK/tools/gen_import_shim.py. Generated: 2026-10-21
 */

#if !defined(_WINAPI_GAME_H_INCLUDED_)
#define _WINAPI_GAME_H_INCLUDED_

#include "platform.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CCALL WinHelpA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL CheckMenuItem_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL IsIconic_c(uint32_t a0);
uint32_t CCALL GetLastActivePopup_c(uint32_t a0);
uint32_t CCALL AdjustWindowRect_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL GetDesktopWindow_c(void);
uint32_t CCALL GetSubMenu_c(uint32_t a0, uint32_t a1);
uint32_t CCALL RemoveMenu_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL DeleteMenu_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL InsertMenuA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
uint32_t CCALL DrawMenuBar_c(uint32_t a0);
uint32_t CCALL LoadMenuA_c(uint32_t a0, uint32_t a1);
uint32_t CCALL SetMenu_c(uint32_t a0, uint32_t a1);
uint32_t CCALL GetMenu_c(uint32_t a0);
uint32_t CCALL DestroyMenu_c(uint32_t a0);
uint32_t CCALL EnableMenuItem_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL joyGetPos_c(uint32_t a0, uint32_t a1);
uint32_t CCALL midiOutGetNumDevs_c(void);
uint32_t CCALL waveOutGetNumDevs_c(void);
uint32_t CCALL AIL_sample_status_c(uint32_t a0);
uint32_t CCALL AIL_serve_c(void);
uint32_t CCALL AIL_resume_sample_c(uint32_t a0);
uint32_t CCALL AIL_resume_sequence_c(uint32_t a0);
uint32_t CCALL AIL_stop_sample_c(uint32_t a0);
uint32_t CCALL AIL_stop_sequence_c(uint32_t a0);
uint32_t CCALL AIL_end_sample_c(uint32_t a0);
uint32_t CCALL AIL_end_sequence_c(uint32_t a0);
uint32_t CCALL AIL_shutdown_c(void);
uint32_t CCALL AIL_startup_c(void);
uint32_t CCALL AIL_allocate_sequence_handle_c(uint32_t a0);
uint32_t CCALL AIL_allocate_sample_handle_c(uint32_t a0);
uint32_t CCALL AIL_init_sample_c(uint32_t a0);
uint32_t CCALL AIL_set_sample_type_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL AIL_set_sample_address_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL AIL_start_sample_c(uint32_t a0);
uint32_t CCALL AIL_waveOutOpen_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t CCALL AIL_sequence_status_c(uint32_t a0);
uint32_t CCALL MEM_free_lock_c(uint32_t a0);
uint32_t CCALL AIL_copy_to_MEM16_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL AIL_init_sequence_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL AIL_start_sequence_c(uint32_t a0);
uint32_t CCALL AIL_midiOutOpen_c(uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t CCALL MEM_alloc_lock_c(uint32_t a0);

#ifdef __cplusplus
}
#endif

#endif
