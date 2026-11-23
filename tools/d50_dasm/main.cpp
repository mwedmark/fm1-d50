// Dumps what the uPD78312 disassembler (MAME's) makes of every address of the D-50's CPU, for tools/d50_dasm/analyze.py.
// usage: dasm FIRMWARE.bin BANK > sweep.txt   (BANK 2: 0x8000..0xBFFF is the file's 0x8000.., BANK 3: its 0xC000..)
// Each line: ADDR LEN FLAGS TEXT (FLAGS: o = step over (call), u = step out (return), c = conditional, - none)
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>
#include "emu.h"
#include "mame/upd78k3d.h"
struct D : upd78312_disassembler {
    using upd78312_disassembler::disassemble;
};
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    std::vector<u8> rom(0x10000, 0xFF);
    size_t got = fread(rom.data(), 1, rom.size(), f);
    fclose(f);
    (void)got;
    int bank = atoi(argv[2]);
    std::vector<u8> mem(0x10000, 0xFF);
    for (u32 a = 0; a < 0x8000; a++) mem[a] = rom[a];   // (the dump's first 8 KB too: the vectors, the BR table at 0x40, code from 0x80)
    for (u32 a = 0x8000; a < 0xC000; a++) mem[a] = rom[(bank == 3 ? 0xC000u : 0x8000u) + (a - 0x8000)];
    D d;
    util::disasm_interface::data_buffer buf(mem.data(), mem.size());
    for (u32 a = 0; a < 0x10000; a++) {
        std::ostringstream s;
        u32 r = d.disassemble(s, a, buf, buf);
        printf("%04X %u %c %s\n", a, r & 0xFFFFu, r & util::disasm_interface::STEP_OVER ? 'o' : r & util::disasm_interface::STEP_OUT ? 'u' : r & util::disasm_interface::STEP_COND ? 'c' : '-', s.str().c_str());
    }
    return 0;
}
