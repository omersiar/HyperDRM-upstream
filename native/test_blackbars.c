#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "blackbars.h"
int main(void){
 unsigned black[16]={0};unsigned char color[16][3];memset(color,100,sizeof(color));
 assert(letterbox_candidate(black,color)==0);
 black[3]=black[11]=black[12]=66;black[1]=55;black[5]=black[9]=33;
 assert(letterbox_candidate(black,color)==1);
 black[1]=black[5]=black[9]=0;assert(letterbox_candidate(black,color)==-1);
 for(unsigned i=0;i<16;i++)black[i]=100;
 memset(color,0,sizeof(color));assert(letterbox_candidate(black,color)==-1);
 memset(black,0,sizeof(black));assert(letterbox_candidate(black,color)==-1);
 unsigned thin_black[16]={12,23,1,21,9,6,6,16,18,12,26,23,23,22,37,8};
 unsigned char thin_color[16][3]={{57,52,48},{97,94,92},{51,42,35},{25,24,21},
 {71,65,60},{128,122,118},{84,74,67},{34,32,27},{47,41,36},{59,47,43},
 {37,29,25},{19,18,15},{22,20,16},{22,19,15},{14,13,11},{78,75,72}};
 assert(letterbox_candidate(thin_black,thin_color)==1);
 thin_black[1]=thin_black[5]=thin_black[9]=0;
 assert(letterbox_candidate(thin_black,thin_color)!=1);
 thin_black[1]=23;thin_black[5]=6;thin_black[9]=12;
 thin_black[11]=0;
 assert(letterbox_candidate(thin_black,thin_color)!=1);
 thin_black[11]=23;
 memset(thin_color,10,sizeof(thin_color));
 assert(letterbox_candidate(thin_black,thin_color)==-1);
 memset(thin_color,100,sizeof(thin_color));
 assert(letterbox_candidate(thin_black,thin_color)!=1);
 for(unsigned i=0;i<16;i++)thin_black[i]=15;
 assert(letterbox_candidate(thin_black,thin_color)!=1);
 puts("PASS: measured wide/thin bars, full frame, one-sided/isolated edges, dark frame and low contrast");
}
