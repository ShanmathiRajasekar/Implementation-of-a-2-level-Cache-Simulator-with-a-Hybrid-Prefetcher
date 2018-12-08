/*
 * File: prefetcher.C
 *
 */

#include "prefetcher.h"
#include <stdio.h>

/* tagged prefetcher, prefetch the next block(s) based on when they
   are accessed. Our state is a massive bit array indicated whether
   or not an element is "tagged" - meaning that it has been issued as 
   a prefetcher request.  Whenever the prefetcher request is used, we 
   prefetch the next block(s). The size of the bit array is exactly
   STATE_SIZE number of bytes: 2048 in this case. */
static char tags[STATE_SIZE];

/* The second major component of our state is the RPT table, which
   stores the PC of the instructions that make memory requests.  It 
   also stores the address of the last memory request by that PC and
   the "stride" which is defined as the distance between two accesses.
   If the "stride" is large enough and regular, then this is used to 
   prefetch instead of just grabbing sequential blocks. The values of 
   the pc, and stride could be shortened into one 32-bit block, but we 
   currently have 3 4-byte integers: 12 bytes per entry.  Thus, the 
   state contribution is NUM_RPT_ENTRIES * 12: 1536 bytes. */
static char rpt_check[NUM_RPT_ENTRIES/8 + 1];

/* We hold another bit array which is used to check whether or not the
   RPT table should be used when a prefetched block is accessed by the
   CPU. It has as many bits as there are entries into the RPT table.  
   Thus, it's state contribution is NUM_RPT_ENTRIES/8 bytes: 
   128/8 = 16 bytes. */
static rpt_row_entry_t rpt_table[NUM_RPT_ENTRIES]; 

/* The following bitArray functions all use 3 ints and 3 chars 
   for their private variables which results in a negligible
   15 bytes of state plus a bit for the return value. */

/* private functions for bit-array management*/
static
bool checkBit(u_int32_t addr, char *bitArray, int size){
  int bit_index, char_index, rem_bits;
  char section, selector, result;
  bit_index = addr % (size * sizeof(char));
  char_index = bit_index/BITS_PER_CHAR;  //8 bits per char
  rem_bits = bit_index - (char_index * BITS_PER_CHAR);
  selector = 1;
  selector = selector << rem_bits;
  section = bitArray[char_index];
  result = section & selector;
  if(result > 0){
    return true;
  }else{
    return false;
  }
}

static
void markBit(u_int32_t addr, char *bitArray, int size){
  int bit_index, char_index, rem_bits;
  char section, selector, result;
  bit_index = addr % (size * sizeof(char));
  char_index = bit_index/BITS_PER_CHAR; //8 bits per char
  rem_bits = bit_index - (char_index * BITS_PER_CHAR);
  /*printf("For address %d, bit_index %d, char_index %d and rem_bits %d\n",
    addr, bit_index, char_index, rem_bits); */
  selector = 1;
  selector = selector << rem_bits;
  bitArray[char_index] = bitArray[char_index] | selector;
  return;
}

static
void unmarkBit(u_int32_t addr, char *bitArray, int size){
  int bit_index, char_index, rem_bits;
  char section, selector, result;
  bit_index = addr % (size * sizeof(char));
  char_index = bit_index/BITS_PER_CHAR; //8 bits per char
  rem_bits = bit_index - (char_index * BITS_PER_CHAR);
  selector = 1;
  selector = selector << rem_bits;
  bitArray[char_index] = bitArray[char_index] & (~selector);
  return;
}
/* i consumes 4 bytes */
Prefetcher::Prefetcher() { 
  int i;
    _ready = false; 
    /* mark all tags as not prefetched, and clear RPT table */
    memset(tags, 0, STATE_SIZE);
    for(i = 0; i < NUM_RPT_ENTRIES; i++){
      rpt_table[i].pc = 0;
      rpt_table[i].last_stride = 0;
      rpt_table[i].last_mem = 0;
      unmarkBit(i, rpt_check, (NUM_RPT_ENTRIES/8 + 1));
    }

}

bool Prefetcher::hasRequest(u_int32_t cycle) { return _ready; }

Request Prefetcher::getRequest(u_int32_t cycle) { return _nextReq; }

/* completeRequest uses an additional pointer and two integers
   which results in an additional 12 bytes of state. */
