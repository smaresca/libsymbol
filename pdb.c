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

			// Read the size of the pages in bytes
			if (fread(&pdb->pageSize, 1, 4, pdb->file) != 4)
				return false;

			// Pass "Flag Page" (don't care)
			if ((fread(buff, 1, 4, pdb->file) != 4) || (*(uint32_t*)buff != 2))
				return false;
			
			return true;
		}

		// Now check for the newer format
		if (memcmp(PDB_SIGNATURE_V7, buff, sizeof(PDB_SIGNATURE_V7) - 1) == 0)
		{
			pdb->version = 7;

			// Expecting [unknown byte]DS\0\0
			if (fread(buff, 1, 5, pdb->file) != 5)
				return false;

			// Read the size of the pages in bytes
			if (fread(&pdb->pageSize, 1, 4, pdb->file) != 4)
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
