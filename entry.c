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

char* g_pdbFile = NULL; // The full path and file name of the pdb file we are operating on
bool g_dumpStream = false; // Do we want to dump a stream?
uint16_t g_dumpStreamId = (uint16_t)-1; // The stream id to dump if dump is true.
bool g_dumpType = false; // Do we want to dump a type?
bool g_dumpAllTypes = false;
char* g_type = NULL;


#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif /* _MSC_VER */


static void PrintHelp()
{
	fprintf(stderr, "Usage: pdbp [options] [pdb file]\n");
	fprintf(stderr, "Options:\n\n");
	fprintf(stderr, "\t-d [stream_num] or --dump-stream [stream_num]\t\tDump the data in the stream to stdout.\n");
	fprintf(stderr, "\t dt [type name} or --dump-type [type name]\t\tDump type information to stdout.\n");
}


static bool ParseCommandLine(int argc, char** argv)
{
	if (argc < 2)
		return false;

	if (argc == 2)
	{
		g_pdbFile = argv[1];
		return true;
	}

	if (argc == 4)
	{
		if ((strcasecmp(argv[1], "-d") == 0)
			|| (strcasecmp(argv[1], "--dump-stream") == 0))
		{
			g_dumpStream = true;
			g_dumpStreamId = (uint16_t)atoi(argv[2]);
		}
		else if ((strcasecmp(argv[1], "-dt") == 0)
			|| (strcasecmp(argv[1], "--dump-type") == 0))
		{
			g_dumpType = true;

			if (strcasecmp(argv[2], "all") == 0)
				g_dumpAllTypes = true;
			else
				g_type = argv[2];
		}
		g_pdbFile = argv[3];

		return true;
	}

	return false;
}


int main(int argc, char** argv)
{
	PDB_FILE* pdb;

	if (!ParseCommandLine(argc, argv))
	{
		PrintHelp();
		return 1;
	}

	pdb = PdbOpen(g_pdbFile);

	if (!pdb)
	{
		fprintf(stderr, "Failed to open pdb file %s\n", argv[argc - 1]);
		return 2;
	}

	fprintf(stderr, "Successfully opened pdb.\n");
	fprintf(stderr, "This file contains %d streams.\n", PdbGetStreamCount(pdb));

	if (g_dumpStream)
	{
		uint8_t buff[512];
		uint32_t chunkSize;
		uint32_t bytesRemaining;
		PDB_STREAM* stream = PdbStreamOpen(pdb, g_dumpStreamId);

		if (!stream)
		{
			PdbClose(pdb);
			fprintf(stderr, "Failed to open stream %d.\n", g_dumpStreamId);
			return 3;
		}

		bytesRemaining = PdbStreamGetSize(stream);

		while (bytesRemaining)
		{
			if (bytesRemaining > 512)
				chunkSize = 512;
			else
				chunkSize = bytesRemaining;

			if (!PdbStreamRead(stream, buff, chunkSize))
			{
				PdbStreamClose(stream);
				PdbClose(pdb);
				fprintf(stderr, "Failed to read stream.\n");
				return 4;
			}

			if (fwrite(buff, 1, chunkSize, stdout) != chunkSize)
			{
				PdbStreamClose(stream);
				PdbClose(pdb);
				fprintf(stderr, "Failed to write to stdout.\n");
				return 5;
			}

			bytesRemaining -= chunkSize;
		}
	}

	if (g_dumpType)
	{
		// Attempt to initialize the types subsystem
		PDB_TYPES* types = PdbTypesOpen(pdb);

		if (!types)
		{
			fprintf(stderr, "Failed to open pdb types.\n");
			PdbClose(pdb);
			return 6;
		}

		if (g_dumpAllTypes)
			PdbTypesEnumerate(types, NULL);
		else
			PdbTypesPrint(types, g_type, NULL);

		PdbTypesClose(types);
	}

	PdbClose(pdb);

	return 0;
}
