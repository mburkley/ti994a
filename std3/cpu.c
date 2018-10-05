/*
 *  TODO :
 *
 *  - Make this CPU only.
 *  - Move interaction into debug module.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <conio.h>

#include "vdp.h"
#include "cpu.h"
#include "grom.h"
#include "break.h"
#include "watch.h"
#include "cond.h"
#include "cru.h"

struct
{
    WORD pc;
    WORD wp;
    WORD st;

    union
    {
        // WORD w[32768];
        // BYTE b[65536];
        WORD w[0x5000];
        BYTE b[0xA000];
    }
    ram;
}
tms9900;

extern BYTE covered[];

int outputLevel;
int outputCovered;

#define AREG(n)  (WORD)(tms9900.wp+(n<<1))
#define REG(n)  cpuRead(AREG(n))
#define SWAP(w)  ((w >> 8) | ((w & 0xFF) << 8))

int mprintf (int level, char *s, ...)
{
    va_list ap;

    va_start (ap, s);

    if (level >= outputLevel)
        vprintf (s, ap);

    va_end (ap);

    return 0;
}

void halt (char *s)
{
    vdpRefresh(1);

    printf ("HALT: %s\n", s);

    exit (1);
}

WORD cpuRead(WORD addr)
{
    if (addr >= 0xA000)
    {
        halt ("CPU read word out of range");
    }

    if ((addr & 0xE000) == 0x8000)
    {
        if ((addr & 0xF800) == 0x9800)
        {
            // halt ("Invalid word read from GROM");
            return 0;
        }
        else if ((addr & 0xF800) == 0x8800)
        {
            halt ("Invalid word read from VDP");
        }
    }

    return tms9900.ram.w[addr>>1];
}

static void cpuWrite(WORD addr, WORD data)
{
    /*
    if (addr < 0x2000)
    {
        halt ("Write to ROM");
    }
    */

    if (addr >= 0xA000)
    {
        halt ("CPU write word out of range");
    }

    if ((addr & 0xE000) == 0x8000)
    {
        if ((addr & 0xF800) == 0x9800)
        {
            if (addr == 0x9802)
            {
                // mprintf (0, "GROMAD WORD write to 9802\n");
                gromSetAddr (data);
            }
            else
            {
                halt ("Invalid word write to GROM");
            }
        }
        else if ((addr & 0xF800) == 0x8800)
        {
            halt ("Invalid word write to GROM");
        }
    }

    tms9900.ram.w[addr>>1] = data;
}

static BYTE cpuReadB(WORD addr)
{
    if (addr >= 0xA000)
    {
        printf ("addr=%04X\n", addr);
        halt ("CPU read byte out of range");
    }

    if ((addr & 0xE000) == 0x8000)
    {
        if ((addr & 0xFC00) == 0x9800)
        {
            /*
             *  Console GROMs respond to any base
             */

            if ((addr & 0x03) == 0x00)
            {
                tms9900.ram.b[addr ^ 1] = gromRead ();
            }
            else if ((addr & 0x03) == 0x02)
            {
                tms9900.ram.b[addr ^ 1] = gromGetAddr ();
            }
            else
            {
                halt ("Strange GROM CPU addr\n");
            }
        }
        else if ((addr & 0xF800) == 0x8800)
        {
            if (addr == 0x8800)
            {
                tms9900.ram.b[0x8801] = vdpReadData ();
            }
            else if (addr == 0x8802)
            {
                tms9900.ram.b[0x8803] = vdpReadStatus ();
            }
        }
    }

    addr = addr ^ 1;

    return tms9900.ram.b[addr];
}

static void cpuWriteB(WORD addr, BYTE data)
{
    /*
    if (addr < 0x2000)
    {
        halt ("Write to ROM");
    }
    */

    if (addr >= 0xA000)
    {
        halt ("CPU write byte out of range");
    }

    if ((addr & 0xE000) == 0x8000)
    {
        if ((addr & 0xFC00) == 0x9C00)
        {
            if ((addr & 0x03) == 0x00)
            {
                halt ("Invalid byte write to GROM addr");
                gromSetAddr (data);
            }
            else if ((addr & 0x03) == 0x02)
            {
                // mprintf (0, "GROMAD BYTE write to 9802\n");
                gromSetAddr (data);
            }
            else
            {
                halt ("invalid GROM write operation");
                return;
            }
        }
        else if ((addr & 0xF800) == 0x8800)
        {
            if (addr == 0x8C00)
            {
                vdpWriteData (data);
            }
            else if (addr == 0x8C02)
            {
                vdpWriteCommand (data);
            }
            else
            {
                printf ("attempt to write %02X to %04X\n", data, addr);
                halt ("invalid VDP write operation");
            }
        }
    }

    addr = addr ^ 1;

    tms9900.ram.b[addr] = data;
}

