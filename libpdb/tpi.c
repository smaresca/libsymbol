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
#include <string.h>

#include "pdb.h"
#include "tpi.h"


#define PDB_TYPES_HEADER_SIZE           0x38

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
	PDB_TYPES_HASH_ENTRY values;
	PDB_TYPES_HASH_ENTRY types;
	PDB_TYPES_HASH_ENTRY adjustments;
} PDB_TYPES_HASH;

typedef struct PDB_TYPES
{
	PDB_STREAM* stream;
	uint32_t version;
	uint32_t headerSize;
	uint32_t minId;
	uint32_t maxId;
	uint32_t len; // The amount of data after the header
	PDB_TYPES_HASH* hash;
} PDB_TYPES;

typedef struct PDB_TYPE_PROPERTIES
{
	uint16_t packed : 1;
	uint16_t ctor : 1;
	uint16_t ovlops : 1; // ?
	uint16_t isnested : 1; // ?
	uint16_t cnested : 1; // ?
	uint16_t opassign : 1; // ?
	uint16_t opcast : 1; // ?
	uint16_t fwdref : 1;
	uint16_t scoped : 1;
	uint16_t reserved : 1;
} PDB_TYPE_PROPERTIES;

typedef struct PDB_TYPE_FIELD_ATTRIBUTES
{
	uint16_t access : 2;
	uint16_t mprop : 3;
	uint16_t psuedo : 1;
	uint16_t noinherit : 1;
	uint16_t noconstruct : 1;
	uint16_t compgenx : 1;
	uint16_t reserved : 7;
} PDB_TYPE_FIELD_ATTRIBUTES;

typedef struct PDB_TYPE
{
	PDB_LEAF_TYPES type;
} PDB_TYPE;

typedef struct PDB_LEAF_TYPE_STRUCTURE
{
	uint16_t lf;
	uint16_t count;
	uint16_t prop;
	uint32_t field;
	uint32_t derived;
	uint32_t vshape;
	char* name;
} PDB_LEAF_TYPE_STRUCTURE;


// My interpretation of the algorithm in Ch 7.5 Hash table and sort table descriptions
// in "Microsoft Symbol and Type Information" at
// http://pierrelib.pagesperso-orange.fr/exec_formats/MS_Symbol_Type_v1.0.pdf
static uint32_t CalcTypeHash(const char* typeName)
{
	size_t len = strlen(typeName) + 1;
	const uint32_t* pName = (uint32_t*)typeName;
	uint32_t end;
	size_t i;
	uint32_t sum;

	end = 0;
	while (len & 3)
	{
		end |= (typeName[len - 1] & 0xdf); // toupper
		end <<= 8;
		len--;
	}

	len /= 4;
	for (i = 0; i < len; i++)
	{
		sum ^= (pName[i] & 0xdfdfdfdf); // toupper
		sum = (sum << 4) | (sum >> 28); //rotl sum, 4
	}
	sum ^= end;

	return sum; // take modulus of hash buckets
}


static PDB_TYPES_HASH* PdbTypesHashOpen(PDB_TYPES* types, uint32_t hashStreamId)
{
	PDB_TYPES_HASH* hash = (PDB_TYPES_HASH*)malloc(sizeof(PDB_TYPES_HASH));
	uint16_t reserved;
	hash->stream = PdbStreamOpen(PdbStreamGetPdb(types->stream), hashStreamId);

	if (!hash->stream)
	{
		free(hash);
		return NULL;
	}

	// Move past the reserved word (filler to preserved alignment)
	if (!PdbStreamRead(types->stream, (uint8_t*)&reserved, 2))
		return false;

	// Get the size of the key
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->keySize, 4))
		return false;

	// Get the number of buckets in the hash
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->buckets, 4))
		return false;

	// Read the hash values
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->values.offset, 4))
		return false;
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->values.size, 4))
		return false;

	// Read the hash indices
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->types.offset, 4))
		return false;
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->types.size, 4))
		return false;

	// Read the hash adjustments
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->adjustments.offset, 4))
		return false;
	if (!PdbStreamRead(types->stream, (uint8_t*)&hash->adjustments.size, 4))
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
	uint16_t hashStreamId;
	uint32_t version;

	// Get the types stream
	PDB_STREAM* stream = PdbStreamOpen(pdb, PDB_STREAM_TYPE_INFO);

	// Read version
	if (!PdbStreamRead(stream, (uint8_t*)&version, 4))
		return NULL;

	// Check for a supported version
	if ((version != PDB_VERSION_VC2)
		&& (version != PDB_VERSION_VC2)
		&& (version != PDB_VERSION_VC4)
		&& (version != PDB_VERSION_VC41)
		&& (version != PDB_VERSION_VC50)
		&& (version != PDB_VERSION_VC60)
		&& (version != PDB_VERSION_VC70)
		&& (version != PDB_VERSION_VC71)
		&& (version != PDB_VERSION_VC8))
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
	if (!PdbStreamRead(types->stream, (uint8_t*)&types->headerSize, 4))
		goto FAIL;

	// Get the minimum type index
	if (!PdbStreamRead(types->stream, (uint8_t*)&types->minId, 4))
		goto FAIL;

	// Get the maximum type index
	if (!PdbStreamRead(types->stream, (uint8_t*)&types->maxId, 4))
		goto FAIL;

	// Get the size of the data following the header
	if (!PdbStreamRead(types->stream, (uint8_t*)&types->len, 4))
		goto FAIL;

	// Sanity check -- the header numbers better agree
	// with the actual stream size
	if (types->headerSize + types->len != PdbStreamGetSize(stream))
		goto FAIL;

	// Get the type hash stream number
	if (!PdbStreamRead(types->stream, (uint8_t*)&hashStreamId, 2))
		goto FAIL;

	// Sanity check before opening
	if (hashStreamId <= PdbGetStreamCount(pdb))
		types->hash = PdbTypesHashOpen(types, hashStreamId);

	return types;

