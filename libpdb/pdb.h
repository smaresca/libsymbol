/*
Copyright (c) 2010, Ryan Salsamendi
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the organization nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL RYAN SALSAMENDI BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


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


#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

	PDBAPI PDB_FILE* PdbOpen(const char* name);
	PDBAPI void PdbClose(PDB_FILE* pdb);

	PDBAPI PDB_STREAM* PdbStreamOpen(PDB_FILE* pdb, uint32_t streamId);
	PDBAPI void PdbStreamClose(PDB_STREAM* stream);

	PDBAPI bool PdbStreamRead(PDB_STREAM* stream, uint8_t* buff, uint64_t bytes);
	PDBAPI bool PdbStreamSeek(PDB_STREAM* stream, uint64_t offset);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDB_H__ */	