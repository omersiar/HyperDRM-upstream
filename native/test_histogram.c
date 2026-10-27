#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "histogram.h"
int main(void) {
 Histogram h={0};
 assert(histogram_mean(h.bins[0])==0);
 for(unsigned intensity=0;intensity<256;intensity++) {
  memset(&h,0,sizeof(h));h.bins[0][intensity]=65535;
  assert(histogram_mean(h.bins[0])==intensity);
 }
 /* Adjacent halfwords must stay independent, including the final bin. */
 memset(&h,0,sizeof(h));h.bins[0][254]=65535;h.bins[0][255]=1;
 assert(histogram_mean(h.bins[0])==254);
 /* Weighted sum exceeds uint32 for full uint16 counts in every bin. */
 for(unsigned b=0;b<256;b++)h.bins[0][b]=65535;
 /* Mean of all levels evaluated in light, not of the encoded indices. */
 assert(histogram_mean(h.bins[0])==151);
 /* A region mixing equal parts black and white keeps the light of grey. */
 memset(&h,0,sizeof(h));h.bins[0][0]=1;h.bins[0][255]=1;
 assert(histogram_mean(h.bins[0])==188);
 for(unsigned primary=0;primary<3;primary++) {
  memset(&h,0,sizeof(h));
  for(unsigned c=0;c<3;c++)h.bins[c][c==primary?255:0]=100;
  for(unsigned c=0;c<3;c++)assert(histogram_mean(h.bins[c])==(c==primary?255:0));
 }
 puts("PASS: ABI, all 256 intensities, adjacent bins, linear-light mean, overflow, RGB primaries");
 return 0;
}
