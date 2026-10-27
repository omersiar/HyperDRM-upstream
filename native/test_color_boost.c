#include <assert.h>
#include <stdio.h>
#include "color_boost.h"
int main(void){
 unsigned char out[3];
 for(unsigned v=0;v<256;v++){
  unsigned char gray[3]={v,v,v};boost_color(gray,out);
  unsigned expected=v;
  assert(out[0]==expected && out[1]==expected && out[2]==expected);
 }
 unsigned char cyan[3]={93,224,242};boost_color(cyan,out);
 assert(out[0]==93 && out[1]==224 && out[2]==242);
 for(unsigned c=0;c<3;c++){
  unsigned char primary[3]={0};primary[c]=255;boost_color(primary,out);
  for(unsigned j=0;j<3;j++)assert(out[j]==primary[j]);
 }
 unsigned char warm[3]={171,82,0};boost_color(warm,out);
 assert(out[0]==171 && out[1]==82 && out[2]==0);
 puts("PASS: neutral grays, unchanged cyan, warm color and primaries");
}
