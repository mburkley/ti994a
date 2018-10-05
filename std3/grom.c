#include <stdio.h>
#include <stdlib.h>

#include "grom.h"

struct
{
    WORD addr;
    BYTE lowByteGet;
    BYTE lowByteSet;

    union
    {
        // WORD w[32768];
        // BYTE b[65536];
        WORD w[0x6000];
        BYTE b[0xC000];
    }
    rom;
}
gRom;

void gromIntegrity (void)
{
    if (gRom.rom.b[0x1bc] != 0xbe)
    {
        halt ("GROM corruption\n");
    }

}

BYTE gromRead (void)
{
    BYTE result;

    // gromIntegrity();
    gRom.lowByteGet = 0;
    gRom.lowByteSet = 0;
    // mprintf (0, "GROMAD lo byte Clr\n");

    if (gRom.addr > 0x7FFF)
    {
        result = 0;
    }
    else
    {
        result = gRom.rom.b[gRom.addr];
    }

    // mprintf (1, "GROMRead: %04X : %02X\n",
    //          (unsigned) gRom.addr,
    //          (unsigned) result);

    gRom.addr++;

    return result;
}

void gromSetAddr (WORD addr)
{
    if (gRom.lowByteSet)
    {
        gRom.addr = gRom.addr & 0xFF00 | addr;
        gRom.lowByteSet = 0;
        gRom.lowByteGet = 0;
        // mprintf (1, "GROMAD addr set to %04X\n", gRom.addr);
    }
    else
    {
        gRom.addr = addr << 8;
        gRom.lowByteSet = 1;
        // mprintf (1, "GROMAD lo byte Set\n");
    }
}

BYTE gromGetAddr (void)
{
    if (gRom.lowByteGet)
    {
        gRom.lowByteGet = 0;
        // mprintf (1, "GROMAD addr get as %04X\n", gRom.addr + 1);
        return (gRom.addr + 1) & 0xFF;
    }

    gRom.lowByteGet = 1;
    // mprintf (1, "GROMAD lo byte Get\n");
    return (gRom.addr + 1) >> 8;
}

void showGromStatus (void)
{
    mprintf (1, "GROM\n");
    mprintf (1, "====\n");

    mprintf (1, "addr        : %04X\n", gRom.addr);
    mprintf (1, "half-ad-set : %d\n", gRom.lowByteSet);
    mprintf (1, "half-ad-get : %d\n", gRom.lowByteGet);
}

void loadGRom (void)
{
    FILE *fp;
    int size;

    if ((fp = fopen ("../994agrom.bin", "rb")) == NULL)
    {
        printf ("can't open grom.hex\n");
        exit (1);
    }

    if (fread (gRom.rom.b, sizeof (BYTE), 0x6000, fp) != 0x6000)
    {
        halt ("GROM file read failure");
    }

    fclose (fp);

    if ((fp = fopen ("../module-g.bin", "rb")) == NULL)
    {
        printf ("can't open munchmng\n");
        exit (1);
    }

    size = fread (gRom.rom.b + 0x6000, sizeof (BYTE), 0x2000, fp);

    if (size != 0x2000 && size != 0x6000)
    {
        halt ("GROM file read failure");
    }

    fclose (fp);
    printf ("GROM load ok\n");
}


