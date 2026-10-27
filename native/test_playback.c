#include <assert.h>
#include <stdio.h>
#include "playback.h"
int main(void){
 PlaybackJson j={0};assert(playback_json_init(&j));
 assert(playback_classify(&j,"[]")==PLAYBACK_UNKNOWN);
 assert(playback_classify(&j,"not json")==PLAYBACK_UNKNOWN);
 assert(playback_classify(&j,"{\"returnValue\":false}")==PLAYBACK_UNKNOWN);
 assert(playback_classify(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_CLEAR);
 assert(playback_classify(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resources\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_CLEAR);
 assert(playback_classify(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"},{\"resource\":\"SVP_CPB\"}]}]")==PLAYBACK_PROTECTED);
 assert(playback_classify(&j,"[{\"is_foreground\":false,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_UNKNOWN);
 assert(playback_classify(&j,"[{\"is_foreground\":true,\"type\":\"hdmi\",\"resource\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_UNKNOWN);
 assert(playback_classify(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]},{}]")==PLAYBACK_UNKNOWN);
 assert(playback_classify(&j,"[{}, {\"resource\":[{\"resource\":\"SVP_CPB\"}]}]")==PLAYBACK_PROTECTED);
 assert(playback_classify(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]},{\"is_foreground\":false,\"resource\":[{\"resource\":\"SVP_CPB\"}]}]")==PLAYBACK_PROTECTED);
 puts("Playback classification tests passed");return 0;
}
