/* 
 * File: prefetcher.h
 * Description: Header file for prefetcher implementation
 *
 */

#ifndef PREFETCHER_H
#define PREFETCHER_H

#include <sys/types.h>
#include "mem-sim.h"
#include <cstdlib>
#include <cstring>

/* For state calculations, the individual descriptions are given in
   prefetcher.C.  Assuming the internal variables for the functions
   can be ignored, the state is given by the private variables, and 
   the tables used.  The private variables are a bool, a Request and 
   an int, which sum to 5 bools and 4 ints ~ 17 bytes.  The tagged 
   table size is given by STATE_SIZE = 2048 bytes.  The rpt_check 
   table uses 128/8 = 16 bytes, and the RPT table itself uses 128
   * 12 bytes = 1536.  Thus, in total we have:
   17 + 16 + 1536 + 2048 = 3617 bytes. */
#define STATE_SIZE 2048  /* STATE calculation: 2kb for tagging */
#define BITS_PER_CHAR 8
#define L2_BLOCK_SIZE 32
#define NUM_REQS_PER_MISS 3

#define NUM_RPT_ENTRIES 128 /*STATE 128 * 12 = 1536 (1792 with tag)*/ 
#define WORTHWHILE_RPT 128

typedef struct rpt_row_entry{ /* each row has 4 * 3 = 12 bytes */
  unsigned int pc;
  unsigned int last_mem;
  int last_stride; 
} rpt_row_entry_t;

class Prefetcher {
  private:
	bool _ready;
	Request _nextReq;
	int _req_left;
	

  public:
	Prefetcher();

	/* functions for dealing with the array */
	/*	bool checkPrefetched(u_int32_t addr);
	void markPrefetched(u_int32_t addr);
	void markUnPrefetched(u_int32_t addr);
	bool bitArrayTest(void); */


	// should return true if a request is ready for this cycle
	bool hasRequest(u_int32_t cycle);

	// request a desired address be brought in
	Request getRequest(u_int32_t cycle);

	// this function is called whenever the last prefetcher request was successfully sent to the L2
	void completeRequest(u_int32_t cycle);

	/*
	 * This function is called whenever the CPU references memory.
	 * Note that only the addr, pc, load, issuedAt, and HitL1 should be considered valid data
	 */
	void cpuRequest(Request req); 
};

#endif
