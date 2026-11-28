/* Copyright (c) 2013-2014 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/arm/arm.h>

#include <mgba/internal/arm/isa-arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/arm/isa-thumb.h>

void ARMSetPrivilegeMode(struct ARMCore* cpu, enum PrivilegeMode mode) {
	if (mode == cpu->privilegeMode) {
		// Not switching modes after all
		return;
	}

	enum RegisterBank newBank = ARMSelectBank(mode);
	enum RegisterBank oldBank = ARMSelectBank(cpu->privilegeMode);
	if (newBank != oldBank) {
		// Switch banked registers
		if (mode == MODE_FIQ || cpu->privilegeMode == MODE_FIQ) {
			int oldFIQBank = oldBank == BANK_FIQ;
			int newFIQBank = newBank == BANK_FIQ;
			cpu->bankedRegisters[oldFIQBank][2] = cpu->gprs[8];
			cpu->bankedRegisters[oldFIQBank][3] = cpu->gprs[9];
			cpu->bankedRegisters[oldFIQBank][4] = cpu->gprs[10];
			cpu->bankedRegisters[oldFIQBank][5] = cpu->gprs[11];
			cpu->bankedRegisters[oldFIQBank][6] = cpu->gprs[12];
			cpu->gprs[8] = cpu->bankedRegisters[newFIQBank][2];
			cpu->gprs[9] = cpu->bankedRegisters[newFIQBank][3];
			cpu->gprs[10] = cpu->bankedRegisters[newFIQBank][4];
			cpu->gprs[11] = cpu->bankedRegisters[newFIQBank][5];
			cpu->gprs[12] = cpu->bankedRegisters[newFIQBank][6];
		}
		cpu->bankedRegisters[oldBank][0] = cpu->gprs[ARM_SP];
		cpu->bankedRegisters[oldBank][1] = cpu->gprs[ARM_LR];
		cpu->gprs[ARM_SP] = cpu->bankedRegisters[newBank][0];
		cpu->gprs[ARM_LR] = cpu->bankedRegisters[newBank][1];

		cpu->bankedSPSRs[oldBank] = cpu->spsr.packed;
		cpu->spsr.packed = cpu->bankedSPSRs[newBank];
	}
	cpu->privilegeMode = mode;
}

void ARMInit(struct ARMCore* cpu) {
	ARMSetComputeTurbo(cpu, 1);
	memset(cpu->cp, 0, sizeof(cpu->cp));
	cpu->master->init(cpu, cpu->master);
	size_t i;
	for (i = 0; i < cpu->numComponents; ++i) {
		if (cpu->components[i] && cpu->components[i]->init) {
			cpu->components[i]->init(cpu, cpu->components[i]);
		}
	}
}

void ARMDeinit(struct ARMCore* cpu) {
	if (cpu->master->deinit) {
		cpu->master->deinit(cpu->master);
	}
	size_t i;
	for (i = 0; i < cpu->numComponents; ++i) {
		if (cpu->components[i] && cpu->components[i]->deinit) {
			cpu->components[i]->deinit(cpu->components[i]);
		}
	}
}

void ARMSetComponents(struct ARMCore* cpu, struct mCPUComponent* master, int extra, struct mCPUComponent** extras) {
	cpu->master = master;
	cpu->numComponents = extra;
	cpu->components = extras;
}

void ARMHotplugAttach(struct ARMCore* cpu, size_t slot) {
	if (slot >= cpu->numComponents) {
		return;
	}
	cpu->components[slot]->init(cpu, cpu->components[slot]);
}

void ARMHotplugDetach(struct ARMCore* cpu, size_t slot) {
	if (slot >= cpu->numComponents) {
		return;
	}
	cpu->components[slot]->deinit(cpu->components[slot]);
}

void ARMReset(struct ARMCore* cpu) {
	int i;
	for (i = 0; i < 16; ++i) {
		cpu->gprs[i] = 0;
	}
	for (i = 0; i < 6; ++i) {
		cpu->bankedRegisters[i][0] = 0;
		cpu->bankedRegisters[i][1] = 0;
		cpu->bankedRegisters[i][2] = 0;
		cpu->bankedRegisters[i][3] = 0;
		cpu->bankedRegisters[i][4] = 0;
		cpu->bankedRegisters[i][5] = 0;
		cpu->bankedRegisters[i][6] = 0;
		cpu->bankedSPSRs[i] = 0;
	}

	cpu->privilegeMode = MODE_SYSTEM;
	cpu->cpsr.packed = MODE_SYSTEM;
	cpu->spsr.packed = 0;

	cpu->shifterOperand = 0;
	cpu->shifterCarryOut = 0;

	cpu->executionMode = MODE_THUMB;
	_ARMSetMode(cpu, MODE_ARM);
	ARMWritePC(cpu);

	cpu->cycles = 0;
	cpu->nextEvent = 0;
	cpu->halted = 0;
	cpu->gbaxFraction = 0;

	cpu->irqh.reset(cpu);
}

void ARMRaiseIRQ(struct ARMCore* cpu) {
	if (cpu->cpsr.i) {
		return;
	}
	union PSR cpsr = cpu->cpsr;
	int instructionWidth;
	if (cpu->executionMode == MODE_THUMB) {
		instructionWidth = WORD_SIZE_THUMB;
	} else {
		instructionWidth = WORD_SIZE_ARM;
	}
	ARMSetPrivilegeMode(cpu, MODE_IRQ);
	cpu->cpsr.priv = MODE_IRQ;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - instructionWidth + WORD_SIZE_ARM;
	cpu->gprs[ARM_PC] = BASE_IRQ;
	_ARMSetMode(cpu, MODE_ARM);
	cpu->cycles += ARMWritePC(cpu);
	cpu->spsr = cpsr;
	cpu->cpsr.i = 1;
	cpu->halted = 0;
}

void ARMRaiseSWI(struct ARMCore* cpu) {
	union PSR cpsr = cpu->cpsr;
	int instructionWidth;
	if (cpu->executionMode == MODE_THUMB) {
		instructionWidth = WORD_SIZE_THUMB;
	} else {
		instructionWidth = WORD_SIZE_ARM;
	}
	ARMSetPrivilegeMode(cpu, MODE_SUPERVISOR);
	cpu->cpsr.priv = MODE_SUPERVISOR;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - instructionWidth;
	cpu->gprs[ARM_PC] = BASE_SWI;
	_ARMSetMode(cpu, MODE_ARM);
	cpu->cycles += ARMWritePC(cpu);
	cpu->spsr = cpsr;
	cpu->cpsr.i = 1;
}

void ARMRaiseUndefined(struct ARMCore* cpu) {
	union PSR cpsr = cpu->cpsr;
	int instructionWidth;
	if (cpu->executionMode == MODE_THUMB) {
		instructionWidth = WORD_SIZE_THUMB;
	} else {
		instructionWidth = WORD_SIZE_ARM;
	}
	ARMSetPrivilegeMode(cpu, MODE_UNDEFINED);
	cpu->cpsr.priv = MODE_UNDEFINED;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - instructionWidth;
	cpu->gprs[ARM_PC] = BASE_UNDEF;
	_ARMSetMode(cpu, MODE_ARM);
	cpu->cycles += ARMWritePC(cpu);
	cpu->spsr = cpsr;
	cpu->cpsr.i = 1;
}

static const uint16_t conditionLut[16] = {
	0xF0F0, // EQ [-Z--]
	0x0F0F, // NE [-z--]
	0xCCCC, // CS [--C-]
	0x3333, // CC [--c-]
	0xFF00, // MI [N---]
	0x00FF, // PL [n---]
	0xAAAA, // VS [---V]
	0x5555, // VC [---v]
	0x0C0C, // HI [-zC-]
	0xF3F3, // LS [-Z--] || [--c-]
	0xAA55, // GE [N--V] || [n--v]
	0x55AA, // LT [N--v] || [n--V]
	0x0A05, // GT [Nz-V] || [nz-v]
	0xF5FA, // LE [-Z--] || [Nz-v] || [nz-V]
	0xFFFF, // AL [----]
	0x0000 // NV
};

static inline void ARMStep(struct ARMCore* cpu) {
	uint32_t opcode = cpu->prefetch[0];
	cpu->prefetch[0] = cpu->prefetch[1];
	cpu->gprs[ARM_PC] += WORD_SIZE_ARM;
	LOAD_32(cpu->prefetch[1], cpu->gprs[ARM_PC] & cpu->memory.activeMask, cpu->memory.activeRegion);

	unsigned condition = opcode >> 28;
	if (condition != 0xE) {
		unsigned flags = cpu->cpsr.flags >> 4;
		bool conditionMet = conditionLut[condition] & (1 << flags);
		if (!conditionMet) {
			cpu->cycles += ARM_PREFETCH_CYCLES;
			return;
		}
	}
	ARMInstruction instruction = _armTable[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0x00F)];
	instruction(cpu, opcode);
}

static inline void ThumbStep(struct ARMCore* cpu) {
	uint32_t opcode = cpu->prefetch[0];
	cpu->prefetch[0] = cpu->prefetch[1];
	cpu->gprs[ARM_PC] += WORD_SIZE_THUMB;
	LOAD_16(cpu->prefetch[1], cpu->gprs[ARM_PC] & cpu->memory.activeMask, cpu->memory.activeRegion);
	ThumbInstruction instruction = _thumbTable[opcode >> 6];
	instruction(cpu, opcode);
}

/* Phase 6A: accelerate internal instruction cost only, not the bus.
 * For eligible register-only operations in IWRAM, the native instruction
 * cost is converted to master ticks with an eighth-cycle accumulator;
 * there is no external bus access.
 * ROM/BIOS/EWRAM instruction fetch and memory/MMIO instructions stay native.
 * No changes to the scheduler, IRQ, timers, PPU, APU or DMA clock. */
