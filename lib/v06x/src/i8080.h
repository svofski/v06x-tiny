// Intel 8080 (KR580VM80A) microprocessor core model
//
// Copyright (C) 2012 Alexander Demin <alexander@demin.ws>
//
// Credits
//
// Viacheslav Slavinsky, Vector-06C FPGA Replica
// http://code.google.com/p/vector06cc/
//
// Dmitry Tselikov, Bashrikia-2M and Radio-86RK on Altera DE1
// http://bashkiria-2m.narod.ru/fpga.html
//
// Ian Bartholomew, 8080/8085 CPU Exerciser
// http://www.idb.me.uk/sunhillow/8080.html
//
// Frank Cringle, The origianal exerciser for the Z80.
//
// Thanks to zx.pk.ru and nedopc.org/forum communities.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#ifndef I8080_H
#define I8080_H

#include <cstdint>

namespace i8080cpu {
extern void i8080_init(void);
int i8080_instruction();
int i8080_execute(int opcode);
bool i8080_iff();   /* Inner interrupt enable flag, not the same as INTE */
int i8080_cycles(void); /* Return number of cycles taken by the last instr */

extern void i8080_jump(int addr);
extern int i8080_pc(void);

extern int i8080_regs_bc(void);
extern int i8080_regs_de(void);
extern int i8080_regs_hl(void);
extern int i8080_regs_sp(void);

extern int i8080_regs_a(void);
extern int i8080_regs_f(void);
extern int i8080_regs_b(void);
extern int i8080_regs_c(void);
extern int i8080_regs_d(void);
extern int i8080_regs_e(void);
extern int i8080_regs_h(void);
extern int i8080_regs_l(void);

extern void i8080_setreg_a(int a);
extern void i8080_setreg_b(int b);
extern void i8080_setreg_c(int c);
extern void i8080_setreg_d(int d);
extern void i8080_setreg_e(int e);
extern void i8080_setreg_h(int h);
extern void i8080_setreg_l(int l);
extern void i8080_setreg_f(int f);
extern void i8080_setreg_sp(int sp);
extern uint8_t last_opcode;
extern int trace_enable;
}

#include <vector>
#include <cstdint>

namespace i8080cpu {
    void serialize(std::vector<uint8_t> &to);
    void deserialize(std::vector<uint8_t>::iterator it, uint32_t);
}
#endif
