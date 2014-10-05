/*
        Uses burstcoin plot files to mine coins
        Author: Markus Tervooren <info@bchain.info>
        BURST-R5LP-KEL9-UYLG-GFG6T

	With code written by Uray Meiviar <uraymeiviar@gmail.com>
	BURST-8E8K-WQ2F-ZDZ5-FQWHX

        Implementation of Shabal is taken from:
        http://www.shabal.com/?p=198

	Usage: ./mine <node ip> [<plot dir> <plot dir> ..]
*/

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h> 
#include <pthread.h>

#include "shabal.h"
#include "helper.h"

// Do not report results with deadline above this to the node. If you mine solo set this to 10000 to avoid stressing out the node.
#define MAXDEADLINE	5000000

// Change if you need
#define DEFAULT_PORT	8125

// These are fixed for BURST. Dont change!
#define HASH_SIZE	32
#define HASH_CAP	4096
#define PLOT_SIZE	(HASH_CAP * HASH_SIZE * 2)

// Read this many nonces at once. 100k = 6.4MB per directory/thread.
// More may speedup things a little.
#define CACHESIZE	100000

#define BUFFERSIZE	2000

unsigned long long addr;
unsigned long long startnonce;
int scoop;

unsigned long long best;
unsigned long long bestn;
unsigned long long deadline;

unsigned long long targetdeadline;

char signature[33];
char oldSignature[33];

char nodeip[16];
int nodeport = DEFAULT_PORT;

unsigned long long bytesRead = 0;
unsigned long long height = 0;
unsigned long long baseTarget = 0;
time_t starttime;

int stopThreads = 0;

pthread_mutex_t byteLock;

#define SHARECACHE	1000

#ifdef SHARE_POOL
int sharefill;
unsigned long long sharekey[SHARECACHE];
unsigned long long sharenonce[SHARECACHE];
#endif

// Buffer to read the passphrase to. Only when SOLO mining
#ifdef SOLO
char passphrase[BUFFERSIZE + 1];
#endif

char readbuffer[BUFFERSIZE + 1];

// Some more buffers
char writebuffer[BUFFERSIZE + 1];

char *contactWallet(char *req, int bytes) {
	int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

	struct sockaddr_in ss;
	ss.sin_addr.s_addr = inet_addr( nodeip );
	ss.sin_family = AF_INET;
	ss.sin_port = htons( nodeport );

	struct timeval tv;
	tv.tv_sec =  15;
	tv.tv_usec = 0;  

	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

	if(connect(s, (struct sockaddr*)&ss, sizeof(struct sockaddr_in)) == -1) {
		printf("\nError sending result to node                           \n");
		fflush(stdout);
		return NULL;
	}

	int written = 0;
	do {
		int w = write(s, &req[written], bytes - written);
		if(w < 1) {
			printf("\nError sending request to node                     \n");
			return NULL;
		}
		written += w;
	} while(written < bytes);

	int bytesread = 0, rbytes;
	do {
		rbytes = read(s, &readbuffer[bytesread], BUFFERSIZE - bytesread);
		if(bytes > 0)
			bytesread += rbytes;

	} while(rbytes > 0 && bytesread < BUFFERSIZE);

	close(s);

	// Finish read
	readbuffer[bytesread] = 0;

	// locate HTTP header end
	char *find = strstr(readbuffer, "\r\n\r\n");

	// No header found
	if(find == NULL)
		return NULL;

	return find + 4;
}

