#include <assert.h>
#include <stdio.h>
#include "playback.h"
/* LG's G4 resource model is what moves the capture source, so all three states
 * are pinned here: protected (PicCap can only see black), clear (PicCap is the
 * better source) and unknown (leave whatever runs alone). */
int main(void){
 PlaybackJson j={0};assert(playback_json_init(&j));
 assert(playback_state(&j,"[]")==PLAYBACK_UNKNOWN);
 assert(playback_state(&j,"not json")==PLAYBACK_UNKNOWN);
 assert(playback_state(&j,"{\"returnValue\":false}")==PLAYBACK_UNKNOWN);
 assert(playback_state(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_CLEAR);
 assert(playback_state(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resources\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_CLEAR);
 assert(playback_state(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"},{\"resource\":\"SVP_CPB\"}]}]")==PLAYBACK_PROTECTED);
 assert(playback_state(&j,"[{\"is_foreground\":false,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_UNKNOWN);
 assert(playback_state(&j,"[{\"is_foreground\":true,\"type\":\"hdmi\",\"resource\":[{\"resource\":\"VDEC\"}]}]")==PLAYBACK_UNKNOWN);
 assert(playback_state(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]},{}]")==PLAYBACK_UNKNOWN);
 assert(playback_state(&j,"[{}, {\"resource\":[{\"resource\":\"SVP_CPB\"}]}]")==PLAYBACK_PROTECTED);
 assert(playback_state(&j,"[{\"is_foreground\":true,\"type\":\"media\",\"resource\":[{\"resource\":\"VDEC\"}]},{\"is_foreground\":false,\"resource\":[{\"resource\":\"SVP_CPB\"}]}]")==PLAYBACK_PROTECTED);
 assert(playback_state(&j,"[{\"resource\":[{\"resource\":\"svp_cpb\"}]}]")==PLAYBACK_UNKNOWN);
 /* The idle screen this TV reports while HyperDRM itself holds the capture. */
 assert(playback_state(&j,"[{\"type\":\"media\",\"is_foreground\":false},{\"type\":\"vtclient\",\"is_foreground\":false,\"resource\":[{\"resource\":\"VT_CAPTURE\"}]}]")==PLAYBACK_UNKNOWN);
 puts("Playback classification tests passed");return 0;
}
