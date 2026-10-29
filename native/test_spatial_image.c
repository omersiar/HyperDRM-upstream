#include <assert.h>
#include <string.h>
#include "spatial_image.h"
int main(void){
 (void)layout_from_json;
 unsigned char regions[16][3];for(unsigned i=0;i<16;i++){regions[i][0]=11;regions[i][1]=22;regions[i][2]=33;}
 char *command=spatial_image_command_duration(regions,149,1500);assert(command);
 assert(strstr(command,"\"command\":\"image\"") && strstr(command,"\"priority\":149") && strstr(command,"\"format\":\"rgb\""));
 const char *encoded=strstr(command,"\"imagedata\":\"")+13;
 for(unsigned i=0;i<SPATIAL_PIXELS;i++)assert(!memcmp(encoded+i*4,"CxYh",4));
 assert(!strcmp(encoded+SPATIAL_PIXELS*4,"\"}\n"));free(command);
 for(unsigned i=0;i<16;i++){regions[i][0]=i*16;regions[i][1]=255-i*16;regions[i][2]=i*8;}
 command=spatial_image_command_duration(regions,149,1500);encoded=strstr(command,"\"imagedata\":\"")+13;
 assert(memcmp(encoded,encoded+(SPATIAL_WIDTH-1)*4,4));
 assert(memcmp(encoded,encoded+(SPATIAL_PIXELS-SPATIAL_WIDTH)*4,4));free(command);
 calibration=(Calibration){.kind=CAL_CUSTOM,.count=2,.points={{0,2},{2,10}}};calibration_generation++;
 command=spatial_image_command_duration(regions,149,1500);encoded=strstr(command,"\"imagedata\":\"")+13;
 const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 unsigned char expected[3],actual[3];LedBlend custom=layout_blend(.5/SPATIAL_WIDTH,.5/SPATIAL_HEIGHT);
 blend_color(regions[custom.a],regions[custom.b],custom.weight,expected);
 unsigned v[4];for(unsigned i=0;i<4;i++)v[i]=(unsigned)(strchr(alphabet,encoded[i])-alphabet);
 actual[0]=(v[0]<<2)|(v[1]>>4);actual[1]=(v[1]<<4)|(v[2]>>2);actual[2]=(v[2]<<6)|v[3];assert(!memcmp(expected,actual,3));free(command);
 puts("PASS: image command, exact neutral RGB base64, spatial edge differences and cached geometry");return 0;
}
