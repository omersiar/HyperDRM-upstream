#define _DEFAULT_SOURCE
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include "target.h"
static int fixture_connect(const TargetConfig *target){
 int fd=target_open(target);if(fd<0)return -1;
 assert(fcntl(fd,F_GETFL,0)&O_NONBLOCK);
 struct pollfd event={fd,POLLOUT,0};int error=0;socklen_t size=sizeof(error);
 if(poll(&event,1,1000)<=0 || getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size) || error){close(fd);return -1;}
 return fd;
}
int main(void){
 PlaybackJson j={0};assert(playback_json_init(&j));TargetConfig t=target;
 assert(target_valid(&t));assert(t.piccap_port==19400);assert(target_piccap_local(&t));
 assert(target_parse(&j,"{\"host\":\"10.1.0.91\",\"port\":19445,\"receiver\":\"HyperHDR\",\"token\":\"test-token\"}",&t));
 assert(t.port==19445 && !strcmp(t.receiver,"HyperHDR") && !strcmp(t.token,"test-token"));assert(!target_piccap_local(&t));
 assert(target_parse(&j,"{\"host\":\"::1\",\"port\":65535,\"receiver\":\"Hyperion\"}",&t));assert(!strcmp(t.token,"test-token"));
 const char *bad[]={"{}","[]","{\"host\":\"localhost\",\"port\":19444,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":0,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":65536,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":19444.5,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":\"19444\",\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":19444,\"receiver\":\"Other\"}","{\"host\":\"127.0.0.1\",\"port\":19444,\"receiver\":\"HyperHDR\",\"token\":\"bad\\\"token\"}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){TargetConfig before=t;assert(!target_parse(&j,bad[i],&t));assert(!memcmp(&t,&before,sizeof(t)));}
 assert(target_parse(&j,"{\"host\":\"127.0.0.1\",\"port\":19444,\"piccapPort\":65535,\"receiver\":\"HyperHDR\"}",&t));assert(t.piccap_port==65535);
 assert(!target_parse(&j,"{\"host\":\"127.0.0.1\",\"port\":19444,\"piccapPort\":0,\"receiver\":\"HyperHDR\"}",&t));assert(t.piccap_port==65535);
 assert(target_parse(&j,"{\"host\":\"::1\",\"port\":65535,\"receiver\":\"HyperHDR\"}",&t));assert(t.piccap_port==65535);
 assert(!target_parse(&j,"{\"host\":\"127.0.0.1\",\"port\":19444,\"piccapPort\":null,\"receiver\":\"HyperHDR\"}",&t));
 TargetConfig v6=t;strcpy(v6.host,"2001:db8::1");assert(!target_piccap_compatible(&v6));strcpy(v6.host,"::1");assert(target_piccap_compatible(&v6) && !strcmp(target_piccap_host(&v6),"127.0.0.1"));
 const char *file="/tmp/hyperdrm-target-test.json";assert(access(file,F_OK)!=0);assert(access("/tmp/hyperdrm-target-test.json.tmp",F_OK)!=0);
 assert(target_capture_interval(&t)==50);
 assert(target_parse(&j,"{\"host\":\"::1\",\"port\":65535,\"receiver\":\"HyperHDR\",\"captureIntervalMs\":100}",&t));
 assert(t.capture_interval==100);
 const char *invalid_intervals[]={"0","24","26","1001","50.5","null","\"50\""};
 for(unsigned i=0;i<sizeof(invalid_intervals)/sizeof(*invalid_intervals);i++){
  char request[256];snprintf(request,sizeof(request),"{\"host\":\"::1\",\"port\":65535,\"receiver\":\"HyperHDR\",\"captureIntervalMs\":%s}",invalid_intervals[i]);
  TargetConfig before=t;assert(!target_parse(&j,request,&t));assert(!memcmp(&before,&t,sizeof(t)));
 }
 assert(target_save(file,&t));struct stat st;assert(stat(file,&st)==0 && (st.st_mode&0777)==0600);
 TargetConfig restored=target;assert(target_load(&j,file,&restored));assert(restored.capture_interval==100);assert(!strcmp(restored.host,"::1") && restored.port==65535 && !strcmp(restored.token,"test-token"));
 assert(target_parse(&j,"{\"host\":\"127.0.0.1\",\"port\":19444,\"receiver\":\"HyperHDR\",\"clearToken\":true}",&t));assert(!t.token[0]);assert(target_piccap_local(&t));unlink(file);
 int server=socket(AF_INET,SOCK_STREAM,0);assert(server>=0);struct sockaddr_in a={.sin_family=AF_INET,.sin_addr={htonl(INADDR_LOOPBACK)}};
 assert(bind(server,(void*)&a,sizeof(a))==0 && listen(server,1)==0);socklen_t length=sizeof(a);assert(getsockname(server,(void*)&a,&length)==0);t.port=ntohs(a.sin_port);
 int client=fixture_connect(&t);assert(client>=0);int accepted=accept(server,NULL,NULL);assert(accepted>=0);close(accepted);close(client);close(server);
 assert(fixture_connect(&t)<0);
 puts("PASS: target validation, token retention/removal, private persistence, IPv6 parsing, nonblocking socket and refused connection");return 0;
}
