/* Frozen test reference: the three original MiniZip scalar readers from
   lib/minizip/unzip.c at 9160120df7630b479747352b3e4773e544b64b4d.
   This file intentionally retains their byte-at-a-time implementation and EOF
   behavior. It must remain independent of subsequent production optimizations.
   Extracted for this regression fixture; the original MiniZip license follows. */
/* unzip.h -- IO for uncompress .zip files using zlib
   Version 1.1, February 14h, 2010
   part of the MiniZip project - ( http://www.winimage.com/zLibDll/minizip.html )

         Copyright (C) 1998-2010 Gilles Vollant (minizip) ( http://www.winimage.com/zLibDll/minizip.html )

         Modifications of Unzip for Zip64
         Copyright (C) 2007-2008 Even Rouault

         Modifications for Zip64 support on both zip and unzip
         Copyright (C) 2009-2010 Mathias Svensson ( http://result42.com )

         For more info read MiniZip_info.txt

         ---------------------------------------------------------------------------------

        Condition of use and distribution are the same than zlib :

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/

/* ===========================================================================
   Reads a long in LSB order from the given gz_stream. Sets
*/
local int unz64local_getShort OF((
    const zlib_filefunc64_32_def* pzlib_filefunc_def,
    voidpf filestream,
    uLong *pX));

local int unz64local_getShort (const zlib_filefunc64_32_def* pzlib_filefunc_def,
                             voidpf filestream,
                             uLong *pX)
{
    uLong x ;
    int i = 0;
    int err;

    err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x = (uLong)i;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((uLong)i)<<8;

    if (err==UNZ_OK)
        *pX = x;
    else
        *pX = 0;
    return err;
}

local int unz64local_getLong OF((
    const zlib_filefunc64_32_def* pzlib_filefunc_def,
    voidpf filestream,
    uLong *pX));

local int unz64local_getLong (const zlib_filefunc64_32_def* pzlib_filefunc_def,
                            voidpf filestream,
                            uLong *pX)
{
    uLong x ;
    int i = 0;
    int err;

    err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x = (uLong)i;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((uLong)i)<<8;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((uLong)i)<<16;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x += ((uLong)i)<<24;

    if (err==UNZ_OK)
        *pX = x;
    else
        *pX = 0;
    return err;
}

local int unz64local_getLong64 OF((
    const zlib_filefunc64_32_def* pzlib_filefunc_def,
    voidpf filestream,
    ZPOS64_T *pX));


local int unz64local_getLong64 (const zlib_filefunc64_32_def* pzlib_filefunc_def,
                            voidpf filestream,
                            ZPOS64_T *pX)
{
    ZPOS64_T x ;
    int i = 0;
    int err;

    err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x = (ZPOS64_T)i;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<8;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<16;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<24;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<32;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<40;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<48;

    if (err==UNZ_OK)
        err = unz64local_getByte(pzlib_filefunc_def,filestream,&i);
    x |= ((ZPOS64_T)i)<<56;

    if (err==UNZ_OK)
        *pX = x;
    else
        *pX = 0;
    return err;
}

