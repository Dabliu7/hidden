#pragma once
// Executa UM comando do SCM pelo handler nativo, sem nenhum script carregado:
// montamos o bytecode do comando num buffer, apontamos um CRunningScript
// "vazio" para ele e chamamos CRunningScript::ProcessOneCommand.
// Resultados (ex.: handle devolvido por CREATE_CHAR) saem nas variaveis locais
// desse script. So pode ser chamado na thread do jogo, com o mundo carregado.
#include <cstdint>
#include <cstring>

#include "win32/Addresses.h"

namespace coop::win32 {

class ScriptCommand {
public:
    explicit ScriptCommand(uint16_t opcode) { Raw(&opcode, 2); }

    ScriptCommand& Int(int32_t v)   { Byte(0x01); Raw(&v, 4); return *this; }
    ScriptCommand& Float(float v)   { Byte(0x06); Raw(&v, 4); return *this; }
    ScriptCommand& Out(uint16_t localIndex) { Byte(0x03); Raw(&localIndex, 2); return *this; }

    ScriptCommand& Run() {
        std::memset(thread_, 0, sizeof(thread_));
        const uint8_t* ip = code_;
        std::memcpy(thread_ + addr::CRunningScript_m_pBaseIP, &ip, sizeof(ip));
        std::memcpy(thread_ + addr::CRunningScript_m_pCurrentIP, &ip, sizeof(ip));
        // __thiscall sem argumentos de pilha == __fastcall(ecx, edx ignorado)
        reinterpret_cast<int8_t(__fastcall*)(void*, void*)>(addr::CRunningScript_ProcessOneCommand)(thread_, nullptr);
        return *this;
    }

    int32_t Result(uint16_t localIndex) const {
        int32_t v;
        std::memcpy(&v, thread_ + addr::CRunningScript_m_aLocalVars + 4 * localIndex, 4);
        return v;
    }

private:
    void Byte(uint8_t b) { Raw(&b, 1); }
    void Raw(const void* p, size_t n) {
        if (len_ + n <= sizeof(code_)) { std::memcpy(code_ + len_, p, n); len_ += n; }
    }
    uint8_t code_[64] = {};
    size_t  len_ = 0;
    uint8_t thread_[addr::CRunningScript_size] = {};
};

} // namespace coop::win32