static WORD fetch (void)
{
    WORD ret;

    // if (tms9900.pc >= 0x2000)
    // {
    //     printf ("PC=%04X\n", tms9900.pc);
    //     halt ("Fetch from outside ROM area\n");
    // }

    ret = cpuRead(tms9900.pc);
    tms9900.pc += 2;
    return ret;
}

static void blwp (WORD addr)
{
    WORD owp = tms9900.wp;
    WORD opc = tms9900.pc;

    // printf ("blwp @%x\n", addr);

    tms9900.wp = cpuRead (addr);
    tms9900.pc = cpuRead ((WORD) (addr+2));

    cpuWrite (AREG (13), owp);
    cpuWrite (AREG (14), opc);
    cpuWrite (AREG (15), tms9900.st);
}

static void rtwp (void)
{
    // printf ("rtwp\n");

    tms9900.pc = REG (14);
    tms9900.st = REG (15);
    tms9900.wp = REG (13);
}

static void compare (WORD data1, WORD data2)
{
    /*
     *  Leave int. mask and Carry flag
     */

    tms9900.st &= FLAG_MSK;

    if (data1 == data2)
    {
        tms9900.st |= FLAG_EQ;
    }

    if (data1 > data2)
    {
        tms9900.st |= FLAG_LGT;
    }

    if ((signed short) data1 > (signed short) data2)
    {
        tms9900.st |= FLAG_AGT;
    }

    mprintf (0, "CMP %04X,%04X %s%s%s%s ",
             data1, data2,
             tms9900.st & FLAG_C ? "C" : "-",
             tms9900.st & FLAG_EQ ? "E" : "-",
             tms9900.st & FLAG_LGT ? "L" : "-",
             tms9900.st & FLAG_AGT ? "A" : "-");
}

static void compareB (BYTE data1, BYTE data2)
{
    tms9900.st &= FLAG_MSK;

    if (data1 == data2)
    {
        tms9900.st |= FLAG_EQ;
    }

    if (data1 > data2)
    {
        tms9900.st |= FLAG_LGT;
    }

    if ((signed char) data1 > (signed char) data2)
    {
        tms9900.st |= FLAG_AGT;
    }

    // mprintf (0, "CMPB: %02X : %02X %s %s %s\n",
    //         data1, data2,
    //         tms9900.st & FLAG_EQ ? "EQ" : "",
    //         tms9900.st & FLAG_LGT ? "LGT" : "",
    //         tms9900.st & FLAG_AGT ? "AGT" : "");

}

static WORD decode (WORD data,
                    BOOL *isByte,
                    WORD *sReg,
                    WORD *sMode,
                    WORD *dReg,
                    WORD *dMode,
                    I8 *offset,
                    WORD *count)
{
    WORD opcode;

    if (data & 0xC000)
    {
        /*
         *  FMT 1
         */

        *isByte =  data & 0x1000;
        *dMode  = (data & 0x0C00) >> 10;
        *dReg   = (data & 0x03C0) >> 6;
        *sMode  = (data & 0x0030) >> 4;
        *sReg   =  data & 0x000F;
        opcode =  data & 0xE000;

        // mprintf (0, "2-op : %04X\n", opcode);
    }
    else if (data & 0x2000)
    {
        // printf ("coc/czc\n");

        *dReg   = (data & 0x03C0) >> 6;
        *sMode  = (data & 0x0030) >> 4;
        *sReg   =  data & 0x000F;
        opcode =  data & 0xFC00;
    }
    else if (data & 0x1000)
    {
        // printf ("jmp\n");

        *offset = data & 0x00FF;
        opcode = data & 0xFF00;
    }
    else if (data & 0x0800)
    {
        // printf ("shift\n");

        *count  = (data & 0x00F0) >> 4;
        *sReg   =  data & 0x000F;
        opcode =  data & 0xFF00;
    }
    else if (data & 0x0400)
    {
        // printf ("prog\n");

        *sMode  = (data & 0x0030) >> 4;
        *sReg   =  data & 0x000F;
        opcode =  data & 0xFFC0;
    }
    else
    {
        // printf ("immed, pc is %x\n", tms9900.pc << 1);

        *sReg   = data & 0x000F;
        opcode = data & 0xFFF0;
    }

    // printf ("dm=%d, dreg=%d, sm=%d, sreg=%d, byte=%d, pc=%x\n",
    //         dMode, dReg, sMode, sReg, isByte, tms9900.pc);

    return opcode;
}

