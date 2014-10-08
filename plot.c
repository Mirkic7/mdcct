/*
	Even faster plot generator for burscoin
	Modified version originally written by Markus Tervooren
	Added code for mshabal/mshabal256 (SSE4/AVX2 optimizations)
	by Cerr Janror <cerr.janror@gmail.com> : https://github.com/BurstTools/BurstSoftware.git

	Author: Mirkic7 <mirkic7@hotmail.com>
	Burst: BURST-RQW7-3HNW-627D-3GAEV

	Original author: Markus Tervooren <info@bchain.info>
	Burst: BURST-R5LP-KEL9-UYLG-GFG6T

	Implementation of Shabal is taken from:
	http://www.shabal.com/?p=198

	Usage: ./plot <public key> <start nonce> <nonces> <stagger size> <threads>
*/

#define USE_MULTI_SHABAL

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include "shabal.h"
#ifdef AVX2
#include "mshabal256.h"
#endif
#include "mshabal.h"
#include "helper.h"

// Leave 5GB free space
#define FREE_SPACE	(unsigned long long)5 * 1000 * 1000 * 1000
#define DEFAULTDIR	"plots/"

// Not to be changed below this
#define PLOT_SIZE	(4096 * 64)
#define HASH_SIZE	32
#define HASH_CAP	4096

unsigned long long addr = 0;
unsigned long long startnonce = 0;
unsigned int nonces = 0;
unsigned int staggersize = 0;
unsigned int threads = 0;
unsigned int noncesperthread;
unsigned int selecttype = 0;
unsigned int asyncmode = 0;
unsigned long long starttime;
int ofd, run, lastrun;

char *cache, *wcache, *acache[2];
char *outputdir = DEFAULTDIR;

#define SET_NONCE(gendata, nonce) \
  xv = (char*)&nonce; \
  gendata[PLOT_SIZE + 8] = xv[7]; gendata[PLOT_SIZE + 9] = xv[6]; gendata[PLOT_SIZE + 10] = xv[5]; gendata[PLOT_SIZE + 11] = xv[4]; \
  gendata[PLOT_SIZE + 12] = xv[3]; gendata[PLOT_SIZE + 13] = xv[2]; gendata[PLOT_SIZE + 14] = xv[1]; gendata[PLOT_SIZE + 15] = xv[0]