FAIL:
	PdbStreamClose(stream);
	free(types);

	return NULL;
}


static bool PrintStructureType(PDB_TYPES* types, PdbTypeEnumFunction typeFn, uint8_t* buff, size_t len)
{
	PDB_LEAF_TYPE_STRUCTURE structType;
	size_t nameLen;
	uint8_t* pbuff = buff;

	structType.count = *(uint16_t*)pbuff;
	pbuff += sizeof(uint16_t);

	structType.prop = *(uint16_t*)pbuff;
	pbuff += sizeof(uint16_t);

	structType.field = *(uint32_t*)pbuff;
	pbuff += sizeof(uint32_t);

	structType.derived = *(uint32_t*)pbuff;
	pbuff += sizeof(uint32_t);

	structType.vshape = *(uint32_t*)pbuff;
	pbuff += sizeof(uint32_t);

	// MS says there are a variable number of bytes here representing
	// the length of the structure.
	// So far, I've seen two zero bytes.
	pbuff += sizeof(uint16_t);

	nameLen = (len - (pbuff - buff)); // The remainder of the buffer is the name field

	if (nameLen)
	{
		structType.name = (uint8_t*)malloc(nameLen + 1);
		strncpy(structType.name, pbuff, nameLen);
	}
	else
	{
		structType.name = NULL;
	}

	pbuff += strlen(structType.name);

	printf("struct name=%s count=%x prop=%x, field=%x, derived=%x, vshape=%x\n",
		structType.name, (uint32_t)structType.count,
		(uint32_t)structType.prop, structType.field, structType.derived,
		structType.vshape);

	return true;
}


static bool PrintFieldList(PDB_TYPES* types, PdbTypeEnumFunction typeFn, uint8_t* buff, size_t len)
{
	uint8_t* pbuff = buff;
	size_t remainingLen = len;

	while (remainingLen)
	{
		uint16_t typeId;
		uint16_t lf;
		uint8_t skip;
		uint32_t val;
		char* name;
		size_t nameLen;

		lf = *(uint16_t*)pbuff;
		pbuff += sizeof(uint16_t);
		remainingLen -= sizeof(uint16_t);

		typeId = *(uint16_t*)pbuff;
		pbuff += sizeof(uint16_t);
		remainingLen -= sizeof(uint16_t);

		switch (lf)
		{
		case LEAF_TYPE_MEMBER:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_ENUMERATE:
			val = (uint32_t)(*(uint16_t*)pbuff);
			pbuff += sizeof(uint16_t);
			remainingLen -= sizeof(uint16_t);

			// If the high bit is set, this isn't simply the enum value
			if (val & 0x8000)
			{
				// These are all the values I encountered...
				switch (val & 0x7ff)
				{
				case 0:
					// A single byte follows that is repeated through the dword
					val = ((*(uint8_t*)pbuff) | ((*(uint8_t*)pbuff) << 8) | ((*(uint8_t*)pbuff) << 16) | ((*(uint8_t*)pbuff) << 24));
					pbuff += sizeof(uint8_t);
					remainingLen -= sizeof(uint8_t);
					break;
				case 1:
					break;
				case 2:
					// The value is a word, promote to dword
					val = (uint32_t)(*(uint16_t*)pbuff);
					pbuff += sizeof(uint16_t);
					remainingLen -= sizeof(uint16_t);
					break;
				case 3:
					// The value that follows is a dword
					val = *(uint32_t*)pbuff;
					pbuff += sizeof(uint32_t);
					remainingLen -= sizeof(uint32_t);
					break;
				case 4:
					// The value that follows is a dword
					val = *(uint32_t*)pbuff;
					pbuff += sizeof(uint32_t);
					remainingLen -= sizeof(uint32_t);
					break;
				default:
					break;
				}
			}

			name = (char*)pbuff;
			nameLen = strlen(name) + 1;
			nameLen = (nameLen > remainingLen ? remainingLen : nameLen);
			remainingLen -= nameLen;
			pbuff += nameLen;
			printf("%d:%s = %d\n", typeId, name, val);
			break;
		case LEAF_TYPE_UNION:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_BITFIELD:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_BCLASS:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_VFUNCTAB:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_ONEMETHOD:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_METHOD:
			remainingLen -= remainingLen;
			break;
		case LEAF_TYPE_NESTTYPE:
			remainingLen -= remainingLen;
			break;
		}

		// Bypass padding bytes
		while ((remainingLen > 0) && (*pbuff > 0xf0))
		{
			skip = (*pbuff & 0xf);
			pbuff += skip;
			remainingLen -= skip;
		}
	}

	return true;
}


