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
#define _FILE_OFFSET_BITS 64

#include <string.h>
#include <errno.h>

#ifndef _MSC_VER
#include <stdbool.h>
#else
typedef int bool;
#define true 1
#define false 0

typedef __int64 off_t;

#define fseeko _fseeki64
#define ftello _ftelli64

#endif /* __MSC_VER */

#include "pdb.h"

const char PDB_SIGNATURE_V2[] = "Microsoft C/C++ program database 2.00\r\n";
const char PDB_SIGNATURE_V7[] = "Microsoft C/C++ MSF 7.00\r\n";

#define PDB_HEADER_SIZE_V2 (sizeof(PDB_SIGNATURE_V2) + 4)
#define PDB_HEADEr_SIZE_V7 (sizeof(PDB_SIGNATURE_V7) + 5)


struct PDB_FILE
{
	char* name; // file name
	FILE* file;
	uint8_t version; // version from the header (2 or 7 are known)
	uint16_t streamCount; // number of streams in the file
	uint32_t pageSize; // bytes per page
	uint32_t pageCount; // total file bytes / page bytes
	uint32_t flagPage;

	PDB_STREAM* root;
};


struct PDB_STREAM
{
	PDB_FILE* pdb;
	uint32_t* pages; // The indices of the pages comprising the stream
	uint64_t currentPage; // The current page index
	uint32_t pageCount; // Total pages in this stream
};


static bool PdbCheckFileSize(PDB_FILE* pdb)
{
	off_t currentOffset;

	// Don't divide by zero
	if (pdb->pageSize == 0)
		return false;

	// Preserve the current offset, we are going to validate the file size
	currentOffset = ftello(pdb->file);

	// Goto the end of the file
	if (fseeko(pdb->file, 0, SEEK_END))
		return false;

	// See if the size yields the expected number of pages
	if ((ftello(pdb->file) / pdb->pageSize) != pdb->pageCount)
		return false;

	// Return to the saved offset
	if (fseeko(pdb->file, currentOffset, SEEK_SET))
		return false;

	return true;
}


static uint32_t GetPageCount(PDB_FILE* pdb, uint32_t bytes)
{
	// Watch out for div0
	if (pdb->pageSize == 0)
		return 0;

	// Round up in cases where it isn't a multiple of the page size
	return (bytes / pdb->pageSize) + ((bytes % pdb->pageSize != 0) ? 1 : 0);
}


static PDB_STREAM* PdbOpenRootStream(PDB_FILE* pdb, uint32_t* pages, uint32_t pageCount)
{
	PDB_STREAM* root = (PDB_STREAM*)malloc(sizeof(PDB_STREAM));

	root->pdb = pdb;
	root->pages = pages;
	root->pageCount = pageCount;
	root->currentPage = 0;

	return root;
}


static bool PdbSeekToStreamInfo(PDB_FILE* pdb, uint32_t streamId)
{
	uint32_t i;

	for (i = 0; i < streamId - 1; i++)
	{
		uint32_t pageCount;

		// Get the number of pages in each stream
		if (fread(&pageCount, 1, 4, pdb->file) != 4)
			return false;

		// Skip past the page indices
		if (fseeko(pdb->file, pageCount * 4, SEEK_CUR))
			return false;
	}

	return true;
}


static bool PdbSeekToStream(PDB_STREAM* stream, off_t offset)
{
	if (stream->pageCount)
	{
		uint64_t page = offset / stream->pdb->pageSize;

		// Sanity check the offset
		if (page > stream->pageCount)
			return false;

		// Goto the page containing the requested offset
		if (fseeko(stream->pdb->file,
			stream->pages[page] * stream->pdb->pageSize, SEEK_SET))
			return false;

		return true;
	}

	return false;
}


PDB_STREAM* PdbOpenStream(PDB_FILE* pdb, uint32_t streamId)
{
	PDB_STREAM* stream = (PDB_STREAM*)malloc(sizeof(PDB_STREAM));
	uint32_t i;

	stream->pdb = pdb;

	// Need to go to the root stream to get the stream info
	if (!PdbSeekToStream(pdb->root, 0))
	{
		free(stream);
		return NULL;
	}

	// Seek to the stream info
	if (!PdbSeekToStreamInfo(pdb, streamId))
	{
		free(stream);
		return NULL;
	}

	// Get the number of pages in the stream
	if (fread(&stream->pageCount, 1, 4, pdb->file) != 4)
	{
		free(stream);
		return NULL;
	}

	// Read in the indices making up the stream
	for (i = 0; i < stream->pageCount; i++)
	{
		if (fread(&stream->pages[i], 1, 4, pdb->file) != 4)
		{
			free(stream);
			return NULL;
		}
	}

	// Seek to the first page of the stream
	if (!PdbSeekToStream(stream, 0))
	{
		free(stream);
		return NULL;
	}

	return stream;
}


void PdbCloseStream(PDB_STREAM* stream)
{
	free(stream->pages);
	free(stream);
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
			uint32_t* rootPages;
			uint32_t rootSize, pageCount, i;

			pdb->version = 7;

			// Expecting [unknown byte]DS\0\0
			if (fread(buff, 1, 5, pdb->file) != 5)
				return false;

			// Read the size of the pages in bytes (Probably 0x400)
			if (fread(&pdb->pageSize, 1, 4, pdb->file) != 4)
				return false;
	
			// Get the flag page (an allocation table, 1 if the page is unused)
			if (fread(&pdb->flagPage, 1, 4, pdb->file) != 4)
				return false;

			// Get number of pages in the file
			if ((fread(&pdb->pageCount, 1, 4, pdb->file) != 4))
				return false;

			// Ensure that this matches the actual file size
			if (!PdbCheckFileSize(pdb))
				return false;

			// Get the root stream size (in bytes)
			if (fread(&rootSize, 1, 4, pdb->file) != 4)
				return false;

			// Pass reserved dword
			if (fread(buff, 1, 4, pdb->file) != 4)
				return false;

			// Calculate the number of pages comprising the root stream
			pageCount = GetPageCount(pdb, rootSize);

			// Ensure that it's even a sane value (the header probably can't
			// exceed the size of a single page
			if ((pageCount * 4) > (pdb->pageSize - ftello(pdb->file)))
				return false;

			// Allocate storage for the pdb's root page list
			rootPages = (uint32_t*)malloc(pdb->pageCount * sizeof(uint32_t));

			// Get the root stream pages
			for (i = 0; i < pageCount; i++)
			{
				if (fread(&rootPages[i], 1, 4, pdb->file) != 4)
					return false;
			}

			// Open the root stream (the pdb now owns rootPages storage)
			pdb->root = PdbOpenRootStream(pdb, rootPages, pageCount);

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

	// Read the header and open the root stream
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
	fclose(pdb->file);
	free(pdb->name);
	free(pdb);
}