#ifdef AVX2
int m256nonce(unsigned long long int addr,
    unsigned long long int nonce1, unsigned long long int nonce2, unsigned long long int nonce3, unsigned long long int nonce4,
    unsigned long long int nonce5, unsigned long long int nonce6, unsigned long long int nonce7, unsigned long long int nonce8,
    unsigned long long cachepos1, unsigned long long cachepos2, unsigned long long cachepos3, unsigned long long cachepos4,
    unsigned long long cachepos5, unsigned long long cachepos6, unsigned long long cachepos7, unsigned long long cachepos8)
{
    char final1[32], final2[32], final3[32], final4[32];
    char final5[32], final6[32], final7[32], final8[32];
    char gendata1[16 + PLOT_SIZE], gendata2[16 + PLOT_SIZE], gendata3[16 + PLOT_SIZE], gendata4[16 + PLOT_SIZE];
    char gendata5[16 + PLOT_SIZE], gendata6[16 + PLOT_SIZE], gendata7[16 + PLOT_SIZE], gendata8[16 + PLOT_SIZE];

    char *xv = (char*)&addr;

    gendata1[PLOT_SIZE] = xv[7]; gendata1[PLOT_SIZE + 1] = xv[6]; gendata1[PLOT_SIZE + 2] = xv[5]; gendata1[PLOT_SIZE + 3] = xv[4];
    gendata1[PLOT_SIZE + 4] = xv[3]; gendata1[PLOT_SIZE + 5] = xv[2]; gendata1[PLOT_SIZE + 6] = xv[1]; gendata1[PLOT_SIZE + 7] = xv[0];

    for (int i = PLOT_SIZE; i <= PLOT_SIZE + 7; ++i)
    {
      gendata2[i] = gendata1[i];
      gendata3[i] = gendata1[i];
      gendata4[i] = gendata1[i];
      gendata5[i] = gendata1[i];
      gendata6[i] = gendata1[i];
      gendata7[i] = gendata1[i];
      gendata8[i] = gendata1[i];
    }

    xv = (char*)&nonce1;

    SET_NONCE(gendata1, nonce1);
    SET_NONCE(gendata2, nonce2);
    SET_NONCE(gendata3, nonce3);
    SET_NONCE(gendata4, nonce4);
    SET_NONCE(gendata5, nonce5);
    SET_NONCE(gendata6, nonce6);
    SET_NONCE(gendata7, nonce7);
    SET_NONCE(gendata8, nonce8);

    mshabal256_context x;
    int i, len;

    for (i = PLOT_SIZE; i > 0; i -= HASH_SIZE)
    {

      mshabal256_init(&x, 256);

      len = PLOT_SIZE + 16 - i;
      if (len > HASH_CAP)
        len = HASH_CAP;

      mshabal256(&x, &gendata1[i], &gendata2[i], &gendata3[i], &gendata4[i], &gendata5[i], &gendata6[i], &gendata7[i], &gendata8[i], len);
      mshabal256_close(&x, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        &gendata1[i - HASH_SIZE], &gendata2[i - HASH_SIZE], &gendata3[i - HASH_SIZE], &gendata4[i - HASH_SIZE],
        &gendata5[i - HASH_SIZE], &gendata6[i - HASH_SIZE], &gendata7[i - HASH_SIZE], &gendata8[i - HASH_SIZE]);

    }

    mshabal256_init(&x, 256);
    mshabal256(&x, gendata1, gendata2, gendata3, gendata4, gendata5, gendata6, gendata7, gendata8, 16 + PLOT_SIZE);
    mshabal256_close(&x, 0, 0, 0, 0, 0, 0, 0, 0, 0, final1, final2, final3, final4, final5, final6, final7, final8);

    // XOR with final
    for (i = 0; i < PLOT_SIZE; i++)
    {
      gendata1[i] ^= (final1[i % 32]);
      gendata2[i] ^= (final2[i % 32]);
      gendata3[i] ^= (final3[i % 32]);
      gendata4[i] ^= (final4[i % 32]);
      gendata5[i] ^= (final5[i % 32]);
      gendata6[i] ^= (final6[i % 32]);
      gendata7[i] ^= (final7[i % 32]);
      gendata8[i] ^= (final8[i % 32]);
    }

    // Sort them:
    for (i = 0; i < PLOT_SIZE; i += 64)
    {
      memmove(&cache[cachepos1 * 64 + (unsigned long long)i * staggersize], &gendata1[i], 64);
      memmove(&cache[cachepos2 * 64 + (unsigned long long)i * staggersize], &gendata2[i], 64);
      memmove(&cache[cachepos3 * 64 + (unsigned long long)i * staggersize], &gendata3[i], 64);
      memmove(&cache[cachepos4 * 64 + (unsigned long long)i * staggersize], &gendata4[i], 64);
      memmove(&cache[cachepos5 * 64 + (unsigned long long)i * staggersize], &gendata5[i], 64);
      memmove(&cache[cachepos6 * 64 + (unsigned long long)i * staggersize], &gendata6[i], 64);
      memmove(&cache[cachepos7 * 64 + (unsigned long long)i * staggersize], &gendata7[i], 64);
      memmove(&cache[cachepos8 * 64 + (unsigned long long)i * staggersize], &gendata8[i], 64);
    }

    return 0;
}
#endif

