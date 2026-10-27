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
static unsigned stopped,called,canceled;
static char last_uri[128],last_data[256];
static uint64_t milliseconds(void){return clock_ms;}
static void release_capture(bool clear){(void)clear;stopped++;}
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
static void media(PlaybackProtection state){
 const char *json=state==PLAYBACK_CLEAR?"[{\"type\":\"media\",\"is_foreground\":true,\"resource\":[{\"resource\":\"VDEC\"}]}]":state==PLAYBACK_PROTECTED?"[{\"resource\":[{\"resource\":\"SVP_CPB\"}]}]":"[]";
 media_reply(NULL,(void*)json,NULL);
}
static void answer(int command,bool running,bool connected){
 char json[128];snprintf(json,sizeof(json),"{\"returnValue\":true,\"isRunning\":%s,\"connected\":%s}",running?"true":"false",connected?"true":"false");
 piccap_reply(NULL,json,(void*)(intptr_t)command);
}
static void confirm_stop(void){answer(2,true,false);assert(piccap_owned);answer(0,false,false);assert(!piccap_owned && capture_source==SOURCE_HISTOGRAM);}
static void stable_clear(void){
 media(PLAYBACK_UNKNOWN);media(PLAYBACK_CLEAR);assert(!handover_tick());clock_ms+=1000;
 media(PLAYBACK_CLEAR);assert(!handover_tick());clock_ms+=1000;
 media(PLAYBACK_CLEAR);assert(handover_tick());assert(strstr(last_uri,"/getSettings"));
}
static void connected_start(void){
 piccap_reply(NULL,"{\"returnValue\":true,\"address\":\"127.0.0.1\",\"port\":19400,\"unix-socket\":false}",(void*)(intptr_t)3);
 assert(strstr(last_uri,"/start"));answer(1,false,false);clock_ms+=600;
 media(PLAYBACK_CLEAR);handover_tick();answer(0,true,true);assert(capture_source==SOURCE_PICCAP);
}
int main(void){
 (void)target_save;(void)target_load;(void)target_open;(void)handover_save;(void)handover_load;
 (void)retry_failure;(void)retry_take;(void)retry_update;
 assert(playback_json_init(&playback_json));call_once=mock_call;cancel_call=mock_cancel;
 handover_enabled=true;next_media_probe=UINT64_MAX;
 media(PLAYBACK_PROTECTED);assert(!handover_tick() && called==0);
 stable_clear();assert(stopped==1);connected_start();
 media(PLAYBACK_UNKNOWN);assert(handover_tick() && capture_source==SOURCE_PICCAP);
 clock_ms+=1000;assert(handover_tick() && capture_source==SOURCE_PICCAP);answer(0,true,true);
 media(PLAYBACK_CLEAR);assert(handover_tick() && capture_source==SOURCE_PICCAP);
 media(PLAYBACK_UNKNOWN);clock_ms+=100;media(PLAYBACK_PROTECTED);
 assert(handover_tick() && capture_source==SOURCE_PICCAP_STOPPING && strstr(last_uri,"/stop"));confirm_stop();
 assert(retries.due==clock_ms);clock_ms+=3001;handover_cooldown=0;stable_clear();connected_start();
 media(PLAYBACK_UNKNOWN);clock_ms+=1600;handover_tick();assert(capture_source==SOURCE_PICCAP_STOPPING);confirm_stop();
 clock_ms+=3001;handover_cooldown=0;stable_clear();connected_start();
 answer(0,true,false);clock_ms+=1000;media(PLAYBACK_CLEAR);handover_tick();answer(0,true,true);
 assert(!piccap_disconnected_since && piccap_fallbacks==0);
 answer(0,true,false);clock_ms+=2500;media(PLAYBACK_CLEAR);handover_tick();answer(0,true,false);
 clock_ms+=2501;media(PLAYBACK_CLEAR);handover_tick();
 assert(capture_source==SOURCE_PICCAP_STOPPING && piccap_fallbacks==1 && strstr(piccap_error,"disconnected"));confirm_stop();
 assert(retries.due==clock_ms);clock_ms+=2000;media(PLAYBACK_CLEAR);assert(!handover_tick());
 clock_ms=handover_cooldown+1;media(PLAYBACK_CLEAR);assert(handover_tick());connected_start();
 retry_pause(&retries);handover_tick();assert(capture_source==SOURCE_PICCAP_STOPPING);confirm_stop();assert(!retries.due);
 retry_reset(&retries);clock_ms+=3001;handover_cooldown=0;stable_clear();
 strcpy(target.host,"10.1.0.100");target.piccap_port=19446;
 handover_tick();assert(capture_source==SOURCE_PICCAP_STOPPING);answer(3,true,false);handover_tick();confirm_stop();
 clock_ms+=3001;handover_cooldown=0;stable_clear();
 piccap_reply(NULL,"{\"returnValue\":true,\"address\":\"127.0.0.1\",\"port\":19400,\"unix-socket\":false}",(void*)(intptr_t)3);
 assert(strstr(last_uri,"/setSettings") && !strcmp(last_data,"{\"address\":\"10.1.0.100\",\"port\":19446,\"unix-socket\":false}"));
 answer(4,true,false);clock_ms+=600;media(PLAYBACK_CLEAR);handover_tick();answer(0,true,true);
 handover_enabled=false;handover_tick();confirm_stop();handover_enabled=true;handover_cooldown=0;stable_clear();
 clock_ms+=3100;handover_tick();assert(canceled==1 && capture_source==SOURCE_PICCAP_STOPPING);confirm_stop();
 handover_cooldown=0;clock_ms+=3001;stable_clear();
 piccap_reply(NULL,"{\"returnValue\":false}",(void*)(intptr_t)3);assert(capture_source==SOURCE_PICCAP_STOPPING && piccap_error[0]);
 clock_ms+=600;handover_tick();confirm_stop();
 strcpy(target.host,"2001:db8::1");handover_cooldown=0;media(PLAYBACK_CLEAR);clock_ms+=2000;media(PLAYBACK_CLEAR);assert(!handover_tick());
 puts("PASS: stable clear gate, bounded pause grace, immediate DRM fallback, disconnect/reconnect and cooldown, exclusive capture, custom targets and failures");return 0;
}
