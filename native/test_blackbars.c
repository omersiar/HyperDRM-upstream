#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "histogram.h"
#include "blackbars.h"
/* A region that is 80 % code 20 and 20 % code 200. Mean, median and peak all
 * disagree, which is what makes the display statistic observable. */
static void split_region(Histogram *hist, unsigned dark, unsigned bright, unsigned dark_count,
                         unsigned bright_count) {
    memset(hist, 0, sizeof(*hist));
    for (unsigned c = 0; c < 3; c++) {
        hist->bins[c][dark] = (uint16_t)dark_count;
        hist->bins[c][bright] = (uint16_t)bright_count;
    }
}
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
 /* Detection must not follow the display statistic. The same bar pattern is
  * judged from the mean even when the profile renders a percentile: a p50
  * reading is 0 in a dark-but-lit interior, which hid every letterbox. */
 Histogram hist[16];
 for(unsigned i=0;i<16;i++)split_region(&hist[i],20,200,800,200);
 unsigned char detect[16][3],p50[16][3],p90[16][3];
 histogram_detection_colors(hist,detect);
 for(unsigned i=0;i<16;i++)for(unsigned c=0;c<3;c++){
  p50[i][c]=color_encode(histogram_linear_percentile(hist[i].bins[c],500));
  p90[i][c]=color_encode(histogram_linear_percentile(hist[i].bins[c],900));
 }
 /* The three statistics really are different, so the assertions below matter. */
 assert(detect[2][0]>35 && detect[2][0]!=p50[2][0] && detect[2][0]!=p90[2][0]);
 assert(p50[2][0]==20 && p90[2][0]==200);
 unsigned bar_black[16]={0};
 bar_black[3]=bar_black[11]=bar_black[12]=66;
 bar_black[1]=55;bar_black[5]=bar_black[9]=33;
 assert(letterbox_candidate(bar_black,detect)==1);
 assert(letterbox_candidate(bar_black,p50)==-1);
 assert(letterbox_candidate(bar_black,p90)==1);
 /* The bar meter has the same requirement: one stray bright pixel in the bar
  * strip makes a peak reading claim there is no bar, while the mean still sees
  * one. layout_bar_height(level) is y1 * (1 - level/255) for r14. */
 Histogram bar[16];
 for(unsigned i=0;i<16;i++)split_region(&bar[i],0,255,990,10);
 unsigned char bar_detect[16][3],bar_peak[16][3];
 histogram_detection_colors(bar,bar_detect);
 for(unsigned i=0;i<16;i++)for(unsigned c=0;c<3;c++)
  bar_peak[i][c]=color_encode(histogram_linear_percentile(bar[i].bins[c],1000));
 assert(bar_detect[14][1]<40);
 assert(bar_peak[14][1]==255);
 assert(0.12*(1.0-bar_detect[14][1]/255.0)>0.05);
 assert(0.12*(1.0-bar_peak[14][1]/255.0)==0.0);
 puts("PASS: measured wide/thin bars, full frame, one-sided/isolated edges, dark frame, low contrast and a detection input that ignores the display statistic");
}
