#include "android/GotHook.h"

#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

namespace coop::android {

namespace {

#if defined(__aarch64__)
constexpr unsigned kJumpSlot = 1026, kGlobDat = 1025; // R_AARCH64_JUMP_SLOT / GLOB_DAT
#elif defined(__arm__)
constexpr unsigned kJumpSlot = 22, kGlobDat = 21;     // R_ARM_JUMP_SLOT / GLOB_DAT
#elif defined(__x86_64__)
constexpr unsigned kJumpSlot = 7, kGlobDat = 6;       // R_X86_64_* (emulador / testes em host)
#elif defined(__i386__)
constexpr unsigned kJumpSlot = 7, kGlobDat = 6;       // R_386_*
#else
#error "arquitetura nao suportada"
#endif

#if defined(__LP64__)
using Rel = ElfW(Rela);
constexpr bool kUsesRela = true;
inline unsigned RelType(const Rel& r) { return static_cast<unsigned>(ELF64_R_TYPE(r.r_info)); }
inline unsigned RelSym(const Rel& r) { return static_cast<unsigned>(ELF64_R_SYM(r.r_info)); }
#else
using Rel = ElfW(Rel);
constexpr bool kUsesRela = false;
inline unsigned RelType(const Rel& r) { return ELF32_R_TYPE(r.r_info); }
inline unsigned RelSym(const Rel& r) { return ELF32_R_SYM(r.r_info); }
#endif

struct Request {
    const char* suffix;
    const char* symbol;
    void* replacement;
    void** original;
    int patched;
};

bool EndsWith(const char* s, const char* suffix) {
    size_t a = std::strlen(s), b = std::strlen(suffix);
    return a >= b && std::strcmp(s + a - b, suffix) == 0;
}

void PatchTable(Request& rq, uintptr_t bias, const Rel* rels, size_t count, const ElfW(Sym)* symtab,
                const char* strtab) {
    long page = sysconf(_SC_PAGESIZE);
    for (size_t i = 0; i < count; ++i) {
        unsigned type = RelType(rels[i]);
        if (type != kJumpSlot && type != kGlobDat) continue;
        const char* name = strtab + symtab[RelSym(rels[i])].st_name;
        if (std::strcmp(name, rq.symbol) != 0) continue;

        auto* slot = reinterpret_cast<void**>(bias + rels[i].r_offset);
        auto pageStart = reinterpret_cast<uintptr_t>(slot) & ~static_cast<uintptr_t>(page - 1);
        // A GOT costuma estar em RELRO (somente leitura apos o carregamento).
        if (mprotect(reinterpret_cast<void*>(pageStart), static_cast<size_t>(page), PROT_READ | PROT_WRITE) != 0)
            continue;
        if (*slot != rq.replacement) {
            if (rq.original && !*rq.original) *rq.original = *slot;
            *slot = rq.replacement;
        }
        ++rq.patched;
    }
}

int Callback(dl_phdr_info* info, size_t, void* data) {
    auto& rq = *static_cast<Request*>(data);
    if (!info->dlpi_name || !EndsWith(info->dlpi_name, rq.suffix)) return 0;

    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dyn = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dyn) return 0;

    // No bionic os ponteiros da secao dinamica sao deslocamentos a partir do
    // endereco de carga; a glibc ja os converte para absolutos.
    uintptr_t bias = info->dlpi_addr;
#if defined(__ANDROID__)
    uintptr_t ptrBias = bias;
#else
    uintptr_t ptrBias = 0;
#endif
    const ElfW(Sym)* symtab = nullptr;
    const char* strtab = nullptr;
    const Rel *jmprel = nullptr, *rel = nullptr;
    size_t jmprelSize = 0, relSize = 0;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
        case DT_SYMTAB: symtab = reinterpret_cast<const ElfW(Sym)*>(ptrBias + dyn->d_un.d_ptr); break;
        case DT_STRTAB: strtab = reinterpret_cast<const char*>(ptrBias + dyn->d_un.d_ptr); break;
        case DT_JMPREL: jmprel = reinterpret_cast<const Rel*>(ptrBias + dyn->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmprelSize = dyn->d_un.d_val; break;
        case DT_RELA: if (kUsesRela) rel = reinterpret_cast<const Rel*>(ptrBias + dyn->d_un.d_ptr); break;
        case DT_RELASZ: if (kUsesRela) relSize = dyn->d_un.d_val; break;
        case DT_REL: if (!kUsesRela) rel = reinterpret_cast<const Rel*>(ptrBias + dyn->d_un.d_ptr); break;
        case DT_RELSZ: if (!kUsesRela) relSize = dyn->d_un.d_val; break;
        default: break;
        }
    }
    if (!symtab || !strtab) return 0;
    if (jmprel) PatchTable(rq, bias, jmprel, jmprelSize / sizeof(Rel), symtab, strtab);
    if (rel) PatchTable(rq, bias, rel, relSize / sizeof(Rel), symtab, strtab);
    return 1; // biblioteca encontrada: encerra a iteracao
}

} // namespace

int HookImport(const char* libraryNameSuffix, const char* symbol, void* replacement, void** original) {
    Request rq{libraryNameSuffix, symbol, replacement, original, 0};
    dl_iterate_phdr(&Callback, &rq);
    return rq.patched;
}

} // namespace coop::android
