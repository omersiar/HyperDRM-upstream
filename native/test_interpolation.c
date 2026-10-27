#include <assert.h>
#include <stdio.h>
#include "interpolation.h"
int main(void){
 const unsigned char a[3]={255,0,0},b[3]={0,255,0};unsigned char out[3];
 blend_color(a,b,0,out);assert(out[0]==255 && out[1]==0 && out[2]==0);
 blend_color(a,b,65535,out);assert(out[0]==0 && out[1]==255 && out[2]==0);
 /* Half of the way in light, not in encoded values: 188, not 127/128. */
 blend_color(a,b,32768,out);assert(out[0]==188 && out[1]==188 && out[2]==0);
 unsigned previous=0;
 for(unsigned w=0;w<=65535;w++){
  blend_color(a,b,w,out);assert(out[1]>=previous && out[2]==0);previous=out[1];
  unsigned char gray[3]={173,173,173};blend_color(gray,gray,w,out);
  assert(out[0]==173 && out[1]==173 && out[2]==173);
 }
 puts("PASS: endpoints, linear-light midpoint, all weights monotonic, constant gray preserved");
}