int mnonce(unsigned long long int addr,
    unsigned long long int nonce1, unsigned long long int nonce2, unsigned long long int nonce3, unsigned long long int nonce4,
    unsigned long long cachepos1, unsigned long long cachepos2, unsigned long long cachepos3, unsigned long long cachepos4)
{
    char final1[32], final2[32], final3[32], final4[32];
    char gendata1[16 + PLOT_SIZE], gendata2[16 + PLOT_SIZE], gendata3[16 + PLOT_SIZE], gendata4[16 + PLOT_SIZE];

    char *xv = (char*)&addr;

    gendata1[PLOT_SIZE] = xv[7]; gendata1[PLOT_SIZE + 1] = xv[6]; gendata1[PLOT_SIZE + 2] = xv[5]; gendata1[PLOT_SIZE + 3] = xv[4];
    gendata1[PLOT_SIZE + 4] = xv[3]; gendata1[PLOT_SIZE + 5] = xv[2]; gendata1[PLOT_SIZE + 6] = xv[1]; gendata1[PLOT_SIZE + 7] = xv[0];

    for (int i = PLOT_SIZE; i <= PLOT_SIZE + 7; ++i)
    {
      gendata2[i] = gendata1[i];
      gendata3[i] = gendata1[i];
      gendata4[i] = gendata1[i];
    }

    SET_NONCE(gendata1, nonce1);
    SET_NONCE(gendata2, nonce2);
    SET_NONCE(gendata3, nonce3);
    SET_NONCE(gendata4, nonce4);

    mshabal_context x;
    int i, len;

    for (i = PLOT_SIZE; i > 0; i -= HASH_SIZE)
    {

      sse4_mshabal_init(&x, 256);

      len = PLOT_SIZE + 16 - i;
      if (len > HASH_CAP)
        len = HASH_CAP;

      sse4_mshabal(&x, &gendata1[i], &gendata2[i], &gendata3[i], &gendata4[i], len);
      sse4_mshabal_close(&x, 0, 0, 0, 0, 0, &gendata1[i - HASH_SIZE], &gendata2[i - HASH_SIZE], &gendata3[i - HASH_SIZE], &gendata4[i - HASH_SIZE]);

    }

    sse4_mshabal_init(&x, 256);
    sse4_mshabal(&x, gendata1, gendata2, gendata3, gendata4, 16 + PLOT_SIZE);
    sse4_mshabal_close(&x, 0, 0, 0, 0, 0, final1, final2, final3, final4);

    // XOR with final
    for (i = 0; i < PLOT_SIZE; i++)
    {
      gendata1[i] ^= (final1[i % 32]);
      gendata2[i] ^= (final2[i % 32]);
      gendata3[i] ^= (final3[i % 32]);
      gendata4[i] ^= (final4[i % 32]);
    }

    // Sort them:
    for (i = 0; i < PLOT_SIZE; i += 64)
    {
      memmove(&cache[cachepos1 * 64 + (unsigned long long)i * staggersize], &gendata1[i], 64);
      memmove(&cache[cachepos2 * 64 + (unsigned long long)i * staggersize], &gendata2[i], 64);
      memmove(&cache[cachepos3 * 64 + (unsigned long long)i * staggersize], &gendata3[i], 64);
      memmove(&cache[cachepos4 * 64 + (unsigned long long)i * staggersize], &gendata4[i], 64);
    }

    return 0;
}


void nonce(unsigned long long int addr, unsigned long long int nonce, unsigned long long cachepos) {
	char final[32];
	char gendata[16 + PLOT_SIZE];

	char *xv = (char*)&addr;
	
	gendata[PLOT_SIZE] = xv[7]; gendata[PLOT_SIZE+1] = xv[6]; gendata[PLOT_SIZE+2] = xv[5]; gendata[PLOT_SIZE+3] = xv[4];
	gendata[PLOT_SIZE+4] = xv[3]; gendata[PLOT_SIZE+5] = xv[2]; gendata[PLOT_SIZE+6] = xv[1]; gendata[PLOT_SIZE+7] = xv[0];

	xv = (char*)&nonce;

	gendata[PLOT_SIZE+8] = xv[7]; gendata[PLOT_SIZE+9] = xv[6]; gendata[PLOT_SIZE+10] = xv[5]; gendata[PLOT_SIZE+11] = xv[4];
	gendata[PLOT_SIZE+12] = xv[3]; gendata[PLOT_SIZE+13] = xv[2]; gendata[PLOT_SIZE+14] = xv[1]; gendata[PLOT_SIZE+15] = xv[0];

	shabal_context x;
	int i, len;

	for(i = PLOT_SIZE; i > 0; i -= HASH_SIZE) {
		shabal_init(&x, 256);
		len = PLOT_SIZE + 16 - i;
		if(len > HASH_CAP)
			len = HASH_CAP;
		shabal(&x, &gendata[i], len);
		shabal_close(&x, 0, 0, &gendata[i - HASH_SIZE]);
	}
	
	shabal_init(&x, 256);
	shabal(&x, gendata, 16 + PLOT_SIZE);
	shabal_close(&x, 0, 0, final);


	// XOR with final
	unsigned long long *start = (unsigned long long*)gendata;
	unsigned long long *fint = (unsigned long long*)&final;

	for(i = 0; i < PLOT_SIZE; i += 32) {
		*start ^= fint[0]; start ++;
		*start ^= fint[1]; start ++;
		*start ^= fint[2]; start ++;
		*start ^= fint[3]; start ++;
	}

	// Sort them:
	for(i = 0; i < PLOT_SIZE; i+=64)
		memmove(&cache[cachepos * 64 + (unsigned long long)i * staggersize], &gendata[i], 64);
}

