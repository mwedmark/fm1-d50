// A stand-in for MAME's emu.h: just what the upd78k disassemblers (BSD-3-Clause, AJR, in mame/) use.
#pragma once
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef uint32_t offs_t;
#define BIT(x, n) (((x) >> (n)) & 1)
namespace util {
class disasm_interface {
public:
    enum : u32 { STEP_OVER = 0x10000000, STEP_OUT = 0x20000000, STEP_COND = 0x40000000, SUPPORTED = 0x80000000 };
    class data_buffer {
    public:
        data_buffer(const u8 *p, u32 n) : m_p(p), m_n(n) {}
        u8 r8(offs_t a) const { return a < m_n ? m_p[a] : 0xFF; }
        u16 r16(offs_t a) const { return (u16)(r8(a) | r8(a + 1) << 8); }   // little endian
    private:
        const u8 *m_p;
        u32 m_n;
    };
    virtual ~disasm_interface() = default;
    virtual u32 opcode_alignment() const = 0;
    virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
};
template <typename T> T fmt_arg(T v) { return v; }
inline const char *fmt_arg(const std::string &v) { return v.c_str(); }   // (stream_format's arguments are checked in MAME; here printf's)
template <typename... A> std::string string_format(const char *fmt, A... a)
{
    char b[256];
    snprintf(b, sizeof b, fmt, fmt_arg(a)...);
    return b;
}
template <typename... A> void stream_format(std::ostream &s, const char *fmt, A... a) { s << string_format(fmt, a...); }
}
