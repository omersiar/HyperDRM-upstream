#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "histogram.h"
#include "continuity.h"
int main(void){
 Histogram hist[16]={0};assert(!histogram_populated(hist));
 for(unsigned r=0;r<16;r++)for(unsigned c=0;c<3;c++)hist[r].bins[c][0]=100;
 assert(histogram_populated(hist));hist[4].bins[2][0]=0;assert(!histogram_populated(hist));
 Continuity s={0};unsigned char red[16][3]={{0}},blue[16][3]={{0}},black[16][3]={{0}},out[16][3];
 for(unsigned r=0;r<16;r++){red[r][0]=200;blue[r][2]=200;}
 assert(!continuity_frame(&s,false,red,1000,out));
 assert(continuity_frame(&s,true,red,1100,out));assert(!memcmp(out,red,sizeof(out)));
 assert(continuity_frame(&s,false,black,1200,out));assert(!memcmp(out,red,sizeof(out)));
 assert(continuity_frame(&s,false,black,2699,out));assert(!continuity_frame(&s,false,black,2700,out));
 assert(continuity_frame(&s,true,blue,2800,out));
 continuity_seed(&s,red,3000);continuity_frame(&s,true,blue,3100,out);assert(out[0][0]==200 && out[0][2]==0);
 continuity_frame(&s,true,blue,3350,out);assert(out[0][0]==100 && out[0][2]==100);
 continuity_frame(&s,true,blue,3600,out);assert(out[0][0]==0 && out[0][2]==200 && !s.fading);
 continuity_seed(&s,red,4000);continuity_frame(&s,true,black,4100,out);assert(!memcmp(out,black,sizeof(out)) && !s.fading);
 continuity_frame(&s,false,red,4200,out);assert(!memcmp(out,black,sizeof(out)));
 for(unsigned r=0;r<16;r++)for(unsigned c=0;c<3;c++)black[r][c]=2;
 continuity_seed(&s,red,4300);continuity_frame(&s,true,black,4400,out);assert(out[0][0]==2 && out[0][1]==2 && !s.fading);
 puts("PASS: empty data versus populated black, bounded hold, recovery fade, immediate real black");return 0;
}