void *work_i(void *x_void_ptr) {
	unsigned long long *x_ptr = (unsigned long long *)x_void_ptr;
	unsigned long long i = *x_ptr;

	unsigned int n;
        for(n=0; n<noncesperthread; n++) {
            if(selecttype == 1) {
                if (n + 4 < noncesperthread)
                {
                    mnonce(addr,
                          (i + n + 0), (i + n + 1), (i + n + 2), (i + n + 3),
                          (unsigned long long)(i - startnonce + n + 0),
                          (unsigned long long)(i - startnonce + n + 1),
                          (unsigned long long)(i - startnonce + n + 2),
                          (unsigned long long)(i - startnonce + n + 3));

                    n += 3;
                } else
                   nonce(addr,(i + n), (unsigned long long)(i - startnonce + n));
#ifdef AVX2
            } else if(selecttype == 2) {
                if (n + 8 < noncesperthread)
                {
                    m256nonce(addr,
                      (i + n + 0), (i + n + 1), (i + n + 2), (i + n + 3),
                      (i + n + 4), (i + n + 5), (i + n + 6), (i + n + 7),
                      (unsigned long long)(i - startnonce + n + 0),
                      (unsigned long long)(i - startnonce + n + 1),
                      (unsigned long long)(i - startnonce + n + 2),
                      (unsigned long long)(i - startnonce + n + 3),
                      (unsigned long long)(i - startnonce + n + 4),
                      (unsigned long long)(i - startnonce + n + 5),
                      (unsigned long long)(i - startnonce + n + 6),
                      (unsigned long long)(i - startnonce + n + 7));

                    n += 7;
                } else
                    nonce(addr,(i + n), (unsigned long long)(i - startnonce + n));
#endif
            } else {
                nonce(addr,(i + n), (unsigned long long)(i - startnonce + n));
            }
        }

	return NULL;
}

unsigned long long getMS() {
	struct timeval time;
	gettimeofday(&time, NULL);
	return ((unsigned long long)time.tv_sec * 1000000) + time.tv_usec;
}

void usage(char **argv) {
	printf("Usage: %s -k KEY [ -x CORE ] [-d DIRECTORY] [-s STARTNONCE] [-n NONCES] [-m STAGGERSIZE] [-t THREADS] -a\n", argv[0]);
        printf("   CORE:\n");
        printf("     0 - default core\n");
        printf("     1 - SSE2 core\n");
#ifdef AVX2
        printf("     2 - AVX2 core\n");
#endif
	printf("   -a = ASYNC writer mode (will use 2x memory!)\n");
	exit(-1);
}