void Prefetcher::completeRequest(u_int32_t cycle) { 
    int rpt_row_num, curr_stride;
    rpt_row_entry_t *curr_row;

    if(_req_left == 0){
    _ready = false; 
  }else{
    _req_left--;
    rpt_row_num = _nextReq.pc % NUM_RPT_ENTRIES;
    curr_row = &rpt_table[rpt_row_num];
    if(curr_row->pc == _nextReq.pc && checkBit(rpt_row_num, rpt_check, (NUM_RPT_ENTRIES/8 + 1))){
      /*this pc is associated with a valid rpt entry, use stride */
      _nextReq.addr = _nextReq.addr + curr_row->last_stride;
    }else{
      _nextReq.addr = _nextReq.addr + L2_BLOCK_SIZE;
    }
    markBit(_nextReq.addr, tags, STATE_SIZE);  //mark this as a prefetched block 
  }
}
/* cpuRequest uses the same as above, 12 bytes of 'state'*/
void Prefetcher::cpuRequest(Request req) { 
        int rpt_row_num, curr_stride;
	rpt_row_entry_t *curr_row;

	/*if it is a hit and this was a prefetch address, 
	  get the next one.  Not worries about duplicate reqs. */ 
        if(req.HitL1 && checkBit(req.addr, tags, STATE_SIZE) && !_ready){
	  /* determine if we need stride or normal block addition */
	  /* check entry in RPT table */
	       rpt_row_num = req.pc % NUM_RPT_ENTRIES;
	       curr_row = &rpt_table[rpt_row_num];
	       if(curr_row->pc == req.pc && 
		  checkBit(rpt_row_num, rpt_check, (NUM_RPT_ENTRIES/8 + 1))){
		 /* this has a valid entry in the RPT table, use its offset */
		 curr_stride = curr_row->last_stride;
		 _nextReq.addr = req.addr + curr_stride;
	       }else{
		 /* no valid entry, use normal sequential */
		 _nextReq.addr = req.addr + L2_BLOCK_SIZE;
	       }
	       markBit(_nextReq.addr, tags, STATE_SIZE);  //mark this as a prefetched block 
	       _ready = true;
	       _req_left = NUM_REQS_PER_MISS - 1;
	}else if(/*!_ready && */!req.HitL1) {
	  /* this was a pure miss, do RPT processing */
	       rpt_row_num = req.pc % NUM_RPT_ENTRIES;
	       curr_row = &rpt_table[rpt_row_num];
	       if(curr_row->pc == req.pc){
		 /* this entry is in the table */
		 if((curr_stride = req.addr - (curr_row->last_mem)) == curr_row->last_stride && curr_stride > WORTHWHILE_RPT){
		   /* if stride is the same as this one,
		      "punch-it" use it to prefetch */
		   /*		     printf("PRE: same stride found for address %x, with lastmem of %x and curr_req of %x, previous stride at %d\n",
				     req.pc, curr_row->last_mem, req.addr, curr_stride); */
		     /* mark this as a 'strong' rpt value to be used */
		     markBit(rpt_row_num, rpt_check, (NUM_RPT_ENTRIES/8 + 1));
		     _nextReq.addr = req.addr + curr_stride;
		 }else{
		     /* update to new stride  and do standard prefetch */
		      curr_row->last_stride = curr_stride; 
		      _nextReq.addr = req.addr + L2_BLOCK_SIZE;
		      /* we didn't get two same in a row, don't use */
                      unmarkBit(rpt_row_num, rpt_check, (NUM_RPT_ENTRIES/8 + 1));
		 } 
	       }else{
		 /* no pc in table, so do standard prefetch*/
		   _nextReq.addr = req.addr + L2_BLOCK_SIZE;
		   /* also update stride to 0 so new entry not confused */
		   curr_row->last_stride = 0;
		   unmarkBit(rpt_row_num, rpt_check, (NUM_RPT_ENTRIES/8 + 1));
	       }
	       /* in all cases, update row in RPT and make prefetch req */
	       curr_row->pc = req.pc;
	       curr_row->last_mem = req.addr;
	       _ready = true;
	       _req_left = NUM_REQS_PER_MISS - 1;  
	}
        /* mark prefetch tag as 0 since CPU requested this */
        unmarkBit(req.addr, tags, STATE_SIZE);

}