void ARMSetComputeTurbo(struct ARMCore* cpu, unsigned multiplier) {
	cpu->gbaxTurbo = multiplier==2 || multiplier==4 || multiplier==8 ? multiplier:1;
	cpu->gbaxFraction=0;
}
static bool _gbaxARMCompute(uint32_t op) {
	/* B/BL; ordinary data processing with Rd != PC; register MUL/MLA. */
	if ((op&0x0e000000u)==0x0a000000u) return (op>>28)!=15;
	if ((op&0x0fc000f0u)==0x00000090u)
		return ((op>>16)&15)!=15 && ((op>>12)&15)!=15 &&
			((op>>8)&15)!=15 && (op&15)!=15;
	if ((op&0x0c000000u)!=0 || ((op>>12)&15)==15) return false;
	unsigned alu=(op>>21)&15;
	/* Exclude miscellaneous encodings in the test/compare opcode space. */
	if (alu>=8 && alu<=11 && !(op&(1u<<20))) return false;
	if (!(op&(1u<<25)) && (op&0x90u)==0x90u) return false;
	return true;
}
static bool _gbaxThumbCompute(uint32_t op) {
	/* Shifts, add/sub, immediate ALU, low-register ALU, PC/SP arithmetic,
	 * conditional/unconditional branch, but not SWI, BX, load/store. */
	return op<0x4000u || (op>=0x4000u && op<0x4400u) ||
		(op>=0xa000u && op<0xb000u) ||
		(op>=0xd000u && op<0xde00u) ||
		(op>=0xe000u && op<0xe800u);
}
static inline void _gbaxStep(struct ARMCore* cpu) {
	const bool thumb=cpu->executionMode==MODE_THUMB;
	const uint32_t pc=(uint32_t)cpu->gprs[ARM_PC]-(thumb?WORD_SIZE_THUMB:WORD_SIZE_ARM);
	const uint32_t op=cpu->prefetch[0];
	const int32_t before=cpu->cycles, event=cpu->nextEvent;
	/* IWRAM only: external bus timing stays native. */
	const bool eligible=cpu->gbaxTurbo>1 && !cpu->halted &&
		(pc>>24)==3 && (thumb?_gbaxThumbCompute(op):_gbaxARMCompute(op));
	if (thumb) ThumbStep(cpu); else ARMStep(cpu);
	/* Never undo a timing change or scale a halt/stop transition. */
	if (!eligible || cpu->halted || cpu->nextEvent!=event ||
		cpu->cycles<=before || ((uint32_t)cpu->gprs[ARM_PC]>>24)!=3) return;
	uint64_t delta=(uint32_t)(cpu->cycles-before);
	uint64_t ticks=delta*(8/cpu->gbaxTurbo)+cpu->gbaxFraction;
	cpu->cycles=before+(int32_t)(ticks/8);
	cpu->gbaxFraction=(unsigned)(ticks%8);
}