void procscoop(unsigned long long nonce, int n, char *data, unsigned long long account_id) {
	char *cache;
	char sig[32 + 64];

	cache = data;

	int v;	

	memmove(sig, signature, 32);

	for(v=0; v<n; v++) {
		memmove(&sig[32], cache, 64);

		shabal_context x;
		shabal_init(&x, 256);
		shabal(&x, sig, 64 + 32);

		char res[32];

		shabal_close(&x, 0, 0, res);

		unsigned long long *wertung = (unsigned long long*)res;

// Sharepool: Submit all deadlines below threshold
// Uray_pool: Submit best deadline
// Solo: Best deadline, but not low quality deadlines

#ifdef SHARE_POOL
		// For sharepool just store results for later submission	
		if(*wertung < targetdeadline * baseTarget && sharefill < SHARECACHE) {
			sharekey[sharefill] = account_id;
			sharenonce[sharefill] = nonce;
			sharefill++;
		}
#else 
		if(bestn == 0 || *wertung <= best) {
			best = *wertung;
			bestn = nonce;

#ifdef SOLO
			if(best < baseTarget * MAXDEADLINE) {		// Has to be this good before we inform the node
#endif


#ifdef URAY_POOL
	                        int bytes = sprintf(writebuffer, "POST /burst?requestType=submitNonce&accountId=%llu&nonce=%llu HTTP/1.0\r\nConnection: close\r\n\r\n", account_id,bestn);
#else
				int bytes = sprintf(writebuffer, "POST /burst?requestType=submitNonce&secretPhrase=%s&nonce=%llu HTTP/1.0\r\nConnection: close\r\n\r\n", passphrase, bestn);
#endif
				char *buffer = contactWallet( writebuffer, bytes );

				if(buffer != NULL) {
					char *rdeadline = strstr(buffer, "\"deadline\":");
					if(rdeadline != NULL) {
						rdeadline += 11;
						char *end = strstr(rdeadline, "}");
						if(end != NULL) {	
							// Parse and check if we have a better deadline
							unsigned long long ndeadline = strtoull(rdeadline, 0, 10);
							if(ndeadline < deadline || deadline == 0)
								deadline = ndeadline;
						}
					} else {
						printf("\nWalet reported no deadline.\n");
					}
#ifdef SOLO
					// Deadline too high? Passphrase may be wrong.
					if(deadline > MAXDEADLINE) {
						printf("\nYour deadline is larger than it should be. Check if you put the correct passphrase to passphrases.txt.\n");
						fflush(stdout);
					}
#endif

				}
#ifdef SOLO
			}
#endif
		}

#endif
		nonce++;
		cache += 64;
	}
}


void *work_i(void *x_void_ptr) {
        char *x_ptr = (char*)x_void_ptr;

	char *cache = (char*) malloc(CACHESIZE * HASH_SIZE * 2);

	if(cache == NULL) {
		printf("\nError allocating memory                         \n");
		exit(-1);
	}


	DIR *d;
	struct dirent *dir;
	d = opendir(x_ptr);

	if (d) {
		while ((dir = readdir(d)) != NULL) {
			unsigned long long key, nonce, nonces, stagger, n;
	
			char fullname[512];
			strcpy(fullname, x_ptr);

			if(sscanf(dir->d_name, "%llu_%llu_%llu_%llu", &key, &nonce, &nonces, &stagger)) {
				// Does path end with a /? If not, add it.
				if( fullname[ strlen( x_void_ptr ) ] == '/' ) {
					strcpy(&fullname[ strlen( x_void_ptr ) ], dir->d_name);
				} else {
					fullname[ strlen( x_void_ptr ) ] = '/';
					strcpy(&fullname[ strlen( x_void_ptr ) + 1 ], dir->d_name);
				}

				int fh = open(fullname, O_RDONLY);

				if(fh < 0) {
                                        printf("\nError opening file %s                             \n", fullname);
                                        fflush(stdout);
				}

				unsigned long long offset = stagger * scoop * HASH_SIZE * 2;
				unsigned long long size = stagger * HASH_SIZE * 2;

				for(n=0; n<nonces; n+=stagger) {
					// Read one Scoop out of this block:
					// start to start+size in steps of CACHESIZE * HASH_SIZE * 2

					unsigned long long start = n * HASH_CAP * HASH_SIZE * 2 + offset, i;
					unsigned long long noffset = 0;
					for(i = start; i < start + size; i += CACHESIZE * HASH_SIZE * 2) {
						unsigned int readsize = CACHESIZE * HASH_SIZE * 2;
						if(readsize > start + size - i)
							readsize = start + size - i;

						int bytes = 0, b;
						do {
							b = pread(fh, &cache[bytes], readsize - bytes, i);
							bytes += b;
						} while(bytes < readsize && b > 0);	// Read until cache is filled (or file ended)
			
						if(b != 0) {
							procscoop(n + nonce + noffset, readsize / (HASH_SIZE * 2), cache, key);	// Process block

							// Lock and add to totals
							pthread_mutex_lock(&byteLock);
							bytesRead += readsize;
							pthread_mutex_unlock(&byteLock);
						}

						noffset += CACHESIZE;
					}
			
					if(stopThreads) {	// New block while processing: Stop.
						close(fh);
						closedir(d);
						free(cache);
						return NULL;	
					}
				}
				close(fh);
			}
		}
		closedir(d);
	}
	free(cache);
	return NULL;
}

