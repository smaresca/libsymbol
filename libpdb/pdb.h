/*
Copyright (c) 2010 Ryan Salsamendi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

I would like to thank and give credit to the following references I consulted while
writing this lib:

http://moyix.blogspot.com/2007/08/pdb-stream-decomposition.html
http://undocumented.rawol.com/ (Sven Boris Schreiber's site, of Undocumented Windows 2000 Secrets fame).

*/
#ifndef __PDB_H__
#define __PDB_H__

#ifdef _MSC_VER
	#ifdef LIBPDB_EXPORTS
		#define PDBAPI __declspec(dllexport)
	#else
		#define PDBAPI __declspec(dllimport)
	#endif /* LIBPDB_EXPORTS */

	typedef int bool;
	#define true 1
	#define false 0

	typedef __int64 off_t;

	#define fseeko _fseeki64
	#define ftello _ftelli64

#else // Linux

	#include <stdbool.h>

	#define PDBAPI __attribute__ ((visibility("default")))
#endif /* _MSC_VER */

#define _FILE_OFFSET_BITS 64


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


typedef struct PDB_FILE PDB_FILE;
typedef struct PDB_STREAM PDB_STREAM;
typedef enum PDB_STREAMS PDB_STREAMS;

enum PDB_STREAMS
{
	PDB_STREAM_ROOT = 0,
	PDB_STREAM_PROGRAM_INFO = 1,
	PDB_STREAM_TYPE_INFO = 2,
	PDB_STREAM_DEBUG_INFO = 3
};


#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

	PDBAPI PDB_FILE* PdbOpen(const char* name);
	PDBAPI void PdbClose(PDB_FILE* pdb);
	PDBAPI uint32_t PdbGetStreamCount(PDB_FILE* pdb);

	PDBAPI PDB_STREAM* PdbStreamOpen(PDB_FILE* pdb, uint32_t streamId);
	PDBAPI void PdbStreamClose(PDB_STREAM* stream);

	PDBAPI PDB_FILE* PdbStreamGetPdb(PDB_STREAM* stream);
	PDBAPI uint32_t PdbStreamGetSize(PDB_STREAM* stream);
	PDBAPI bool PdbStreamRead(PDB_STREAM* stream, uint8_t* buff, uint64_t bytes);
	PDBAPI bool PdbStreamSeek(PDB_STREAM* stream, uint64_t offset);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDB_H__ */	
