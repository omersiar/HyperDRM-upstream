#define _DEFAULT_SOURCE
#include <assert.h>
#include <string.h>
#include "layout.h"
int main(void){
 (void)layout_from_json;
 PlaybackJson json={0};assert(playback_json_init(&json));
 assert(calibration_effective()==CAL_AVERAGE);
 strcpy(tv_model,"OLED65G42LW.DEUQLJP");assert(calibration_effective()==CAL_G4);
 assert(!calibration_is_g4("OLED65G52LW") && !calibration_is_g4("LG G4"));
 Calibration next=calibration;
 const char *custom="{\"profile\":\"custom\",\"model\":\"OLED65C42LA\",\"points\":[{\"position\":0,\"region\":2},{\"position\":2,\"region\":10}]}";
 assert(calibration_parse(&json,custom,&next));calibration=next;calibration_generation++;
 LedBlend b=layout_blend(.5,0);assert(b.a==2 && b.b==10 && b.weight>=16383 && b.weight<=16385);
 const char *bad[]={"{}","{\"profile\":\"custom\",\"points\":null}","{\"profile\":\"other\"}","{\"profile\":\"custom\",\"model\":\"bad\\\"name\",\"points\":[]}","{\"profile\":\"custom\",\"model\":\"TV\",\"points\":[{\"position\":2,\"region\":1},{\"position\":1,\"region\":2}]}","{\"profile\":\"custom\",\"model\":\"TV\",\"points\":[{\"position\":0,\"region\":16},{\"position\":4,\"region\":2}]}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){Calibration before=next;assert(!calibration_parse(&json,bad[i],&next));assert(!memcmp(&before,&next,sizeof(next)));}
 const char *path="/tmp/hyperdrm-calibration-test.json";assert(access(path,F_OK)!=0);assert(calibration_save(path,&next));
 calibration=(Calibration){0};calibration_load(&json,path);assert(calibration.count==2 && !strcmp(calibration.model,"OLED65C42LA"));
 Calibration refs=next;refs.has_reference=true;refs.compensate=true;refs.active_mean=true;
 for(unsigned i=0;i<16;i++)for(unsigned c=0;c<3;c++){refs.black[i][c]=(unsigned char)(10+c);refs.white[i][c]=(unsigned char)(200+c);}
 assert(calibration_save(path,&refs));
 calibration=(Calibration){0};calibration_load(&json,path);
 assert(calibration.has_reference && calibration.compensate && calibration.active_mean && calibration.count==2);
 assert(calibration.black[0][0]==10 && calibration.black[3][2]==12 && calibration.white[0][2]==202);
 Calibration cleared=calibration;assert(calibration_parse(&json,custom,&cleared));assert(!cleared.has_reference && cleared.active_mean);
 /* The colour statistic round-trips, and an absent key keeps the current value
  * so a profile saved by an older build cannot silently reset it. */
 assert(cleared.color_stat==CAL_STAT_MEAN);
 Calibration stat=cleared;
 assert(calibration_parse(&json,"{\"profile\":\"custom\",\"model\":\"OLED65C42LA\",\"points\":[{\"position\":0,\"region\":2},{\"position\":2,\"region\":10}],\"colorStat\":\"p90\"}",&stat));
 assert(stat.color_stat==CAL_STAT_P90 && !strcmp(calibration_stat_key(stat.color_stat),"p90"));
 assert(calibration_save(path,&stat));
 calibration=(Calibration){0};calibration_load(&json,path);assert(calibration.color_stat==CAL_STAT_P90);
 Calibration keep=stat;assert(calibration_parse(&json,custom,&keep));assert(keep.color_stat==CAL_STAT_P90);
 assert(calibration_parse(&json,"{\"profile\":\"custom\",\"model\":\"OLED65C42LA\",\"points\":[{\"position\":0,\"region\":2},{\"position\":2,\"region\":10}],\"colorStat\":\"peak\"}",&keep));
 assert(keep.color_stat==CAL_STAT_PEAK);assert(!strcmp(calibration_stat_key(CAL_STAT_MEAN),"mean"));
 unlink(path);
 assert(calibration_parse(&json,"{\"profile\":\"auto\"}",&calibration));assert(calibration_effective()==CAL_G4);
 strcpy(tv_model,"OLED65C42LA");assert(calibration_effective()==CAL_CUSTOM);
 strcpy(tv_model,"UNKNOWN-TV");assert(calibration_effective()==CAL_AVERAGE);
 unsigned char input[16][3],output[16][3];for(unsigned i=0;i<16;i++)for(unsigned c=0;c<3;c++)input[i][c]=i*10+c;
 calibration_colors(input,output);for(unsigned i=0;i<16;i++)for(unsigned c=0;c<3;c++)assert(output[i][c]==75+c);
 calibration.kind=CAL_G4;calibration_colors(input,output);assert(!memcmp(input,output,48));
 puts("PASS: G4 detection, custom model profile, rim mapping, invalid profile rejection, persistence and honest average fallback");return 0;
}
