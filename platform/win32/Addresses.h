#pragma once
// Enderecos do gta_sa.exe 1.0 US (o executavel "compact"/HOODLUM de
// 14.383.616 bytes, alvo de praticamente toda a documentacao publica).
// Fonte de verdade para conferir/expandir: projeto gta-reversed e plugin-sdk.
// NENHUM endereco e usado fora deste arquivo: o nucleo e agnostico.
#include <cstdint>

namespace coop::win32::addr {

// Assinatura de versao: valor lido em kVersionProbe10US identifica o 1.0 US.
constexpr uintptr_t kVersionProbe10US = 0x82457C;
constexpr uint32_t  kVersionProbeValue = 0x94BF;

constexpr uintptr_t CGame_Process       = 0x53BEE0; // void __cdecl CGame::Process()
constexpr uintptr_t CText_Get           = 0x6A0050; // const char* __thiscall CText::Get(const char* key)
constexpr uintptr_t CHud_SetHelpMessage = 0x588BE0; // void __cdecl (const char*, bool quick, bool permanent, bool brief)

// Front-end
constexpr uintptr_t CMenuManager_aScreens        = 0x8CE008; // 44 paginas de 0xE2 bytes
constexpr uintptr_t FrontEndMenuManager          = 0xBA6748;
constexpr uintptr_t CMenuManager_m_nCurrentRow   = 0x54;     // int: linha selecionada
constexpr uintptr_t CMenuManager_m_nCurrentPage  = 0x15D;    // uint8: pagina atual

// Scripts
constexpr uintptr_t CRunningScript_ProcessOneCommand = 0x469EB0; // int8 __thiscall ()
constexpr uintptr_t CRunningScript_size         = 0xE0;
constexpr uintptr_t CRunningScript_m_pBaseIP    = 0x10;
constexpr uintptr_t CRunningScript_m_pCurrentIP = 0x14;
constexpr uintptr_t CRunningScript_m_aLocalVars = 0x3C; // int[32]
constexpr uintptr_t CPools_GetPed               = 0x404910; // CPed* __cdecl (int handle)

constexpr uintptr_t CWorld_Players      = 0xB7CD98; // CPlayerInfo[2]; +0 = CPlayerPed*

// CPed / CPhysical / CPlaceable (x86)
constexpr uintptr_t CPlaceable_m_matrix   = 0x14;  // CMatrix*
constexpr uintptr_t CMatrix_pos           = 0x30;  // CVector
constexpr uintptr_t CPhysical_m_vecMove   = 0x44;  // CVector, metros por passo de 1/50 s
constexpr uintptr_t CPed_m_fHealth        = 0x540;
constexpr uintptr_t CPed_m_fArmour        = 0x548;
constexpr uintptr_t CPed_m_fCurrentRot    = 0x558; // heading em radianos
constexpr uintptr_t CPed_m_fAimingRot     = 0x55C; // heading alvo

} // namespace coop::win32::addr
