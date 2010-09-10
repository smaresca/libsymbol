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

*/
#include "pdb.h"
#include "type_stream.h"


#define PDB_VERSION_VC2                 19941610
#define PDB_VERSION_VC4                 19950623
#define PDB_VERSION_VC41                19950814
#define PDB_VERSION_VC50                19960307
#define PDB_VERSION_VC60                19970604
#define PDB_VERSION_VC70                19990604
#define PDB_VERSION_VC71                20000404
#define PDB_VERSION_VC8                 20040203


typedef struct PDB_TYPES_STREAM_HASH_ENTRY
{
	uint32_t offset;
	uint32_t size;
} PDB_TYPES_HASH_ENTRY;

typedef struct PDB_TYPES_HASH
{
	PDB_STREAM* stream;
	uint32_t keySize;
	uint32_t buckets;
	PDB_TYPES_HASH_ENTRY hashValues;
	PDB_TYPES_HASH_ENTRY typeOffsets;
	PDB_TYPES_HASH_ENTRY hashAdjustment;
} PDB_TYPES_HASH;

typedef struct TYPE_STREAM_HEADER
{
	uint32_t version;
	uint32_t size; // header size
	uint32_t minIndex;
	uint32_t maxIndex;
	uint32_t dataSize; // bytes after the header
	PDB_TYPES_HASH hash;
} TYPE_STREAM_HEADER;


typedef struct PDB_TYPES
{
	PDB_STREAM* stream;
	uint32_t version;
	uint32_t minId;
	uint32_t maxId;
	PDB_TYPES_HASH* hash;
} PDB_TYPES;


static PDB_TYPES_HASH* PdbTypesHashOpen(PDB_TYPES* types, uint32_t hashStreamId)
{
	PDB_TYPES_HASH* hash = (PDB_TYPES_HASH*)malloc(sizeof(PDB_TYPES_HASH));
	hash->stream = PdbStreamOpen(PdbStreamGetPdb(types->stream), hashStreamId);

	if (!hash->stream)
	{
		free(hash);
		return NULL;
	}

	// Get the size of the key
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->keySize, 4))
		return false;

	// Get the number of buckets in the hash
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->buckets, 4))
		return false;



	
	return hash;
}


static void PdbTypesHashClose(PDB_TYPES_HASH* hash)
{
	PdbStreamClose(hash->stream);
	free(hash);
}


PDB_TYPES* PdbTypesOpen(PDB_FILE* pdb)
{
	PDB_TYPES* types;
	uint32_t version;
	uint32_t headerSize;
	uint32_t dataSize;
	uint32_t hashStreamId;

	// Get the types stream
	PDB_STREAM* stream = PdbStreamOpen(pdb, PDB_STREAM_TYPE_INFO);

	// Read version
	if (!PdbStreamRead(stream, (uint8_t*)&version, 4))
		return NULL;

	// Check the version
	if ((version != PDB_VERSION_VC2)
		|| (version != PDB_VERSION_VC2)
		|| (version != PDB_VERSION_VC4)
		|| (version != PDB_VERSION_VC41)
		|| (version != PDB_VERSION_VC50)
		|| (version != PDB_VERSION_VC60)
		|| (version != PDB_VERSION_VC70)
		|| (version != PDB_VERSION_VC71)
		|| (version != PDB_VERSION_VC8))
	{
		// Can't support this version
		PdbStreamClose(stream);
		return NULL;
	}

	types = (PDB_TYPES*)malloc(sizeof(PDB_TYPES));
	types->version = version;
	types->stream = stream;
	types->hash = NULL;

	// Get the header size, for sanity checking purposes
	if (!PdbStreamRead(types->stream, (uint8_t*)&headerSize, 4))
		return false;

	// Get the minimum type index
	if (!PdbStreamRead(types->stream, (uint8_t*)&types->minId, 4))
		return false;

	// Get the maximum type index
	if (!PdbStreamRead(types->stream, (uint8_t*)&types->maxId, 4))
		return false;

	// Get the size of the data following the header
	if (!PdbStreamRead(types->stream, (uint8_t*)&dataSize, 4))
		return false;

	// Sanity check -- the header numbers better agree with the stream size
	if (headerSize + dataSize != PdbStreamGetSize(stream))
		return false;

	// Get the type hash stream number
	if (!PdbStreamRead(types->stream, (uint8_t*)&hashStreamId, 4))
		return false;

	// Sanity check before opening
	if (hashStreamId >= PdbGetStreamCount(pdb))
		types->hash = PdbTypesHashOpen(types, hashStreamId);

	return types;
}


void PdbTypesClose(PDB_TYPES* types)
{
	if (types->hash)
		PdbTypesHashClose(types->hash);
	PdbStreamClose(types->stream);
	free(types);
}

