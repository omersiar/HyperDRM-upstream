#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "histogram.h"
#include "continuity.h"
#include "interpolation.h"
#define PRIORITY 149
typedef struct {unsigned char a,b;unsigned weight;} LedBlend;
typedef enum {SOURCE_HISTOGRAM,SOURCE_PICCAP_STARTING,SOURCE_PICCAP,SOURCE_PICCAP_STOPPING} CaptureSource;
static CaptureSource capture_source;
static struct {bool paused,blocked;} retries;
static struct {char receiver[16];} target={"Hyperion"};
static struct {int stage;bool valid;uint64_t updated;unsigned sequence,count;LedBlend *mapping;unsigned char colors[16][3];} stream;
#include "runtime_context.h"
static CaptureContext capture_ctx;
static bool piccap_connected;
static char *queued;
static bool stream_queue(const char *line){if(queued)return false;queued=strdup(line);return queued!=NULL;}
static char *spatial_image_command_duration(const unsigned char colors[16][3],unsigned priority,unsigned duration){(void)colors;(void)priority;(void)duration;return strdup("image");}
#include "source_bridge.h"
static void consume(void){free(queued);queued=NULL;}
int main(void){
 LedBlend mapping={0,0,0};stream.mapping=&mapping;stream.count=1;stream.stage=4;
 for(unsigned r=0;r<16;r++){capture_ctx.continuity.last[r][0]=200;stream.colors[r][2]=200;}capture_ctx.continuity.available=true;capture_ctx.continuity.valid_at=1000;
 capture_source=SOURCE_PICCAP_STARTING;bridge_tick(1000,&capture_ctx);assert(bridge_phase==1 && strstr(queued,"200,0,0"));consume();
 capture_source=SOURCE_PICCAP;piccap_connected=true;bridge_tick(1200,&capture_ctx);assert(bridge_phase==2 && strstr(queued,"clear"));consume();
 stream.valid=true;stream.sequence=1;stream.updated=1300;bridge_tick(1300,&capture_ctx);assert(bridge_phase==2);
 stream.sequence=2;stream.updated=1400;bridge_tick(1400,&capture_ctx);assert(bridge_phase==3 && strstr(queued,"200,0,0"));consume();
 bridge_tick(1650,&capture_ctx);assert(strstr(queued,"100,0,100"));consume();
 bridge_tick(1900,&capture_ctx);assert(!bridge_phase && strstr(queued,"clear"));consume();
 capture_source=SOURCE_PICCAP_STOPPING;stream.updated=2000;bridge_tick(2000,&capture_ctx);assert(capture_ctx.continuity.fading && capture_ctx.continuity.last[0][2]==200 && strstr(queued,"0,0,200"));consume();
 capture_source=SOURCE_HISTOGRAM;bridge_tick(2100,&capture_ctx);assert(!queued);
 capture_source=SOURCE_PICCAP_STARTING;capture_ctx.continuity.valid_at=3000;bridge_tick(3000,&capture_ctx);consume();bridge_tick(6000,&capture_ctx);assert(!bridge_phase && strstr(queued,"clear"));consume();
 capture_source=SOURCE_PICCAP_STARTING;bridge_source=SOURCE_HISTOGRAM;capture_ctx.continuity.valid_at=7000;bridge_tick(7000,&capture_ctx);consume();retries.paused=true;bridge_tick(7100,&capture_ctx);assert(!bridge_phase && strstr(queued,"clear"));consume();
 retries.paused=false;bridge_source=SOURCE_HISTOGRAM;capture_ctx.continuity.valid_at=8000;capture_source=SOURCE_PICCAP_STARTING;bridge_tick(8000,&capture_ctx);consume();
 capture_source=SOURCE_PICCAP;bridge_tick(8100,&capture_ctx);consume();stream.sequence++;stream.updated=8300;memset(stream.colors,0,sizeof(stream.colors));bridge_tick(8300,&capture_ctx);assert(!bridge_phase && !queued);
 bridge_source=SOURCE_HISTOGRAM;capture_source=SOURCE_PICCAP_STARTING;capture_ctx.continuity.valid_at=1000;bridge_tick(9000,&capture_ctx);assert(!bridge_phase && !queued);
 stream.stage=1;bridge_phase=1;bridge_started=9000;bridge_tick(12000,&capture_ctx);assert(!queued);retries.paused=true;bridge_tick(12100,&capture_ctx);assert(!bridge_phase && !queued);
 strcpy(target.receiver,"HyperHDR");char *line=region_command(stream.colors,&mapping,1,500);assert(!strcmp(line,"image"));free(line);assert(!region_command(stream.colors,NULL,0,500));
 puts("PASS: both source transitions, fresh sample gate, fade midpoint/end, bounded timeout, manual-stop cleanup");return 0;
}
