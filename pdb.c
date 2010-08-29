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
*/

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>


typedef struct PDB_FILE PDB_FILE;

const char PDB_SIGNATURE_V2[] = "Microsoft C/C++ program database 2.00\r\n";
const char PDB_SIGNATURE_V7[] = "Microsoft C/C++ MSF 7.00\r\n";


struct PDB_FILE
{
	char* name; // file name
	FILE* file;
	uint8_t version; // version from the header (2 or 7 are known)
	uint16_t streamCount; // number of streams in the file
	off_t pageSize; // bytes per page
	off_t pageCount; // total file bytes / page bytes
};


static void PrintHelp()
{
	fprintf(stderr, "Usage: pdbp [pdb file]\n");
}


static bool PdbParseHeader(PDB_FILE* pdb)
{
	char buff[sizeof(PDB_SIGNATURE_V2) + 1];

	// First try to read the longer (older) signature
	if (fread(buff, 1, sizeof(PDB_SIGNATURE_V2), pdb->file) == sizeof(PDB_SIGNATURE_V2))
	{
		// See if we have a match
		if (memcmp(PDB_SIGNATURE_V2, buff, sizeof(PDB_SIGNATURE_V2) - 1) == 0)
		{
			pdb->version = 2;

			// Expecting [unknown byte]JG\0
			if (fread(buff, 1, 4, pdb->file) != 4)
				return false;

			// Read the size of the pages in bytes (Hopefully 0x400,0x800, or 0x1000)
			if (fread(&pdb->pageSize, 1, 4, pdb->file) != 4)
				return false;
		
			return true;
		}

		// Now check for the newer format
		if (memcmp(PDB_SIGNATURE_V7, buff, sizeof(PDB_SIGNATURE_V7) - 1) == 0)
		{
			off_t currentOffset;

			pdb->version = 7;

			// Expecting [unknown byte]DS\0\0
			if (fread(buff, 1, 5, pdb->file) != 5)
				return false;

			// Read the size of the pages in bytes (Probably 0x400)
			if (fread(&pdb->pageSize, 1, 4, pdb->file) != 4)
				return false;
	
			// Validate and pass "Flag Page" (don't care)
			if ((fread(buff, 1, 4, pdb->file) != 4) || (*(uint32_t*)buff != 2))
				return false;

			// Number of pages in the file
			if ((fread(&pdb->pageCount, 1, 4, pdb->file) != 4))
				return false;

			// Preserve the current offset, we are going to validate the file size
			currentOffset = ftello(pdb->file);

			// Goto the end of the file
			if (fseeko(pdb->file, 0, SEEK_END))
				return false;

			// See if the size yields the expected number of pages
			if ((ftello(pdb->file) * pdb->pageSize) != pdb->pageCount)
				return false;

			// Return to the saved offset
			if (fseeko(pdb->file, currentOffset, SEEK_SET))
				return false;
	
			return true;
		}
	}


	return false;
}


PDB_FILE* PdbOpen(const char* name)
{
	FILE* file = fopen(name, "rb");
	PDB_FILE* pdb;

	if (!file)
	{
		fprintf(stderr, "Failed to open pdb file.  OS reports: %s\n", strerror(errno));
		return NULL;
	}
	
	pdb = (PDB_FILE*)malloc(sizeof(PDB_FILE));

	// Initialize
	pdb->name = strdup(name);
	pdb->file = file;
	pdb->version = 0;
	pdb->streamCount = 0;
	pdb->pageSize = 0;
	pdb->pageCount = 0;

	// Verify the PDB signature
	if (!PdbParseHeader(pdb))
	{
		free(pdb->name);
		fclose(pdb->file);
		free(pdb);

		return NULL;
	}

	return pdb;
}


void PdbClose(PDB_FILE* pdb)
{
	if (!pdb)
		return;

	fclose(pdb->file);
	free(pdb->name);
	free(pdb);
}


int main(int argc, char** argv)
{
	PDB_FILE* pdb;

	if (argc < 2)
	{
		PrintHelp();
		return 1;
	}

	pdb = PdbOpen(argv[1]);
	if (pdb)
		fprintf(stderr, "Successfully opened pdb.\n");

	PdbClose(pdb);

	return 0;
}
