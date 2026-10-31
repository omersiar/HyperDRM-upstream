#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "retry.h"
typedef struct {int unused;} LSError;
typedef bool (*Callback)(void*,void*,void*);
static RetryState retries;
static uint64_t clock_ms=10000;
static unsigned held,called,canceled;
static bool link_up=true,producing=true;
static char last_uri[128],last_data[256];
static uint64_t milliseconds(void){return clock_ms;}
static void release_capture_hold(uint64_t now){(void)now;held++;}
static const char *payload(void *m){return m;}
static void error_init(LSError *e){(void)e;}
static void error_free(LSError *e){(void)e;}
#define PICCAP_BINARY "/bin/sh"
#include "target.h"
#include "handover.h"
static bool mock_call(void *h,const char *uri,const char *data,Callback cb,void *ctx,unsigned long *token,LSError *e){
 (void)h;(void)cb;(void)ctx;(void)e;*token=++called;
 snprintf(last_uri,sizeof(last_uri),"%s",uri);snprintf(last_data,sizeof(last_data),"%s",data);return true;
}
static bool mock_cancel(void *h,unsigned long token,LSError *e){(void)h;(void)token;(void)e;canceled++;return true;}
static bool tick(bool capture_ok){return handover_tick(capture_ok,producing?clock_ms:0);}
static void answer(int command,bool running,bool connected){
 char json[128];snprintf(json,sizeof(json),"{\"returnValue\":true,\"isRunning\":%s,\"connected\":%s}",running?"true":"false",connected?"true":"false");
 piccap_reply(NULL,json,(void*)(intptr_t)command);
}
static void media(PlaybackState state){
 media_reply(NULL,(void*)(state==PLAYBACK_PROTECTED?"[{\"resource\":[{\"resource\":\"SVP_CPB\"}]}]"
                       :state==PLAYBACK_CLEAR?"[{\"type\":\"media\",\"is_foreground\":true,\"resource\":[{\"resource\":\"VDEC\"}]}]"
                                             :"[]"),NULL);
}
/* Answer whatever request is outstanding the way PicCap would. */
static void serve(void){
 if(!piccap_busy)return;
 if(strstr(last_uri,"/getSettings"))
  piccap_reply(NULL,"{\"returnValue\":true,\"address\":\"127.0.0.1\",\"port\":19400,\"unix-socket\":false}",(void*)(intptr_t)3);
 else if(strstr(last_uri,"/setSettings"))answer(4,true,false);
 else if(strstr(last_uri,"/start")){answer(1,false,false);piccap_running=true;}
 else if(strstr(last_uri,"/stop")){answer(2,true,false);piccap_running=false;}
 else answer(0,piccap_running,link_up);
}
static void advance(uint64_t ms,bool capture_ok,PlaybackState state){
 uint64_t end=clock_ms+ms;
 while(clock_ms<end){uint64_t step=end-clock_ms<1000?end-clock_ms:1000;clock_ms+=step;
  media(state);tick(capture_ok);if(step==1000)serve();}
}
static void reset(void){
 retry_reset(&retries);handover_cooldown=piccap_action_at=0;
 piccap_busy=piccap_connected=piccap_running=piccap_restore=piccap_owned=media_busy=false;
 capture_source=SOURCE_HISTOGRAM;piccap_error[0]=0;piccap_disconnected_since=0;
 playback=PLAYBACK_UNKNOWN;playback_at=clear_since=histogram_since=transition_at=0;clear_samples=0;
 next_media_probe=next_piccap_probe=UINT64_MAX;
 snprintf(target.host,sizeof(target.host),"%s","127.0.0.1");target.piccap_port=19400;
}
/* Feed clear replies until HyperDRM starts PicCap: the unprotected trigger. */
static bool start_piccap(void){
 next_piccap_probe=UINT64_MAX;
 for(int i=0;i<10&&capture_source==SOURCE_HISTOGRAM;i++){
  media(PLAYBACK_CLEAR);clock_ms+=1000;tick(true);}
 return capture_source==SOURCE_PICCAP_STARTING && strstr(last_uri,"/getSettings");
}
/* HyperDRM starts PicCap for unprotected playback and hands it the capture. */
static void own_piccap(void){
 unsigned before=held;
 assert(start_piccap() && piccap_owned && held==before+1);
 serve();assert(strstr(last_uri,"/start"));answer(1,false,false);piccap_running=true;
 clock_ms+=600;assert(tick(true));answer(0,true,true);
 assert(capture_source==SOURCE_PICCAP && piccap_owned && handover_yield());
}
int main(void){
 unsigned base;
 (void)target_save;(void)target_load;(void)target_open;(void)handover_save;(void)handover_load;
 (void)retry_failure;(void)retry_take;(void)retry_update;(void)retry_pause;
 assert(playback_json_init(&playback_json));call_once=mock_call;cancel_call=mock_cancel;
 handover_enabled=true;reset();

 /* Cold boot with nothing playing: the histogram runs and PicCap is left alone. */
 advance(30000,true,PLAYBACK_UNKNOWN);
 assert(called==0 && capture_source==SOURCE_HISTOGRAM && !handover_yield());

 /* One clear reply is not enough to start anything. */
 reset();base=called;
 media(PLAYBACK_CLEAR);assert(!tick(true) && capture_source==SOURCE_HISTOGRAM && called==base);

 /* Unprotected playback: PicCap is started, given the capture, and registered
  * under the receiver through its own priority. */
 own_piccap();

 /* The player hides its decoder on its own menu screens. The source stays where
  * it is -- this is what used to flap between the two captures. */
 advance(30000,false,PLAYBACK_UNKNOWN);
 assert(capture_source==SOURCE_PICCAP && handover_yield());

 /* Protected playback: PicCap can only deliver black, so it is stopped and the
  * histogram takes the capture. */
 media(PLAYBACK_PROTECTED);assert(tick(false));
 assert(capture_source==SOURCE_PICCAP_STOPPING && piccap_restore && strstr(last_uri,"/stop"));
 answer(2,true,false);answer(0,false,false);
 assert(capture_source==SOURCE_HISTOGRAM && !handover_yield() && !piccap_owned);
 assert(histogram_since==clock_ms && retries.quick_retries==2);
 assert(strstr(piccap_error,"Protected playback"));

 /* Protected playback ends: after the dwell PicCap is started again. */
 clock_ms+=HANDOVER_DWELL_MS;
 assert(start_piccap());
 serve();assert(strstr(last_uri,"/start"));answer(1,false,false);piccap_running=true;
 clock_ms+=600;assert(tick(true));answer(0,true,true);
 assert(capture_source==SOURCE_PICCAP && !piccap_restore && handover_yield());

 /* Auto PicCap off with a PicCap the user started: yield, never touch it. */
 reset();handover_enabled=false;media(PLAYBACK_UNKNOWN);
 assert(!tick(false));
 piccap_running=true;assert(tick(false) && capture_source==SOURCE_YIELDED);
 base=called;media(PLAYBACK_CLEAR);tick(true);
 assert(capture_source==SOURCE_YIELDED && called==base && !piccap_owned);
 handover_enabled=true;

 /* Manual Stop stops the PicCap this service started and leaves the histogram. */
 reset();own_piccap();
 retry_pause(&retries);tick(true);
 assert(capture_source==SOURCE_PICCAP_STOPPING && strstr(last_uri,"/stop"));
 answer(2,true,false);answer(0,false,false);
 assert(capture_source==SOURCE_HISTOGRAM && !piccap_owned && !retries.due);
 retry_reset(&retries);

 /* Turning Auto PicCap off does the same. */
 reset();own_piccap();
 handover_enabled=false;tick(true);
 assert(capture_source==SOURCE_PICCAP_STOPPING);
 answer(2,true,false);answer(0,false,false);
 assert(capture_source==SOURCE_HISTOGRAM && !piccap_owned);
 handover_enabled=true;

 /* A PicCap that cannot be configured leaves the histogram running. */
 reset();
 assert(start_piccap());
 piccap_reply(NULL,"{\"returnValue\":false}",(void*)(intptr_t)3);
 assert(capture_source==SOURCE_HISTOGRAM && !piccap_owned && piccap_error[0]);
 reset();
 assert(start_piccap());
 serve();assert(strstr(last_uri,"/start"));
 piccap_reply(NULL,"{\"returnValue\":false}",(void*)(intptr_t)1);
 assert(capture_source==SOURCE_HISTOGRAM && handover_cooldown>clock_ms);
 assert(strstr(piccap_error,"refused"));

 /* Request timeouts cancel instead of waiting for ever. */
 reset();
 assert(start_piccap());
 clock_ms+=3001;tick(true);
 assert(canceled>=1 && strstr(piccap_error,"timed out"));

 /* The histogram produces nothing under this stream: give the capture back
  * instead of leaving PicCap stopped and retrying for ever. */
 reset();own_piccap();
 media(PLAYBACK_PROTECTED);assert(tick(false));answer(2,true,false);answer(0,false,false);
 assert(capture_source==SOURCE_HISTOGRAM && piccap_restore);
 producing=false;
 advance(HANDOVER_TAKEOVER_GIVEUP_MS+3000,false,PLAYBACK_PROTECTED);
 assert(capture_source==SOURCE_PICCAP && !piccap_restore && strstr(piccap_error,"unreadable"));
 producing=true;

/* An IPv6 target means PicCap cannot address the receiver: nothing is managed. */
 reset();strcpy(target.host,"2001:db8::1");base=called;
 advance(30000,true,PLAYBACK_CLEAR);
 assert(called==base && capture_source==SOURCE_HISTOGRAM);
 strcpy(target.host,"127.0.0.1");
 puts("PASS: PicCap runs for unprotected playback, holds through app screens, yields to the histogram for protected streams, and respects manual control");return 0;
}