static void operand (WORD mode, WORD reg, WORD *arg, WORD *addr, BOOL isByte)
{
    switch (mode)
    {
    case AMODE_NORMAL:
        *addr = AREG(reg);
        break;

    case AMODE_INDIR:
        // mprintf (0, "INDIR: R%d->%04X\n", reg, REG(reg));
        *addr = REG(reg);
        break;

    case AMODE_SYM:
        // printf ("SYM: @[%04X]+R%d (%04X) \n",
        //         (unsigned) cpuRead (tms9900.pc), (unsigned) sReg, (unsigned) REG (sReg));
        *arg = fetch();
        *addr = (WORD) (*arg + (reg == 0 ? 0 : REG (reg)));
        break;

    case AMODE_INDIRINC:
        *addr = REG(reg);
        cpuWrite (AREG(reg), (WORD) (REG(reg) + (isByte ? 1 : 2)));
        break;
    }

    // printf ("dm=%d, sm=%d, pc=%x\n", dMode, sMode, tms9900.pc);

}

static char * printOper (WORD mode, WORD reg, WORD addr)
{
    static char result[20];

    switch (mode)
    {
    case AMODE_NORMAL:
        sprintf (result, "%d", reg);
        break;

    case AMODE_INDIR:
        sprintf (result, "*%d", reg);
        break;

    case AMODE_SYM:
        if (reg)
        {
            sprintf (result, "@>%04X[%d]", addr, reg);
        }
        else
        {
            sprintf (result, "@>%04X", addr);
        }

        break;

    case AMODE_INDIRINC:
        sprintf (result, "*%d+", reg);
        break;
    }

    return result;
}

static void unasmTwoOp (char *name, BOOL isByte, WORD sMode, WORD sReg,
                        WORD sAddr, WORD dMode, WORD dReg, WORD dAddr)
{
    char opName[5];
    char op[10];
    char out[31];

    if (outputLevel < 2)
        return;

    if (outputCovered && covered[tms9900.pc >> 1])
        return;

    covered[tms9900.pc >> 1] = 1;

    sprintf (opName, "%s%s", name, isByte ? "B" : "");

    strcpy (op, printOper (sMode, sReg, sAddr));


    sprintf (out, "%-4s\t%s,%s",
             opName,
             op,
             printOper (dMode, dReg, dAddr));

    mprintf (2, "%-30.30s", out);
}

static void unasmOneOp (char *name, WORD sMode, WORD sReg,
                        WORD sAddr)
{
    char out[31];

    if (outputLevel < 2)
        return;

    if (outputCovered && covered[tms9900.pc >> 1])
        return;

    covered[tms9900.pc >> 1] = 1;

    sprintf (out, "%-4s\t%s",
             name,
             printOper (sMode, sReg, sAddr));

    mprintf (2, "%-30.30s", out);
}

static void unasmImmed (char *name, WORD sReg)
{
    char out[31];

    if (outputLevel < 2)
        return;

    if (outputCovered && covered[tms9900.pc >> 1])
        return;

    covered[tms9900.pc >> 1] = 1;

    sprintf (out, "%-4s\t%s,>%04X",
             name,
             printOper (0, sReg, 0),
             cpuRead(tms9900.pc));

    mprintf (2, "%-30.30s", out);
}

static void unasmJump (char *name, I8 offset, BOOL cond)
{
    char out[31];

    if (outputLevel < 2)
        return;

    if (outputCovered && covered[tms9900.pc >> 1])
        return;

    covered[tms9900.pc >> 1] = 1;

    sprintf (out, "%-4s\t>%04X\t\t%s",
            name,
            tms9900.pc + (offset << 1),
            cond ? "****" : "");

    mprintf (2, "%-30.30s", out);
}

static void unasmCRU (char *name, I8 offset)
{
    char out[31];

    if (outputLevel < 2)
        return;

    if (outputCovered && covered[tms9900.pc >> 1])
        return;

    covered[tms9900.pc >> 1] = 1;

    sprintf (out, "%-4s\t%d",
            name,
            offset);

    mprintf (2, "%-30.30s", out);
}