int pollNode() {

	// Share-pool works differently
#ifdef SHARE_POOL
	int bytes = sprintf(writebuffer, "GET /pool/getMiningInfo HTTP/1.0\r\nHost: %s:%i\r\nConnection: close\r\n\r\n", nodeip, nodeport);
#else
	int bytes = sprintf(writebuffer, "POST /burst?requestType=getMiningInfo HTTP/1.0\r\nConnection: close\r\n\r\n");
#endif

	char *buffer = contactWallet( writebuffer, bytes );

	if(buffer == NULL)
		return 0;

	// Parse result
#ifdef SHARE_POOL
	char *rbaseTarget = strstr(buffer, "\"baseTarget\": \"");
	char *rheight = strstr(buffer, "\"height\": \"");
	char *generationSignature = strstr(buffer, "\"generationSignature\": \"");
	char *tdl = strstr(buffer, "\"targetDeadline\": \"");

	if(rbaseTarget == NULL || rheight == NULL || generationSignature == NULL || tdl == NULL)
		return 0;

	char *endBaseTarget = strstr(rbaseTarget + 15, "\"");
	char *endHeight = strstr(rheight + 11, "\"");
	char *endGenerationSignature = strstr(generationSignature + 24, "\"");
	char *endtdl = strstr(tdl + 19, "\"");

	if(endBaseTarget == NULL || endHeight == NULL || endGenerationSignature == NULL || endtdl == NULL)
		return 0;

	// Set endpoints
	endBaseTarget[0] = 0;
	endHeight[0] = 0;
	endGenerationSignature[0] = 0;
	endtdl[0] = 0;
	
	// Parse
	if(xstr2strr(signature, 33, generationSignature + 24) < 0) {
		printf("\nNode response: Error decoding generationsignature          	\n");
		fflush(stdout);
		return 0;
	}

	height = strtoull(rheight + 11, 0, 10);
	baseTarget = strtoull(rbaseTarget + 15, 0, 10);
	targetdeadline = strtoull(tdl + 19, 0, 10);
#else 
	char *rbaseTarget = strstr(buffer, "\"baseTarget\":\"");
	char *rheight = strstr(buffer, "\"height\":\"");
	char *generationSignature = strstr(buffer, "\"generationSignature\":\"");
	if(rbaseTarget == NULL || rheight == NULL || generationSignature == NULL)
		return 0;

	char *endBaseTarget = strstr(rbaseTarget + 14, "\"");
	char *endHeight = strstr(rheight + 10, "\"");
	char *endGenerationSignature = strstr(generationSignature + 23, "\"");
	if(endBaseTarget == NULL || endHeight == NULL || endGenerationSignature == NULL)
		return 0;

	// Set endpoints
	endBaseTarget[0] = 0;
	endHeight[0] = 0;
	endGenerationSignature[0] = 0;
	
	// Parse
	if(xstr2strr(signature, 33, generationSignature + 23) < 0) {
		printf("\nNode response: Error decoding generationsignature          	\n");
		fflush(stdout);
		return 0;
	}

	height = strtoull(rheight + 10, 0, 10);
	baseTarget = strtoull(rbaseTarget + 14, 0, 10);
#endif

	return 1;
}

void update() {
	// Try until we get a result.
	while(pollNode() == 0) {
		printf("\nCould not get mining info from Node. Will retry..             \n");
		fflush(stdout);
		struct timespec wait;
		wait.tv_sec = 1;
		wait.tv_nsec = 0;
		nanosleep(&wait, NULL);
	};
}

