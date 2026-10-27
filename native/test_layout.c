#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "layout.h"
int main(void){
 unsigned sizes[]={50,500};
 for(unsigned k=0;k<2;k++){
  unsigned n=sizes[k];size_t capacity=150*n+100;char *text=malloc(capacity);size_t used=snprintf(text,capacity,"{\"info\":{\"leds\":[");
  for(unsigned i=0;i<n;i++)used+=snprintf(text+used,capacity-used,"%s{\"hmin\":%.8f,\"hmax\":%.8f,\"vmin\":0,\"vmax\":0.05}",i?",":"",(double)i/n,(double)(i+1)/n);
  snprintf(text+used,capacity-used,"]}}");LedBlend *mapping=NULL;unsigned count=0;
  assert(layout_from_json(text,&mapping,&count));assert(count==n);
  for(unsigned i=0;i<n;i++)assert(mapping[i].a<16 && mapping[i].b<16 && mapping[i].weight<=65535);
  assert(mapping[0].a!=mapping[n-1].a);free(mapping);free(text);printf("PASS: %u LEDs, positions and order preserved\n",n);
 }
 const char *bad[]={"{}","{\"info\":{\"leds\":[]}}","{\"info\":{\"leds\":[42]}}","{\"info\":{\"leds\":[{\"hmin\":2,\"hmax\":1,\"vmin\":0,\"vmax\":1}]}}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){LedBlend *mapping=NULL;unsigned count=0;assert(!layout_from_json(bad[i],&mapping,&count));assert(!mapping && !count);}
 puts("PASS: malformed layouts rejected without replacing output");
}