bool PdbTypesPrint(PDB_TYPES* types, const char* name, PdbTypeEnumFunction typeFn)
{
	uint32_t typeHash;
	uint32_t bucket;

	typeHash = CalcTypeHash(name);
	bucket = typeHash % types->hash->buckets;

	return false;
}


bool PdbTypesEnumerate(PDB_TYPES* types, PdbTypeEnumFunction typeFn)
{
	uint16_t type;
	uint32_t len = types->len;
	uint32_t typeCount;
	uint32_t i;

	// Seek to the beginning of all types
	if (!PdbStreamSeek(types->stream, types->headerSize))
		return false;

	// Calculate the number of types we expect in the stream
	typeCount = types->maxId - types->minId;

	for (i = 0; i < typeCount; i++)
	{
		uint16_t typeLen;
		uint8_t* buff = NULL;

		// Check if we ran out of data before we ran out of types
		if (len == 0)
		{
			printf("Error:  Ran out of data before ran out of types.\n");
			return false;
		}

		if (!PdbStreamRead(types->stream, (uint8_t*)&typeLen, 2))
			return false;

		// Get the type type (LEAF_TYPE_?)
		if (!PdbStreamRead(types->stream, (uint8_t*)&type, 2))
			return false;

		buff = (uint8_t*)malloc(typeLen);

		// Read the data associated with the type type
		if (!PdbStreamRead(types->stream, buff, typeLen - 2))
		{
			free(buff);
			return false;
		}

		switch (type)
		{
		case LEAF_TYPE_STRUCTURE:
			PrintStructureType(types, typeFn, buff, typeLen - 2);
			break;
		case LEAF_TYPE_POINTER:
			printf("POINTER TYPE\n");
			break;
		case LEAF_TYPE_FIELDLIST:
			printf("FIELDLIST TYPE\n");
			PrintFieldList(types, typeFn, buff, typeLen - 2);
			break;
		case LEAF_TYPE_UNION:
			printf("UNION TYPE\n");
			break;
		case LEAF_TYPE_BITFIELD:
			printf("BITFIELD TYPE\n");
			break;
		case LEAF_TYPE_ENUM:
			{
				char* name = buff + 0xc;
				char* tag = ((0x0e + strlen(name) + 1) < ((size_t)(typeLen - 2))) ? (name + strlen(name) + 1) : NULL; //(name + strlen(name) + 1) : NULL);
				uint16_t count = *(uint16_t*)(buff + 2); 
				uint32_t idx = *(uint32_t*)(buff + 8);
				printf("ENUM name=%s tag=%s %d members fieldlist idx=%.04x\n", name, tag, count, idx);
			}
			break;
		case LEAF_TYPE_ARRAY:
			printf("ARRAY TYPE\n");
			break;
		case LEAF_TYPE_PROCEDURE:
			printf("PROCEDURE TYPE\n");
			break;
		case LEAF_TYPE_ARGLIST:
			printf("ARGLIST TYPE\n");
			break;
		case LEAF_TYPE_MODIFIER:
			printf("MODIFIER TYPE\n");
			break;
		case LEAF_TYPE_CLASS:
			printf("CLASS TYPE\n");
			break;
		case LEAF_TYPE_MFUNCTION:
			printf("MFUNCTION TYPE\n");
			break;
		case LEAF_TYPE_METHODLIST:
			printf("METHODLIST TYPE\n");
			break;
		case LEAF_TYPE_VTSHAPE:
			printf("VTSHAPE TYPE\n");
			break;
		default:
			printf("UNKNOWN TYPE\n");
			break;
		};

		// TODO:  Something with the type

		free(buff);

		// Pass this type (and size, which is not accounted for by the value read from the file)
		len -= (typeLen + 2);
	}

	return true;
}


uint32_t PdbTypesGetCount(PDB_TYPES* types)
{
	return 0;
}


void PdbTypesClose(PDB_TYPES* types)
{
	if (types->hash)
		PdbTypesHashClose(types->hash);
	PdbStreamClose(types->stream);
	free(types);
}

