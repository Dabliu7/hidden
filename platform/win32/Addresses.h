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
constexpr uintptr_t CWorld_Players      = 0xB7CD98; // CPlayerInfo[2]; +0 = CPlayerPed*

// CPed / CPhysical / CPlaceable (x86)
constexpr uintptr_t CPlaceable_m_matrix   = 0x14;  // CMatrix*
constexpr uintptr_t CMatrix_pos           = 0x30;  // CVector
constexpr uintptr_t CPhysical_m_vecMove   = 0x44;  // CVector, metros por passo de 1/50 s
constexpr uintptr_t CPed_m_fHealth        = 0x540;
constexpr uintptr_t CPed_m_fArmour        = 0x548;
constexpr uintptr_t CPed_m_fCurrentRot    = 0x558; // heading em radianos

} // namespace coop::win32::addr
