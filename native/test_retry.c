#include <assert.h>
#include <stdio.h>
#include "retry.h"
int main(void){
 RetryState state={0};uint64_t now=1000;
 retry_update(&state,true,false,now);assert(state.due==6000);
 assert(!retry_take(&state,5999));
 for(unsigned i=0;i<10000;i++){
  now=state.due;assert(retry_take(&state,now));retry_failure(&state,now);
  assert(state.due==now+5000);
 }
 assert(state.attempts==10000);
 retry_pause(&state);retry_update(&state,true,false,now+5000);
 assert(state.paused && !state.due && !retry_take(&state,now+100000));
 retry_reset(&state);retry_update(&state,true,false,now);
 assert(state.due);retry_update(&state,false,false,now);assert(!state.due);
 retry_update(&state,true,false,now);assert(state.due);
 state.blocked=true;retry_update(&state,true,false,now);retry_failure(&state,now);assert(!state.due);
 retry_reset(&state);retry_failure(&state,now);
 for(unsigned i=0;i<6;i++){now=state.due;assert(retry_take(&state,now));retry_failure(&state,now);}
 assert(state.attempts==6 && !state.due);
 retry_reset(&state);retry_update(&state,true,true,now);assert(!state.due);
 retry_update(&state,true,false,now);assert(state.due==now+5000);
 retry_reset(&state);retry_source_transition(&state,now);assert(state.due==now+200);
 assert(!retry_take(&state,now+199));assert(retry_take(&state,now+200));retry_failure(&state,now+200);assert(state.due==now+600);
 assert(retry_take(&state,now+600));retry_failure(&state,now+600);assert(state.due==now+1000);
 assert(retry_take(&state,now+1000));retry_failure(&state,now+1000);assert(state.due==now+6000);
 retry_pause(&state);retry_source_transition(&state,now);assert(!state.due);
 retry_reset(&state);state.blocked=true;retry_source_transition(&state,now);assert(!state.due);
 puts("PASS: startup retries, bounded rapid source recovery, Stop pause and cleanup safety block");
}
