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


#include <string.h>
#include <errno.h>

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
	uint32_t streamCount; // number of streams in the file
	uint32_t pageSize; // bytes per page
	uint32_t pageCount; // total file bytes / page bytes
	uint32_t flagPage;

	PDB_STREAM* root;
	PDB_STREAM* lastAccessed;
};


struct PDB_STREAM
{
	PDB_FILE* pdb;
	uint32_t* pages; // The indices of the pages comprising the stream
	uint64_t currentOffset; // The current offset
	uint32_t pageCount; // Total pages in this stream
	uint32_t size; // Total bytes in the stream
};


static bool PdbCheckFileSize(PDB_FILE* pdb)
{
	off_t currentOffset;
	off_t fileSize;
	uint64_t expectedPages;

	// Don't divide by zero
	if (pdb->pageSize == 0)
		return false;

	// Preserve the current offset, we are going to validate the file size
	currentOffset = ftello(pdb->file);

	// Goto the end of the file
	if (fseeko(pdb->file, 0, SEEK_END))
		return false;

	fileSize = ftello(pdb->file);

	// Calculate the expected file size
	expectedPages = ((uint64_t)fileSize / pdb->pageSize)
		+ ((fileSize % pdb->pageSize) ? 1 : 0);

	// See if the size yields the expected number of pages
	if (expectedPages != pdb->pageCount)
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


uint32_t PdbGetStreamCount(PDB_FILE* pdb)
{
	return pdb->streamCount;
}


static bool PdbCountStreams(PDB_FILE* pdb)
{
	pdb->streamCount = 0;

	// Seek to the beginning of the root stream
	if (!PdbStreamSeek(pdb->root, 0))
		return false;

	// Read the first dword, it's the number of streams in the file
	if (!PdbStreamRead(pdb->root, (uint8_t*)&pdb->streamCount, sizeof(pdb->streamCount)))
		return false;

	return true;
}


static bool PdbStreamOpenRoot(PDB_FILE* pdb, uint32_t* pages, uint32_t pageCount, uint32_t size)
{
	PDB_STREAM* root = (PDB_STREAM*)malloc(sizeof(PDB_STREAM));

	pdb->root = root;
	root->pdb = pdb;
	root->pages = pages;
	root->pageCount = pageCount;
	root->currentOffset = 0;
	root->size = size;

	// Get a count of the streams in this file
	if (!PdbCountStreams(pdb))
	{
		free(root);
		pdb->root = NULL;
		return false;
	}

	return true;
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


bool PdbStreamSeek(PDB_STREAM* stream, uint64_t offset)
{
	if (stream->pageCount)
	{
		uint64_t page;
		uint64_t fileOffset;

		// Avoid div0
		if (stream->pdb->pageSize == 0)
			return false;

		// Sanity check the offset
		if (offset > stream->size)
			return false;

		// Calculate the page number within the stream this offset appears at
		page = offset / stream->pdb->pageSize;

		// Use the stream page number to lookup the page index within the file and then 
		// to calculate the offset of the page within the file
		// It may not be an even multiple of page size, so add back the remainder
		fileOffset = (stream->pages[page] * stream->pdb->pageSize)
			+ ((offset % stream->pdb->pageSize != 0) ? 1 : 0);

		// Goto the page containing the requested offset
		if (fseeko(stream->pdb->file, fileOffset, SEEK_SET))
			return false;

		// Update last accessed
		stream->pdb->lastAccessed = stream;

		return true;
	}

	return false;
}


PDB_STREAM* PdbStreamOpen(PDB_FILE* pdb, uint32_t streamId)
{
	PDB_STREAM* stream = (PDB_STREAM*)malloc(sizeof(PDB_STREAM));
	uint32_t i;

	stream->pdb = pdb;

	// Need to go to the root stream to get the stream info
	if (!PdbStreamSeek(pdb->root, 0))
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

	// Read the number of bytes in the stream
	if (fread(&stream->size, 1, 4, pdb->file) != 4)
	{
		free(stream);
		return NULL;
	}

	// Calculate the number of pages needed
	stream->pageCount = (stream->size / pdb->pageSize)
		+ ((stream->size % pdb->pageSize != 0) ? 1 : 0);

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
	if (!PdbStreamSeek(stream, 0))
	{
		free(stream);
		return NULL;
	}

	return stream;
}


void PdbStreamClose(PDB_STREAM* stream)
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
			uint32_t rootSize, rootPageCount, i;
			off_t offs;

			pdb->version = 7;

			// We went past the end of the signature because the V2 sig is
			// larger than the V7 sig.
			if (fseeko(pdb->file, sizeof(PDB_SIGNATURE_V7) - 1, SEEK_SET))
				return false;

			// Expecting reserved bytes, something like [unknown byte]DS\0\0\0
			if (fread(buff, 1, 6, pdb->file) != 6)
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
			rootPageCount = GetPageCount(pdb, rootSize);

			// Ensure that it's even a sane value (the header probably can't
			// exceed the size of a single page
			if ((rootPageCount * 4) > (pdb->pageSize - ftello(pdb->file)))
				return false;

			// Allocate storage for the pdb's root page list
			rootPages = (uint32_t*)malloc(pdb->pageCount * sizeof(uint32_t));

			// Get the root stream pages
			for (i = 0; i < rootPageCount; i++)
			{
				if (fread(&rootPages[i], 1, 4, pdb->file) != 4)
					return false;
			}

			// Open the root stream (the pdb now owns rootPages storage)
			if (!PdbStreamOpenRoot(pdb, rootPages, rootPageCount, rootSize))
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
	pdb->lastAccessed = NULL;
	pdb->root = NULL;

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


bool PdbStreamRead(PDB_STREAM* stream, uint8_t* buff, uint64_t bytes)
{
	uint8_t* pbuff = buff;
	uint64_t bytesRemaining = bytes;
	size_t bytesLeftOnPage;

	// Ensure that the requested bytes don't run off the end of the stream
	if (stream->currentOffset + bytes >= stream->size)
		return false;

	// Assume that if this is the last accessed stream, that
	// we are already at the current offset.  Otherwise make it so.
	if (stream->pdb->lastAccessed != stream)
	{
		// Some other stream was read last, seek to this stream
		if (!PdbStreamSeek(stream, stream->currentOffset))
			return false;
	}
	stream->pdb->lastAccessed = stream;

	// Calculate how many bytes are left on the current page, starting from the current offset
	bytesLeftOnPage = (stream->pdb->pageSize - (stream->currentOffset % stream->pdb->pageSize));

	// Now read the remaining pages
	while (bytesRemaining)
	{
		size_t bytesToRead;

		// We can only read a page at a time
		// The first page may be shorter if the current offset is not at the beginning of the page
		bytesToRead = ((size_t)bytesRemaining < bytesLeftOnPage) 
			? (size_t)bytesRemaining : bytesLeftOnPage;

		if (fread(pbuff, 1, bytesToRead, stream->pdb->file) != bytesToRead)
			return false;

		pbuff += bytesToRead;
		bytesRemaining -= bytesToRead;

		// Seek to the requested position
		stream->currentOffset += bytesToRead;
		if (!PdbStreamSeek(stream, stream->currentOffset))
			return false;

		// Subsequent reads begin at the page offset 0, so they
		// will read an entire page
		bytesLeftOnPage = stream->pdb->pageSize;
	}

	return true;
}