void ARMRun(struct ARMCore* cpu) {
	while (cpu->cycles >= cpu->nextEvent) {
		cpu->irqh.processEvents(cpu);
	}
	if (cpu->gbaxTurbo>1) {
		_gbaxStep(cpu);
	} else if (cpu->executionMode == MODE_THUMB) {
		ThumbStep(cpu);
	} else {
		ARMStep(cpu);
	}
	while (cpu->cycles >= cpu->nextEvent) {
		cpu->irqh.processEvents(cpu);
	}
}

void ARMRunLoop(struct ARMCore* cpu) {
	if (cpu->gbaxTurbo>1) {
		while (cpu->cycles < cpu->nextEvent) _gbaxStep(cpu);
	} else if (cpu->executionMode == MODE_THUMB) {
		while (cpu->cycles < cpu->nextEvent) {
			ThumbStep(cpu);
		}
	} else {
		while (cpu->cycles < cpu->nextEvent) {
			ARMStep(cpu);
		}
	}
	cpu->irqh.processEvents(cpu);
}

void ARMRunFake(struct ARMCore* cpu, uint32_t opcode) {
	if (cpu->executionMode == MODE_ARM) {
		cpu->gprs[ARM_PC] -= WORD_SIZE_ARM;
	} else {
		cpu->gprs[ARM_PC] -= WORD_SIZE_THUMB;
	}
	cpu->prefetch[1] = cpu->prefetch[0];
	cpu->prefetch[0] = opcode;
}