int main(int argc, char **argv) {
	int i;
	if(argc < 3) {
		printf("Usage: ./mine <node url> [<plot dir> <plot dir> ..]\n");
		exit(-1);
	}

#ifdef SOLO
	// Reading passphrase from file
	int pf = open( "passphrases.txt", O_RDONLY );
	if( pf < 0 ) {
		printf("Could not find file passphrases.txt\nThis file should contain the passphrase used to create the plotfiles\n");
		exit(-1);
	}
	
	int bytes = read( pf, passphrase, 2000 );

	// Replace spaces with +
	for( i=0; i<bytes; i++ ) {
		if( passphrase[i] == ' ' )
			passphrase[i] = '+';

		// end on newline
		if( passphrase[i] == '\n' || passphrase[i] == '\r')
			passphrase[i] = 0;
	}

	passphrase[bytes] = 0;
#endif

	// Check if all directories exist:
	struct stat d = {0};

	for(i = 2; i < argc; i++) {
		if ( stat( argv[i], &d) ) {
			printf( "Plot directory %s does not exist\n", argv[i] );
			exit(-1);
		} else {
			if( !(d.st_mode & S_IFDIR) ) {
				printf( "%s is not a directory\n", argv[i] );
				exit(-1);
			}
		}
	}
	
	char *hostname = argv[1];

	// Contains http://? strip it.
	if(strncmp(hostname, "http://", 7) == 0)
		hostname += 7;

	// Contains Port? Extract and strip.
	char *p = strstr(hostname, ":");
	if(p != NULL) {
		p[0] = 0;
		p++;
		nodeport = atoi(p);
	}

	printf("Using %s port %i\n", hostname, nodeport);

	hostname_to_ip(hostname, nodeip);

	memset(oldSignature, 0, 33);

	pthread_t worker[argc];
	time(&starttime);

	// Get startpoint:
	update();	

	// Main loop
	for(;;) {
		// Get scoop:
		char scoopgen[40];
		memmove(scoopgen, signature, 32);

		char *mov = (char*)&height;

		scoopgen[32] = mov[7]; scoopgen[33] = mov[6]; scoopgen[34] = mov[5]; scoopgen[35] = mov[4]; scoopgen[36] = mov[3]; scoopgen[37] = mov[2]; scoopgen[38] = mov[1]; scoopgen[39] = mov[0];

		shabal_context x;
		shabal_init(&x, 256);
		shabal(&x, scoopgen, 40);
		char xcache[32];
		shabal_close(&x, 0, 0, xcache);
		
		scoop = (((unsigned char)xcache[31]) + 256 * (unsigned char)xcache[30]) % HASH_CAP;

		// New block: reset stats
		best = bestn = deadline = bytesRead = 0;

#ifdef SHARE_POOL
		sharefill = 0;
#endif

		for(i = 2; i < argc; i++) {
			if(pthread_create(&worker[i], NULL, work_i, argv[i])) {
				printf("\nError creating thread. Out of memory? Try lower stagger size\n");
				exit(-1);
			}
		}

#ifdef SHARE_POOL
	// Collect threads back in for dev's pool:
                for(i = 2; i < argc; i++)
                       pthread_join(worker[i], NULL);

		if(sharefill > 0) {
			char *f1 = (char*) malloc(SHARECACHE * 100);
			char *f2 = (char*) malloc(SHARECACHE * 100);

			int used = 0;
			for(i = 0; i<sharefill; i++)
				used += sprintf(&f1[used], "%llu:%llu\n", sharekey[i], sharenonce[i]);

			
			int ilen = 1, red = used;
			while(red > 10) {
				ilen++;
				red /= 10;
			}

			int db = sprintf(f2, "POST /pool/submitWork HTTP/1.0\r\nHost: %s:%i\r\nContent-Type: text/plain;charset=UTF-8\r\nContent-Length: %i\r\n\r\n{\"%s\", %u}", nodeip, nodeport, used + 6 + ilen , f1, used);

			printf("\nServer response: %s\n", contactWallet(f2, db));
			
			free(f1);
			free(f2);
		}
#endif

		memmove(oldSignature, signature, 32);
	
		// Wait until block changes:
		do {
			update();
			
			time_t ttime;
			time(&ttime);
#ifdef SHARE_POOL
			printf("\r%llu MB read/%llu GB total/%i shares@target %llu                 ", (bytesRead / ( 1024 * 1024 )), (bytesRead / (256 * 1024)), sharefill, targetdeadline);
#else
			if(deadline == 0)	
				printf("\r%llu MB read/%llu GB total/no deadline                 ", (bytesRead / ( 1024 * 1024 )), (bytesRead / (256 * 1024)));
			else 
				printf("\r%llu MB read/%llu GB total/deadline %llus (%llis left)           ", (bytesRead / ( 1024 * 1024 )), (bytesRead / (256 * 1024)), deadline, (long long)deadline + (unsigned int)starttime - (unsigned int)ttime);
#endif

			fflush(stdout);

			struct timespec wait;
			// Query faster when solo mining
#ifdef SOLO
			wait.tv_sec = 1;
#else 
			wait.tv_sec = 5;
#endif
			wait.tv_nsec = 0;
			nanosleep(&wait, NULL);
		} while(memcmp(signature, oldSignature, 32) == 0);	// Wait until signature changed

		printf("\nNew block %llu, basetarget %llu                          \n", height, baseTarget);
		fflush(stdout);

		// Remember starttime
		time(&starttime);

#ifndef SHARE_POOL
		// Tell all threads to stop:
		stopThreads = 1;
		for(i = 2; i < argc; i++)
		       pthread_join(worker[i], NULL);

		stopThreads = 0;
#endif
	}
}

