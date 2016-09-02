/*
  fileio.h - file i/o handler
  Copyright (C) Yann Collet 2013-2016

  GPL v2 License

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

  You can contact the author at :
  - ZSTD homepage : http://www.zstd.net/
*/
#pragma once

#if defined (__cplusplus)
extern "C" {
#endif


/* *************************************
*  Special i/o constants
**************************************/
#define stdinmark "stdin"
#define stdoutmark "stdout"
#ifdef _WIN32
#  define nulmark "nul"
#else
#  define nulmark "/dev/null"
#endif


/*-*************************************
*  Parameters
***************************************/
void FIO_overwriteMode(void);
void FIO_setNotificationLevel(unsigned level);
void FIO_setMaxWLog(unsigned maxWLog);     /**< if `maxWLog` == 0, no max enforced */
void FIO_setSparseWrite(unsigned sparse);  /**< 0: no sparse; 1: disable on stdout; 2: always enabled */
void FIO_setDictIDFlag(unsigned dictIDFlag);
void FIO_setChecksumFlag(unsigned checksumFlag);
void FIO_setRemoveSrcFile(unsigned flag);


/*-*************************************
*  Single File functions
***************************************/
/** FIO_compressFilename() :
    @return : 0 == ok;  1 == pb with src file. */
int FIO_compressFilename (const char* outfilename, const char* infilename, const char* dictFileName, int compressionLevel);

/** FIO_decompressFilename() :
    @return : 0 == ok;  1 == pb with src file. */
int FIO_decompressFilename (const char* outfilename, const char* infilename, const char* dictFileName);


/*-*************************************
*  Multiple File functions
***************************************/
/** FIO_compressMultipleFilenames() :
    @return : nb of missing files */
int FIO_compressMultipleFilenames(const char** srcNamesTable, unsigned nbFiles,
                                  const char* suffix,
                                  const char* dictFileName, int compressionLevel);

/** FIO_decompressMultipleFilenames() :
    @return : nb of missing or skipped files */
int FIO_decompressMultipleFilenames(const char** srcNamesTable, unsigned nbFiles,
                                    const char* suffix,
                                    const char* dictFileName);


#if defined (__cplusplus)
}
#endif
