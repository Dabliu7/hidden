#pragma once
// Serializacao explicita little-endian, byte a byte. NUNCA fazer memcpy de
// structs do jogo para a rede: layout, padding e tamanho de ponteiro diferem
// entre x86 (PC) e ARMv7/ARM64 (Android).
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace coop {

class ByteWriter {
public:
    template <typename T>
    void Put(T v) {
        static_assert(std::is_integral<T>::value || std::is_enum<T>::value, "inteiros apenas");
        auto u = static_cast<uint64_t>(v);
        for (size_t i = 0; i < sizeof(T); ++i) buf_.push_back(static_cast<uint8_t>(u >> (8 * i)));
    }
    void PutF32(float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4); // IEEE-754 binary32 nas tres ABIs
        Put(u);
    }
    void PutBytes(const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void PutStr(const std::string& s) {
        Put(static_cast<uint8_t>(s.size() > 255 ? 255 : s.size()));
        PutBytes(s.data(), s.size() > 255 ? 255 : s.size());
    }
    const uint8_t* Data() const { return buf_.data(); }
    size_t Size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* p, size_t n) : p_(p), end_(p + n) {}

    template <typename T>
    T Get() {
        if (static_cast<size_t>(end_ - p_) < sizeof(T)) { ok_ = false; p_ = end_; return T{}; }
        uint64_t u = 0;
        for (size_t i = 0; i < sizeof(T); ++i) u |= static_cast<uint64_t>(p_[i]) << (8 * i);
        p_ += sizeof(T);
        return static_cast<T>(u);
    }
    float GetF32() {
        uint32_t u = Get<uint32_t>();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    std::string GetStr() {
        size_t n = Get<uint8_t>();
        if (static_cast<size_t>(end_ - p_) < n) { ok_ = false; p_ = end_; return {}; }
        std::string s(reinterpret_cast<const char*>(p_), n);
        p_ += n;
        return s;
    }
    // Pacotes malformados nunca causam leitura fora do buffer; o chamador
    // verifica Ok() uma unica vez ao final e descarta a mensagem.
    bool Ok() const { return ok_; }
    size_t Remaining() const { return static_cast<size_t>(end_ - p_); }

private:
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_ = true;
};

} // namespace coop