void *writecache(void *arguments) {
	unsigned long long bytes = (unsigned long long) staggersize * PLOT_SIZE;
	unsigned long long position = 0;
	int percent;

	percent = (int)(100 * lastrun / nonces);

	if(asyncmode == 1) {
		printf("\33[2K\r%i Percent done. (ASYNC write)", percent);
		fflush(stdout);
	} else {
		printf("\33[2K\r%i Percent done. (write)", percent);
		fflush(stdout);
	}

	do {
		int b = write(ofd, &wcache[position], bytes > 100000000 ? 100000000 : bytes);	// Dont write more than 100MB at once
		position += b;
		bytes -= b;
	} while(bytes > 0);

	unsigned long long ms = getMS() - starttime;
	
	percent = (int)(100 * lastrun / nonces);
	double minutes = (double)ms / (1000000 * 60);
	int speed = (int)(staggersize / minutes);
	int m = (int)(nonces - run) / speed;
	int h = (int)(m / 60);
	m -= h * 60;

	printf("\33[2K\r%i Percent done. %i nonces/minute, %i:%02i left", percent, speed, h, m);
	fflush(stdout);

	return NULL;
}

int main(int argc, char **argv) {
	if(argc < 2) 
		usage(argv);

	int i;
	int startgiven = 0;
        for(i = 1; i < argc; i++) {
		// Ignore unknown argument
                if(argv[i][0] != '-')
			continue;

		if(!strcmp(argv[i],"-a")) {
			asyncmode=1;
			printf("Async mode set.\n");
			continue;
		}

		char *parse = NULL;
		unsigned long long parsed;
		char param = argv[i][1];
		int modified, ds;

		if(argv[i][2] == 0) {
			if(i < argc - 1)
				parse = argv[++i];
		} else {
			parse = &(argv[i][2]);
		}
		if(parse != NULL) {
			modified = 0;
			parsed = strtoull(parse, 0, 10);
			switch(parse[strlen(parse) - 1]) {
				case 't':
				case 'T':
					parsed *= 1000;
				case 'g':
				case 'G':
					parsed *= 1000;
				case 'm':
				case 'M':
					parsed *= 1000;
				case 'k':
				case 'K':
					parsed *= 1000;
					modified = 1;
			}
			switch(param) {
				case 'k':
					addr = parsed;
					break;
				case 's':
					startnonce = parsed;
					startgiven = 1;
					break;
				case 'n':
					if(modified == 1) {
						nonces = (unsigned long long)(parsed / PLOT_SIZE);
					} else {
						nonces = parsed;
					}	
					break;
				case 'm':
					if(modified == 1) {
						staggersize = (unsigned long long)(parsed / PLOT_SIZE);
					} else {
						staggersize = parsed;
					}	
					break;
				case 't':
					threads = parsed;
					break;
				case 'x':
					selecttype = parsed;
					break;
				case 'd':
					ds = strlen(parse);
					outputdir = (char*) malloc(ds + 2);
					memcpy(outputdir, parse, ds);
					// Add final slash?
					if(outputdir[ds - 1] != '/') {
						outputdir[ds] = '/';
						outputdir[ds + 1] = 0;
					} else {
						outputdir[ds] = 0;
					}
					
			}			
		}
        }

        if(selecttype == 1) printf("Using SSE2 core.\n");
#ifdef AVX2
        else if(selecttype == 2) printf("Using AVX2 core.\n");
#endif
        else {
		printf("Using original algorithm.\n");
		selecttype=0;
	}

	if(addr == 0)
		usage(argv);

	// Autodetect threads
	if(threads == 0)
		threads = getNumberOfCores();

	// No startnonce given: Just pick random one
	if(startgiven == 0) {
		// Just some randomness
		srand(time(NULL));
		startnonce = (unsigned long long)rand() * (1 << 30) + rand();
	}

	// No nonces given: use whole disk
	if(nonces == 0) {
		unsigned long long fs = freespace(outputdir);
		if(fs <= FREE_SPACE) {
			printf("Not enough free space on device\n");
			exit(-1);
		}
		fs -= FREE_SPACE;
				
		nonces = (unsigned long long)(fs / PLOT_SIZE);
	}

	// Autodetect stagger size
	if(staggersize == 0) {
		// use 80% of memory
		unsigned long long memstag = (freemem() * 0.8) / PLOT_SIZE;

		if(nonces < memstag) {
			// Small stack: all at once
			staggersize = nonces;
		} else {
			// Determine stagger that (almost) fits nonces
			for(i = memstag; i >= 1000; i--) {
				if( (nonces % i) < 1000) {
					staggersize = i;
					nonces-= (nonces % i);
					i = 0;
				}
			}
		}
	}

	// 32 Bit and above 4GB?
	if( sizeof( void* ) < 8 ) {
		if( staggersize > 15000 ) {
			printf("Cant use stagger sizes above 15000 with 32-bit version\n");
			exit(-1);
		}
	}

	// Adjust according to stagger size
	if(nonces % staggersize != 0) {
		nonces -= nonces % staggersize;
		nonces += staggersize;
		printf("Adjusting total nonces to %u to match stagger size\n", nonces);
	}

	printf("Creating plots for nonces %llu to %llu (%u GB) using %u MB memory and %u threads\n", startnonce, (startnonce + nonces), (unsigned int)(nonces / 4 / 953), (unsigned int)(staggersize / 4 * (1 + asyncmode)), threads);

	// Comment this out/change it if you really want more than 200 Threads
	if(threads > 200) {
		printf("%u threads? Sure?\n", threads);
		exit(-1);
	}

	if(asyncmode == 1) {
		acache[0] = calloc( PLOT_SIZE, staggersize );
		acache[1] = calloc( PLOT_SIZE, staggersize );

		if(acache[0] == NULL || acache[1] == NULL) {
			printf("Error allocating memory. Try lower stagger size or removing ASYNC mode.\n");
			exit(-1);
		}
	} else {
		cache = calloc( PLOT_SIZE, staggersize );

		if(cache == NULL) {
			printf("Error allocating memory. Try lower stagger size.\n");
			exit(-1);
		}
	}

	mkdir(outputdir, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH);

	char name[100];
	sprintf(name, "%s%llu_%llu_%u_%u", outputdir, addr, startnonce, nonces, staggersize);

	ofd = open(name, O_CREAT | O_LARGEFILE | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(ofd < 0) {
		printf("Error opening file %s\n", name);
		exit(0);
	}

	// Threads:
	noncesperthread = (unsigned long)(staggersize / threads);

	if(noncesperthread == 0) {
		threads = staggersize;
		noncesperthread = 1;
	}

	pthread_t worker[threads], writeworker;
	unsigned long long nonceoffset[threads];

	int asyncbuf=0;
	unsigned long long astarttime;
	if(asyncmode == 1) cache=acache[asyncbuf];
	else wcache=cache;

	for(run = 0; run < nonces; run += staggersize) {
		astarttime = getMS();

		for(i = 0; i < threads; i++) {
			nonceoffset[i] = startnonce + i * noncesperthread;

			if(pthread_create(&worker[i], NULL, work_i, &nonceoffset[i])) {
				printf("Error creating thread. Out of memory? Try lower stagger size / less threads\n");
				exit(-1);
			}
		}

		// Wait for Threads to finish;
		for(i=0; i<threads; i++) {
			pthread_join(worker[i], NULL);
		}
		
		// Run leftover nonces
		for(i=threads * noncesperthread; i<staggersize; i++)
			nonce(addr, startnonce + i, (unsigned long long)i);

		// Write plot to disk:
		starttime=astarttime;
		if(asyncmode == 1) {
			if(run > 0) pthread_join(writeworker, NULL);
			lastrun=run+staggersize;
			wcache=cache;
			if(pthread_create(&writeworker, NULL, writecache, (void *)NULL)) {
				printf("Error creating thread. Out of memory? Try lower stagger size / less threads / remove async mode\n");
				exit(-1);
			}	
			asyncbuf=1-asyncbuf;			
			cache=acache[asyncbuf];
		} else {
			lastrun=run+staggersize;
			if(pthread_create(&writeworker, NULL, writecache, (void *)NULL)) {
				printf("Error creating thread. Out of memory? Try lower stagger size / less threads\n");
				exit(-1);
			}
			pthread_join(writeworker, NULL);
		}

		startnonce += staggersize;
	}
	
	if(asyncmode == 1) pthread_join(writeworker, NULL);

	close(ofd);

	printf("\nFinished plotting.\n");
	return 0;
}


