/*
	Simple mergetool for Burstcoin plot-files.
        Author: Markus Tervooren <info@bchain.info>
        Burst: BURST-R5LP-KEL9-UYLG-GFG6T

        Implementation of Shabal is taken from:
        http://www.shabal.com/?p=198

        Usage: ./merge <input file>
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
#include "helper.h"
#endif

// These are fixed. Do not change!
#define SCOOP_SIZE      64
#define PLOTSIZE        262144

int optimizeFile(char *filename, unsigned long long memory) {
	// find last '/' in path
	char *lastslash = strrchr(filename, '/');
	char *dir = NULL;

	// No path: use filename
	if(lastslash == NULL)
		lastslash = filename;
	else {
		dir = (char*)malloc(lastslash - filename + 1);
		memcpy(dir, filename, lastslash - filename);
		dir[lastslash - filename] = 0;
		lastslash++;
	}

	unsigned long long key;
	unsigned long long startnonce;
	unsigned long long nonces;
	unsigned long long stagger;

	// Check if filename has right format
#ifdef _WIN32
	if(!sscanf(lastslash, "%I64u_%I64u_%I64u_%I64u", &key, &startnonce, &nonces, &stagger)) {
#else
	if(!sscanf(lastslash, "%llu_%llu_%llu_%llu", &key, &startnonce, &nonces, &stagger)) {
#endif
		printf("%s: Wrong file name\n", lastslash);
		if(dir != NULL)
			free(dir);
		return -1;
	}

#ifdef _WIN32
	int fh = open(filename, _O_RDONLY | _O_BINARY);
#else
	int fh = open(filename, O_RDONLY);
#endif

	if(fh < 0) {
		printf("File %s not found\n", filename);
		if(dir != NULL)
			free(dir);
		return -1;
	}

	// Check if file is complete
	struct stat sh;
	fstat(fh, &sh);

	unsigned long long int expect = (unsigned long long int)nonces * PLOTSIZE;

	if(((long long) sh.st_size) != expect) {
#ifdef _WIN32
		printf("File not complete. Expected size: %I64u, actual size: %I64u\n", expect, (long long) sh.st_size);
#else
		printf("File not complete. Expected size: %llu, actual size: %llu\n", expect, (long long) sh.st_size);
#endif
		if(dir != NULL)
			free(dir);
		return -1;
	}

	if(nonces % stagger != 0) {
		printf("Nonces not a multiple of stagger. Quitting.\n");
		if(dir != NULL)
			free(dir);
		return -1;
	}

	int blocks = nonces / stagger;

	if(blocks == 1) {
		printf("File is already organized\n");
		if(dir != NULL)
			free(dir);
		return -1;
	}

	// Enough free space? Linux only
#ifndef _WIN32
	unsigned long long fs;
	if(dir == NULL) {
		fs = freespace(".");
	} else {
		fs = freespace(dir);
	}
	
	if(fs < expect) {
		printf("Not enough free space on device. Free %llu MB to continue\n", (expect - fs) / 1000000);
		return -1;
	}	
#endif
	
	// Open output file
	char outputfile[100];   // Should be plenty

#ifdef _WIN32
	if(dir == NULL) {
		sprintf(outputfile, "%I64u_%I64u_%I64u_%I64u", key, startnonce, nonces, nonces);
	} else {
		sprintf(outputfile, "%s/%I64u_%I64u_%I64u_%I64u", dir, key, startnonce, nonces, nonces);
	}
#else
	if(dir == NULL) {
		sprintf(outputfile, "%llu_%llu_%llu_%llu", key, startnonce, nonces, nonces);
	} else {
		sprintf(outputfile, "%s/%llu_%llu_%llu_%llu", dir, key, startnonce, nonces, nonces);
	}
#endif

#ifdef _WIN32
	int oh = open(outputfile, O_CREAT | _O_WRONLY | _O_BINARY, 0644);
#else
	int oh = open(outputfile, O_CREAT | O_WRONLY, 0644);
#endif

	if(oh < 0) {
		printf("Could not open output file %s\n", outputfile);
		return -1;
	}

	// Processing plots:
	int i,j,k;

	char *buffers[blocks];
	
	int ssize = 4096;
	unsigned long long memused;

	for(; ssize > 1; ssize /= 2) {
		memused = blocks * stagger * SCOOP_SIZE * ssize;
		if(memused < memory)
			break;
	}

	for(i=0; i<blocks; i++) {
		buffers[i] = (char*) malloc( stagger * SCOOP_SIZE * ssize );
		if(buffers[i] == NULL) {
			printf("Error allocating memory\n");
			exit(0);
		}
	}

	printf("Reorganizing file %s to file %s:\n", filename, outputfile);
	printf("Processing %i scoops at once (uses %u MB memory)\n", ssize, (unsigned int)(memused / 1000000));

	unsigned long long bytes;
	for(i=0; i<(PLOTSIZE / SCOOP_SIZE); i += ssize) {
		printf("\rprocessing Scoop %i of %i           ", i+1, (PLOTSIZE / SCOOP_SIZE));
		fflush(stdout);

		for(j=0; j<blocks; j++) {
			bytes = 0;
			do {
				lseek64(fh, ((unsigned long long int)j * stagger * PLOTSIZE) + ((unsigned long long int)i * stagger * SCOOP_SIZE) + bytes, SEEK_SET);
				unsigned long long found = (unsigned long long)read( fh, &(buffers[j])[bytes], (stagger * SCOOP_SIZE * ssize) - bytes);
				bytes += found;
			} while (bytes < stagger * SCOOP_SIZE * ssize);
		}

		for(k=0; k < ssize; k++) {
			for(j=0; j<blocks; j++) {
				bytes = 0;
				do {
					bytes += write(
						oh,
						&(buffers[j])[bytes + (stagger * SCOOP_SIZE * k)],
						(stagger * SCOOP_SIZE) - bytes);
				} while (bytes < stagger * SCOOP_SIZE);
			}
		}
	}

	printf("\nDone.\n");
	close(fh);
	close(oh);

	for(i=0; i<blocks; i++) {
		free(buffers[i]);
	}
	if(dir != NULL)
		free(dir);

	return 0;
}

int main(int argc, char **argv) {
        if(argc < 2) {
                printf("Usage: ./optimize [-m MEMORY] <Plot file> [<Plot file> ..]\n");
                exit(-1);
        }

	int file;
#ifdef _WIN32
	unsigned long long memory = 2000000000;
#else
	unsigned long long memory = freemem() * 0.8;
#endif

	char **files = (char**)malloc( sizeof(char*) * argc);
	int nfiles = 0;

	int finished = 0;
	for(file = 1; file < argc; file++) {
		if(finished == 0 && argv[file][0] == '-') {
			if(argv[file][1] == '-') {
				// finish with --
				finished = 1;
			} else if(argv[file][1] == 'm') {
				char *parse = NULL;
				if(argv[file][2] == 0) {
					if(file < argc - 1) 
						parse = argv[++file];
				} else {
					parse = &(argv[file][2]);
				}
				if(parse != NULL) {
					memory = strtoull(parse, 0, 10);
					switch(parse[strlen(parse) - 1]) {
						case 't':
						case 'T':
							memory *= 1000;
						case 'g':
						case 'G':
							memory *= 1000;
						case 'm':
						case 'M':
							memory *= 1000;
						case 'k':
						case 'K':
							memory *= 1000;
					}
				}
			}
		} else {
			files[nfiles++] = argv[file];
		}
	}

	for(file = 0; file < nfiles; file++) {
		if(optimizeFile(files[file], memory) == 0) {
			printf("Replacing plot file\n");
			unlink(files[file]);		
		}
	}	

	return 0;
}