static void execute (WORD data)
{
    WORD  sReg = 0, dReg = 0;
    WORD sAddr = 0, dAddr = 0;
    WORD sData = 0, dData = 0;
    WORD dMode = 0, sMode = 0;
    WORD sArg = 0, dArg = 0;
    BOOL isByte = 0;
    BOOL doStore = 0;
    I8 offset = 0;
    WORD count = 0;
    WORD opcode = 0;
    U32 u32 = 0;
    I32 i32 = 0;
    BOOL carry = 0;

    opcode = decode (data,
            &isByte, &sReg,
            &sMode,
            &dReg,
            &dMode,
            &offset,
            &count);

    if (!outputCovered || !covered[tms9900.pc >> 1])
    {
       mprintf (2, "%04X:%04X ", tms9900.pc - 2, data);
    }

    operand (sMode, sReg, &sArg, &sAddr, isByte);
    operand (dMode, dReg, &dArg, &dAddr, isByte);

    if (isByte)
    {
        sData = cpuReadB (sAddr) << 8;
        dData = cpuReadB (dAddr) << 8;

        // mprintf (0, "sData [%04X] = %02X, dData [%04X] = %02X\n",
        //         (unsigned) sAddr, (unsigned) sData >> 8,
        //         (unsigned) dAddr, (unsigned) dData >> 8);
    }
    else
    {
        sData = cpuRead (sAddr);
        dData = cpuRead (dAddr);

        // mprintf (0, "sData = %04X, dData = %04X\n", (unsigned) sData, (unsigned) dData);
    }

    switch (opcode)
    {

    /*
     *  D U A L   O P E R A N D
     */

    case OP_SZC:
        unasmTwoOp ("SZC", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        dData = dData & ~sData;
        doStore = 1;
        break;

    case OP_S:
        unasmTwoOp ("S", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        dData = dData - sData;
        doStore = 1;
        break;

    case OP_C:
        unasmTwoOp ("C", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        // if (isByte)
        // {
        //     compareB (sData, dData);
        // }
        // else
        // {
            compare (sData, dData);
        // }

        break;

    case OP_A:
        unasmTwoOp ("A", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        // mprintf (0, "Add %04X+%04X\n", sData, dData);
        u32 = (U32) dData + sData;
        dData = u32 & 0xFFFF;
        u32 >>= 16;

        if (u32)
            carry = 1;

        doStore = 1;
        break;

    case OP_MOV:
        unasmTwoOp ("MOV", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        // mprintf (0, "MOV: %04X\n", (unsigned) sData);
        dData = sData;
        doStore = 1;
        break;

    case OP_SOC:
        unasmTwoOp ("SOC", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        dData = dData | sData;
        doStore = 1;
        break;

    /*
     *
     */

    case OP_COC:
        unasmTwoOp ("COC", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        compare (sData & dData, sData);
        break;

    case OP_CZC:
        unasmTwoOp ("CZC", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        compare (sData & ~dData, sData);
        // printf ("\n CZC : %04X,%04X (CMP %04X to %04X)\n", sData, dData,
        //         sData & ~dData, sData);
        break;

    case OP_XOR:
        unasmTwoOp ("XOR", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        dData ^= sData;
        doStore = 1;
        break;

    case OP_XOP:
        halt ("Unsupported");
    case OP_MPY:
        /*
         *  TODO: double-check this
         */
        unasmTwoOp ("MPY", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        u32 = dData * sData;
        cpuWrite (AREG(dReg), (WORD) (u32 >> 16));
        cpuWrite (AREG(dReg+1), (WORD) (u32 & 0xFFFF));
        break;

    case OP_DIV:
        /*
         *  TODO: double-check this
         */
        unasmTwoOp ("DIV", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        u32 = REG(dReg) << 16 | REG(dReg+1);
        if (sData == 0)
        {
            tms9900.st |= FLAG_OV;
        }
        else
        {
            u32 /= sData;
        }
        cpuWrite (AREG(dReg), (WORD) (u32 >> 16));
        cpuWrite (AREG(dReg+1), (WORD) (u32 & 0xFFFF));
        break;

    /*
     *  I M M E D I A T E S
     */

    case OP_LI:
        unasmImmed ("LI", sReg);
        cpuWrite (AREG(sReg), fetch());
        break;

    case OP_AI:
        unasmImmed ("AI", sReg);
        cpuWrite (AREG(sReg), (WORD) (REG(sReg) + fetch()));
		break;

	case OP_ANDI:
        unasmImmed ("ANDI", sReg);
        cpuWrite (AREG(sReg), (WORD) (REG(sReg) & fetch()));
        break;

    case OP_ORI:
        unasmImmed ("ORI", sReg);
        dAddr = AREG(sReg);
        dData = (WORD) (REG(sReg) | fetch());
        doStore = 1;
        // cpuWrite (AREG(sReg), (WORD) (REG(sReg) | fetch()));
        break;

    case OP_CI:
        unasmImmed ("CI", sReg);
        compare (sData, fetch());
        break;

    case OP_STST:
        unasmImmed ("STST", sReg);
        cpuWrite (AREG(sReg), tms9900.st);
        break;

    case OP_STWP:
        unasmImmed ("STWP", sReg);
        cpuWrite (AREG(sReg), tms9900.wp);
        break;

    case OP_LWPI:
        unasmImmed ("LWPI", sReg);
        tms9900.wp = fetch();
        break;

    case OP_LIMI:
        unasmImmed ("LIMI", sReg);
        tms9900.st = (tms9900.st & ~FLAG_MSK) | fetch();
        break;

    /*
     *  J U M P
     */

    case OP_JMP:
        unasmJump ("JMP", offset, 1);
        tms9900.pc += offset << 1;
        break;

    case OP_JLT:
        unasmJump ("JLT", offset, 1);
        if (!(tms9900.st & FLAG_AGT) &&
            !(tms9900.st & FLAG_EQ))
        {
                tms9900.pc += offset << 1;
        }

        break;

    case OP_JGT:
        unasmJump ("JGT", offset, 1);
        if ((tms9900.st & FLAG_AGT))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JL:
        unasmJump ("JL", offset, 1);
        if (!(tms9900.st & FLAG_LGT) &&
            !(tms9900.st & FLAG_EQ))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JLE:
        unasmJump ("JLE", offset, 1);
        if (!(tms9900.st & FLAG_LGT) ||
             (tms9900.st & FLAG_EQ))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JH:
        unasmJump ("JH", offset, 1);
        if ( (tms9900.st & FLAG_LGT) &&
            !(tms9900.st & FLAG_EQ))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JHE:
        unasmJump ("JHE", offset, 1);
        if ( (tms9900.st & FLAG_LGT) ||
             (tms9900.st & FLAG_EQ))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JNC:
        unasmJump ("JNC", offset, !(tms9900.st & FLAG_C));

        if (!(tms9900.st & FLAG_C))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JOC:
        unasmJump ("JOC", offset, tms9900.st & FLAG_C);

        if ((tms9900.st & FLAG_C))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JNO:
        unasmJump ("JNO", offset, !(tms9900.st & FLAG_OV));

        if (!(tms9900.st & FLAG_OV))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JNE:
        unasmJump ("JNE", offset, !(tms9900.st & FLAG_EQ));

        if (!(tms9900.st & FLAG_EQ))
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_JEQ:
        unasmJump ("JEQ", offset, tms9900.st & FLAG_EQ);
        if (tms9900.st & FLAG_EQ)
        {
            tms9900.pc += offset << 1;
        }

        break;

    case OP_SBZ:
        unasmCRU ("SBZ", offset);
        cruBitSet (REG(12), offset, 0);
        break;

    case OP_SBO:
        unasmCRU ("SBO", offset);
        cruBitSet (REG(12), offset, 1);
        break;

    case OP_TB:
        unasmCRU ("TB", offset);
        if (cruBitGet (REG(12), offset))
            tms9900.st |= FLAG_EQ;

        break;

    /*
     *
     */

    case OP_LDCR:
        unasmTwoOp ("LDCR", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        cruMultiBitSet (REG(12), REG(sReg), count);
        break;

    case OP_STCR:
        unasmTwoOp ("STCR", isByte, sMode, sReg, sArg, dMode, dReg, dArg);
        cpuWrite (AREG(sReg), cruMultiBitGet (REG(12), count));
        break;

    case OP_SRA:
        unasmTwoOp ("SRA", 0, 0, sReg, 0, 0, count, 0);
        i32 = REG (sReg);
        i32 <<= 16;
        i32 >>= count;

        cpuWrite (AREG (sReg), i32 >> 16);
        compare (REG (sReg), 0);

        if (i32 & 0x8000)
        {
            carry = 1;
        }

        break;

    case OP_SRC:
        unasmTwoOp ("SRC", 0, 0, sReg, 0, 0, count, 0);
        mprintf (0, "SRC : fudging\n");

    case OP_SRL:
        unasmTwoOp ("SRL", 0, 0, sReg, 0, 0, count, 0);
        u32 = REG (sReg);
        u32 <<= 16;
        u32 >>= count;

        cpuWrite (AREG (sReg), u32 >> 16);
        compare (REG (sReg), 0);

        if (u32 & 0x8000)
        {
            carry = 1;
        }

        break;

    case OP_SLA:
        unasmTwoOp ("SLA", 0, 0, sReg, 0, 0, count, 0);
        u32 = REG (sReg);
        u32 <<= count;

        cpuWrite (AREG (sReg), u32 & 0xFFFF);
        compare (REG (sReg), 0);

        /*
         *  Set carry flag on MSB set
         */

        if (u32 & 0x10000)
        {
            carry = 1;
        }

        break;

    case OP_BLWP:
        unasmOneOp ("BLWP", sMode, sReg, sArg);
        blwp (sAddr);
        break;

    case OP_RTWP:
        unasmOneOp ("RTWP", sMode, sReg, sArg);
        rtwp ();
        break;

    case OP_B:
        unasmOneOp ("B", sMode, sReg, sArg);
        tms9900.pc = sAddr;
		break;

	case OP_X:
        unasmOneOp ("X", sMode, sReg, sArg);
        mprintf (0, "X : fudged\n");
		break;

	case OP_CLR:
        unasmOneOp ("CLR", sMode, sReg, sArg);
        cpuWrite (sAddr, 0);
		break;

	case OP_NEG:
        unasmOneOp ("NEG", sMode, sReg, sArg);
        cpuWrite (sAddr, -sData);
		break;

	case OP_INV:
        unasmOneOp ("INV", sMode, sReg, sArg);
        cpuWrite (sAddr, ~sData);
        break;

    case OP_INC:
        unasmOneOp ("INC", sMode, sReg, sArg);
        cpuWrite (sAddr, sData+=1);
        compare (sData, 0);
        break;

    case OP_INCT:
        unasmOneOp ("INCT", sMode, sReg, sArg);
        cpuWrite (sAddr, sData+=2);
        compare (sData, 0);
        break;

    case OP_DEC:
        unasmOneOp ("DEC", sMode, sReg, sArg);
        cpuWrite (sAddr, sData-=1);
        compare (sData, 0);
        break;

    case OP_DECT:
        unasmOneOp ("DECT", sMode, sReg, sArg);
        cpuWrite (sAddr, sData-=2);
        compare (sData, 0);
        break;

    case OP_BL:
        unasmOneOp ("BL", sMode, sReg, sArg);
        cpuWrite(AREG(11), tms9900.pc);
        tms9900.pc = sAddr;
		break;

    case OP_SWPB:
        unasmOneOp ("SWPB", sMode, sReg, sArg);
        cpuWrite (sAddr, SWAP(sData));
        break;

    case OP_SETO:
        unasmOneOp ("SETO", sMode, sReg, sArg);
        cpuWrite (sAddr, 0xFFFF);
        break;

	case OP_ABS:
        unasmOneOp ("ABS", sMode, sReg, sArg);
        if ((signed) sData < 0)
            cpuWrite (sAddr, -sData);

		break;
    default:
        printf ("%04X\n", opcode);
        halt ("Unknown opcode");
    }

    if (doStore)
    {
        if (isByte)
        {
            mprintf (2, "B:[%04X] = %02X",
                    (unsigned) dAddr, (unsigned) dData >> 8);
            cpuWriteB (dAddr, (WORD) (dData >> 8));
            compare (dData, 0);

            // compareB (dData, 0);
        }
        else
        {
            mprintf (2, "W:[%04X] = %04X",
                    (unsigned) dAddr, (unsigned) dData);
            cpuWrite (dAddr, dData);
            compare (dData, 0);
        }

        if (dAddr >= tms9900.wp && dAddr < tms9900.wp + 32)
        {
            mprintf (2, " (R%d -> %04X)",
                     (dAddr-tms9900.wp)>>1, REG((dAddr-tms9900.wp)>>1));
        }
    }

    mprintf (2, "\n");

    if (carry)
    {
        tms9900.st |= FLAG_C;
    }
}

void showCPUStatus(void)
{
    WORD i;

    mprintf (1, "CPU\n");
    mprintf (1, "===\n");
    mprintf (1, "st=%04X\nwp=%04X\npc=%04X\n", tms9900.st, tms9900.wp, tms9900.pc);

    for (i = 0; i < 16; i++)
    {
        mprintf (1, "R%02d: %04X ", i, REG(i));
        if ((i + 1) % 4 == 0)
            printf ("\n");
    }
}

void showScratchPad (void)
{
    WORD i, j;

    mprintf (1, "Scratchpad\n");
    mprintf (1, "==========");

    for (i = 0; i < 256; i += 16 )
    {
        mprintf (1, "\n%04X - ", i + 0x8300);

        for (j = i; j < i + 16; j += 2)
        {
            mprintf (1, "%04X ",
                    cpuRead ((WORD)(j+0x8300)));
        }
    }

    printf ("\n");
}

static void boot (void)
{
    blwp (0x0);  // BLWP @>0
}

static void loadRom (void)
{
    FILE *fp;
    int i;

    if ((fp = fopen ("../994arom.bin", "rb")) == NULL)
    {
        printf ("can't open ROM bin file\n");
        exit (1);
    }

    if (fread (tms9900.ram.w, sizeof (WORD), 0x1000, fp) != 0x1000)
    {
        halt ("ROM file read failure");
    }

    fclose (fp);

    if ((fp = fopen ("../module-c.bin", "rb")) == NULL)
    {
        printf ("can't open ROM bin file\n");
        exit (1);
    }

    if (fread (tms9900.ram.w + 0x3000, sizeof (WORD), 0x1000, fp) != 0x1000)
    {
        halt ("ROM file read failure");
    }

    for (i = 0; i < 0x4000; i++)
    {
        tms9900.ram.w[i] = SWAP (tms9900.ram.w[i]);
    }

    fclose (fp);

    printf ("ROM & MUNCHMN load ok\n");
}

static BOOL checkKey (void)
{
    static int c;

    if (kbhit())
    {
        printf ("Key pressed\n");

        c = getch();
    }

    if (!c)
        return 0;

    if (c == 3)// || c==32)
    {
        printf ("Break pressed\n");
        c = 0;
        return 1;
    }

    if (c == 'a')
    {
        printf ("Output level bumped down to %d\n", --outputLevel);
        c = 0;
        return 0;
    }

    if (c == 'b')
    {
        printf ("Output level bumped up to %d\n", ++outputLevel);
        c = 0;
        return 0;
    }

    if (tms9900.pc != 0x0478)
        return 0;

    printf ("Forcing key pressed...\n");

    cpuWriteB(AREG(0), c);
    cpuWrite(AREG(6), FLAG_EQ);

    showCPUStatus ();

    // keyboardSet (c);
    c = 0;

    return 0;
}

static void input (FILE *fp)
{
    char in[80];
    BOOL run = 0;
    WORD reg, data;
    long execCount = 0;
    long intCount = 0;
    // BOOL cover = 0;

    if (fp)
    {
        fgets (in, sizeof in, fp);
    }
    else
    {
        printf ("\nTMS9900 > ");
        fgets (in, sizeof in, stdin);
        printf ("\n");
    }

    switch (in[0])
    {
    case 'b':
        switch (in[1])
        {
        case 'a':
            breakPointAdd (&in[2]);
            break;
        case 'l':
            breakPointList ();
            break;
        case 'r':
            breakPointRemove (&in[2]);
            break;
        case 'c':
            breakPointCondition (&in[2]);
            break;

        default:
            printf (" * invalid *\n");
            break;
        }
        break;

    case 'w':
        switch (in[1])
        {
        case 'a':
            watchAdd (&in[2]);
            break;
        case 'l':
            watchList ();
            break;
        case 'r':
            watchRemove (&in[2]);
            break;

        default:
            printf (" * invalid *\n");
            break;
        }
        break;

    case 'c':
        switch (in[1])
        {
        case 'a':
            conditionAdd (&in[2]);
            break;
        case 'l':
            conditionList ();
            break;
        case 'r':
            conditionRemove (&in[2]);
            break;
    
        default:
            printf (" * invalid *\n");
            break;
        }
        break;

    case 's':
        showCPUStatus();
        break;
    case 'p':
        showScratchPad ();
        break;
    case 'g':
        run = 1;
        break;

    case 'm':
        sscanf (in+1, " %d %x", &reg, &data);
        cpuWrite (AREG(reg), (WORD) data);

        run = 1;
        // cover = 1;
        // break;

    case '\n':
        // tms9900.ram.covered[tms9900.pc>>1] = 1;
        execute (fetch());
        showCPUStatus ();
        showGromStatus ();
        break;

    case 'u':
        if (in[1] == 'v')
        {
            outputCovered = 1;
            mprintf (99, "Uncovered code only will be unassembled\n");
        }
        else
        {
            outputLevel = in[1] - '0';
            mprintf (99, "Level set to %d\n", outputLevel);
        }
        break;
    case 'q':
        exit (0);
        break;

    case 'v':
        vdpInitGraphics();
        break;

    default:
        printf (" * invalid *\n");
        break;
    }

    if (run)
    {
        while (!breakPointHit (tms9900.pc) &&
              // (!cover || tms9900.ram.covered[tms9900.pc>>1] == 1))
              1)
        {
            // tms9900.ram.covered[tms9900.pc>>1] = 1;

            if (checkKey())
                break;

    // gromIntegrity();
            execute (fetch());
    // gromIntegrity();

            if ((execCount++ % 100) == 0)
            {
                // printf ("Refresh....\n");
                vdpRefresh(0);
            }

                /*
                 *  Time for an interrupt ?
                 */

            if ((intCount++ % 100) == 0)
            {
                if ((tms9900.st & FLAG_MSK) > 0)
                {
                    /*
                     *  Clear bit 2 to indicate VDP interrupt
                     */

                    cruBitSet (0, 2, 0);

                    // printf ("Interrupt....\n");
                    blwp (4);
                }
            }

            watchShow();
            // showGromStatus();
        }
    }
}

int main (int argc, char *argv[])
{
    FILE *fp = NULL;

    loadRom ();
    loadGRom ();
    boot ();

    if (argc > 1)
    {
        if ((fp = fopen (argv[1], "r")) == NULL)
        {
            printf ("Can't open '%s'\n", argv[1]);
        }
    }

    while (1)
    {
        input (fp);

        if (feof (fp))
        {
            fclose (fp);
            fp = NULL;
        }
    }

    // return 0;
}

/*
        0x3C00: div
        0x3000: ldcr
        0x2000: coc
        0x2400: czc
        0x3800: mul
        0x0700: abs


|SZC  s,d|4000|-----***|1|Y|Set Zeros Corresponding            |
|SZCB s,d|5000|-----***|1|Y|Set Zeros Corresponding Bytes      |
|S    s,d|6000|---*****|1|N|Subtract                           |
|SB   s,d|7000|--******|1|N|Subtract Bytes                     |
|C    s,d|8000|-----***|1|N|Compare                            |
|CB   s,d|9000|--*--***|1|N|Compare Bytes                      |
|A    s,d|A000|---*****|1|Y|Add                                |
|AB   s,d|B000|--******|1|Y|Add Bytes                          |
|MOV  s,d|C000|-----***|1|Y|Move                               |
|MOVB s,d|D000|--*--***|1|Y|Move Bytes                         |
|SOC  s,d|E000|-----***|1|Y|Set Ones Corresponding             |
|SOCB s,d|F000|-----***|1|Y|Set Ones Corresponding Bytes       |

|LI   r,i|0200|-----***|8|N|Load Immediate                     |
|AI   r,i|0220|---*****|8|Y|Add Immediate                      |
|ANDI r,i|0240|-----***|8|Y|AND Immediate                      |
|ORI  r,i|0260|-----***|8|Y|OR Immediate                       |
|CI   r,i|0280|-----***|8|N|Compare Immediate                  |
|STST r  |02C0|--------|8|N|Store Status Register              |
|STWP r  |02A0|--------|8|N|Store Workspace Pointer            |
|LWPI i  |02E0|--------|8|N|Load Workspace Pointer Immediate   |
|LIMI i  |0300|*-------|8|N|Load Interrupt Mask Immediate      |

|IDLE    |0340|--------|7|N|Computer Idle                      |
|RSET    |0360|*-------|7|N|Reset                              |
|RTWP    |0380|????????|7|N|Return Workspace Pointer (4)       |
|CKON    |03A0|--------|7|N|Clock On                           |
|CKOF    |03C0|--------|7|N|Clock Off                          |
|LREX    |03E0|*-------|7|N|Load or Restart Execution          |

|BLWP s  |0400|--------|6|N|Branch & Load Workspace Ptr (3) (2)|
|B    s  |0440|--------|6|N|Branch (PC=d)                      |
|X    s  |0480|--------|6|N|Execute the instruction at s       |
|CLR  d  |04C0|--------|6|N|Clear                              |
|NEG  d  |0500|---*****|6|Y|Negate                             |
|INV  d  |0540|-----***|6|Y|Invert                             |
|INC  d  |0580|---*****|6|Y|Increment                          |
|INCT d  |05C0|---*****|6|Y|Increment by Two                   |
|DEC  d  |0600|---*****|6|Y|Decrement                          |
|DECT d  |0640|---*****|6|Y|Decrement by Two                   |
|BL   s  |0680|--------|6|N|Branch and Link (R11=PC,PC=s)      |
|SWPB d  |06C0|--------|6|N|Swap Bytes                         |
|SETO d  |0700|--------|6|N|Set to Ones                        |
|ABS  d  |0740|---*****|6|Y|Absolute value                     |

|SRA  r,c|0800|----****|5|Y|Shift Right Arithmetic (1)         |
|SRC  r,c|0800|----****|5|Y|Shift Right Circular (1)           |
|SRL  r,c|0900|----****|5|Y|Shift Right Logical (1)            |
|SLA  r,c|0A00|----****|5|Y|Shift Left Arithmetic (1)          |

|JMP  a  |1000|--------|2|N|Jump unconditionally               |
|JLT  a  |1100|--------|2|N|Jump if Less Than                  |
|JLE  a  |1200|--------|2|N|Jump if Low or Equal               |
|JEQ  a  |1300|--------|2|N|Jump if Equal                      |
|JHE  a  |1400|--------|2|N|Jump if High or Equal              |
|JGT  a  |1500|--------|2|N|Jump if Greater Than               |
|JNE  a  |1600|--------|2|N|Jump if Not Equal                  |
|JNC  a  |1700|--------|2|N|Jump if No Carry                   |
|JOC  a  |1800|--------|2|N|Jump On Carry                      |
|JNO  a  |1900|--------|2|N|Jump if No Overflow                |
|JL   a  |1A00|--------|2|N|Jump if Low                        |
|JH   a  |1B00|--------|2|N|Jump if High                       |
|JOP  a  |1C00|--------|2|N|Jump if Odd Parity                 |
|SBO  a  |1D00|--------|2|N|Set Bit to One                     |
|SBZ  a  |1E00|--------|2|N|Set Bit to Zero                    |
|TB   a  |1F00|-----*--|2|N|Test Bit                           |

|COC  s,r|2000|-----*--|3|N|Compare Ones Corresponding         |
|CZC  s,r|2400|-----*--|3|N|Compare Zeros Corresponding        |
|XOR  s,r|2800|-----***|3|N|Exclusive OR                       |

|XOP  s,c|2C00|-1------|9|N|Extended Operation (5) (2)         |
|LDCR s,c|3000|--*--***|4|Y|Load Communication Register        |
|STCR s,c|3400|--*--***|4|Y|Store Communication Register       |
|MPY  d,r|3800|--------|9|N|Multiply                           |
|DIV  d,r|3C00|---*----|9|N|Divide                             |
*/
