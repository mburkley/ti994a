/*
 * Copyright (c) 2004-2023 Mark Burkley.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 *  Dump the contents of a disk file
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "types.h"
#include "parse.h"
#include "files.h"
#include "decodebasic.h"

#define BYTES_PER_SECTOR        256

static FILE *diskFp;
static bool showContents = false;
static bool showBasic = false;

static struct
{
    char name[10];
    int16_t len;
    uint8_t flags;
    uint8_t recSec;
    int16_t alloc;
    uint8_t eof;
    uint8_t recLen;
    int16_t l3Alloc;
    char dtCreate[4];
    char dtUpdate[4];
    uint8_t chain[76][3];
}
fileHeader;

static struct
{
    char name[10];
    int16_t sectors;
    uint8_t secPerTrk;
    char dsk[3];
    // 0x10
    uint8_t protected; // 'P' = protected, ' '=not
    uint8_t tracks;
    uint8_t sides;
    uint8_t density; // SS=1,DS=2
    uint8_t tbd3; // reserved
    uint8_t fill1[11];
    // 0x20
    uint8_t date[8];
    uint8_t fill2[16];
    // 0x38
    uint8_t bitmap[0xc8]; // bitmap 38-64, 38-91 or 38-eb for SSSD,DSSD,DSDD respec
}
volumeHeader;

static char * showFlags (uint8_t flags)
{
    static char str[26];

    sprintf (str, "(%s%s%s%s%s%s)", 
        (flags & 0x80) ? "VAR":"FIX",
        (flags & 0x20) ? "-EMU" : "",
        (flags & 0x10) ? "-MOD" : "",
        (flags & 0x08) ? "-WP" : "",
        (flags & 0x02) ? "-BIN" : "-ASC",
        (flags & 0x01) ? "-PROG" : "-DATA");

    return str;
}

static void decodeChain (uint8_t chain[], uint16_t *p1, uint16_t *p2)
{
    *p1 = (chain[1]&0xF)<<8|chain[0];
    *p2 = chain[1]>>4|chain[2]<<4;
}

static void analyseFirstSector (void)
{
    fseek (diskFp, 0, SEEK_SET);

    fread (&volumeHeader, sizeof (volumeHeader), 1, diskFp);
    printf ("Vol-Label='%-10.10s'", volumeHeader.name);
    printf (", sectors=%d", ntohs (volumeHeader.sectors));
    printf (", sec/trk:%d", volumeHeader.secPerTrk);
    printf (", DSR:'%-3.3s'", volumeHeader.dsk);
    printf (", Protect:'%c'", volumeHeader.protected);
    printf (", tracks:%d", volumeHeader.tracks);
    printf (", sides:%d", volumeHeader.sides);
    printf (", density:%02X", volumeHeader.density);

    if (volumeHeader.date[0])
        printf (", year:%-8.8s", volumeHeader.date);

    printf (", FAT sectors used");

    #if 1
    int max = 0x64;
    if (volumeHeader.sides==2)
    {
        if (volumeHeader.density == 1)
            max = 0x91;
        else
            max = 0xeb;
    }

    bool inuse = false;
    for (int i = 0; i <= max - 0x38; i++)
    {
        uint8_t map=volumeHeader.bitmap[i];

        for (int j = 0; j < 8; j++)
        {
            bool bit = (map & (1<<j));

            if (!inuse && bit)
            {
                printf ("[%d-", i*8+j);
                inuse = true;
            }
            else if (inuse && !bit)
            {
                printf ("%d]", i*8+j-1);
                inuse = false;
            }
        }
    }
    if (inuse) printf ("%d]",(max-0x38+1)*8);
    #endif
    printf ("\n");
}

static void dumpContents (int sectorStart, int sectorCount, int recLen)
{
    if (!showContents)
        return;

    for (int i = sectorStart; i <= sectorStart+sectorCount; i++)
    {
        int8_t data[BYTES_PER_SECTOR];
        fseek (diskFp, BYTES_PER_SECTOR * i, SEEK_SET);

        fread (&data, sizeof (data), 1, diskFp);

        for (int j = 0; j < BYTES_PER_SECTOR; j += recLen)
        {
            printf ("\n\t'");
            for (int k = 0; k < recLen; k++)
            {
                printf ("%c", isalnum (data[j+k]) ? data[j+k] : '.');
            }
            printf ("'");
        }
    }
}
    
static int readProg (uint8_t *buff, int offset, int sectorStart, int sectorCount)
{
    for (int i = sectorStart; i <= sectorStart+sectorCount; i++)
    {
        fseek (diskFp, BYTES_PER_SECTOR * i, SEEK_SET);

        fread (&buff[offset], BYTES_PER_SECTOR, 1, diskFp);
        offset += BYTES_PER_SECTOR;
    }

    return offset;
}

static void analyseFile (int sector)
{
    int length;
    uint8_t *prog = NULL;
    int progBytes = 0;

    fseek (diskFp, BYTES_PER_SECTOR * sector, SEEK_SET);

    fread (&fileHeader, sizeof (fileHeader), 1, diskFp);
    printf ("%-10.10s", fileHeader.name);

    printf (" %6d", sector);

    if (fileHeader.flags & 0x01)
    {
        length = (ntohs (fileHeader.alloc) - 1) * BYTES_PER_SECTOR + fileHeader.eof;
        if (showBasic)
            prog = malloc (ntohs (fileHeader.alloc) * BYTES_PER_SECTOR);
    }
    else
        length = ntohs(fileHeader.len);

    printf (" %6d", length);

    printf (" %02X%-17.17s", fileHeader.flags,showFlags (fileHeader.flags));
    printf (" %8d", ntohs(fileHeader.alloc));
    printf (" %8d", fileHeader.recSec * ntohs (fileHeader.alloc));
    printf (" %10d", fileHeader.eof);
    printf (" %7d", ntohs(fileHeader.l3Alloc));
    printf (" %7d", fileHeader.recLen);

    for (int i = 0; i < 23; i++)
    {
        uint8_t *chain=fileHeader.chain[i];
        if (chain[0] != 0 || chain[1] != 0 || chain[2] !=0)
        {
            uint16_t start, len;
            decodeChain (chain, &start, &len);
            printf ("%s%2d=(%d-%d)", i!=0?",":"", i, start, start+len);

            if (prog)
            {
                progBytes = readProg (prog, progBytes, start, len);
            }
            else
                dumpContents (start, len, fileHeader.recLen);
        }
    }

    if (prog)
        decodeBasicProgram (prog, length);

    printf ("\n");
}

static void analyseDirectory (int sector)
{
    uint8_t data[BYTES_PER_SECTOR/2][2];

    fseek (diskFp, BYTES_PER_SECTOR * sector, SEEK_SET);

    fread (&data, sizeof (data), 1, diskFp);

    printf ("Name       Sector Len    Flags               #Sectors #Records EOF-offset L3Alloc Rec-Len Sectors\n");
    printf ("========== ====== ====== =================== ======== ======== ========== ======= ======= =======\n");
    for (int i = 0; i < BYTES_PER_SECTOR/2; i++)
    {
        sector = data[i][0] << 8 | data[i][1];
        if (sector == 0)
            break;
        analyseFile (sector);
    }
}

int main (int argc, char *argv[])
{
    char c;

    while ((c = getopt(argc, argv, "db")) != -1)
    {
        switch (c)
        {
            case 'd' : showContents = true; break;
            case 'b' : showBasic = true; break;
            default: printf ("Unknown option '%c'\n", c);
        }
    }

    if (argc - optind < 1)
    {
        printf ("\nSector dump disk file read tool\n\n"
                "usage: %s [-d] [-b] <dsk-file>\n"
                "\t where -d=dump HEX, -b=decode basic\n\n", argv[0]);
        return 1;
    }

    if ((diskFp = fopen (argv[optind], "r")) == NULL)
    {
        printf ("Can't open %s\n", argv[1]);
        return 0;
    }

    analyseFirstSector ();
    analyseDirectory (1);

    fclose (diskFp);

    return 0;
}
