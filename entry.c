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


static void PrintHelp()
{
	fprintf(stderr, "Usage: pdbp [options] [pdb file]\n");
	fprintf(stderr, "Options:\n\n");
	fprintf(stderr, "\t-l or --list-streams\t\tList the streams in the PDB file.\n");
	fprintf(stderr, "\t-d [stream_num] or --dump-stream [stream_num]\t\tDump the data in the stream to stdout.\n");
}


int main(int argc, char** argv)
{
	PDB_FILE* pdb;

	if (argc < 2)
	{
		PrintHelp();
		return 1;
	}

	pdb = PdbOpen(argv[argc - 1]);

	if (!pdb)
	{
		fprintf(stderr, "Failed to open pdb file %s\n", argv[argc - 1]);
		return 2;
	}

	fprintf(stderr, "Successfully opened pdb.\n");
	fprintf(stderr, "This file contains %d streams.\n", PdbGetStreamCount(pdb));

	PdbClose(pdb);

	return 0;
}
