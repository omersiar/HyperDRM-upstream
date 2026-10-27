#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <sys/wait.h>
#include "target.h"
#include "layout.h"
#include "diagnostics.h"
static void fixture(PlaybackJson *json,int kind){
 int server=socket(AF_INET,SOCK_STREAM,0);assert(server>=0);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_addr={htonl(INADDR_LOOPBACK)}};
 assert(bind(server,(void*)&address,sizeof(address))==0 && listen(server,1)==0);
 socklen_t length=sizeof(address);assert(getsockname(server,(void*)&address,&length)==0);
 TargetConfig config=target;config.port=ntohs(address.sin_port);if(kind==1)strcpy(config.token,"fixture-token");
 pid_t child=fork();assert(child>=0);
 if(!child){
  int fd=accept(server,NULL,NULL);if(fd<0)_exit(2);char request[512]={0};size_t used=0;
  while(used+1<sizeof(request)){ssize_t n=read(fd,request+used,sizeof(request)-used-1);if(n<=0)_exit(3);used+=(size_t)n;if(strchr(request,'\n'))break;}
  if(!strstr(request,kind==1?"authorize":"serverinfo") || strstr(request,"color") || strstr(request,"image"))_exit(4);
  const char *response=kind==1?"{\"success\":false}\n":kind==2?"{\"success\":true,\"info\":{\"leds\":[]}}\n":"{\"success\":true,\"info\":{\"leds\":[{\"hmin\":0,\"hmax\":1,\"vmin\":0,\"vmax\":0.1}]}}\n";
  if(write(fd,response,7)!=7)_exit(5);
  usleep(20000);
  if(write(fd,response+7,strlen(response)-7)!=(ssize_t)strlen(response)-7)_exit(6);
  close(fd);close(server);_exit(0);
 }
 assert(test_begin(&config,1000));assert(!test_begin(&config,1000));
 for(unsigned i=0;test_busy() && i<400;i++){test_tick(json,1000+i*10);usleep(1000);}
 assert(!test_busy());assert(connection_test.stage==(kind?TEST_FAILED:TEST_DONE));
 if(!kind)assert(connection_test.leds==1 && !connection_test.error[0]);
 if(kind==1)assert(strstr(connection_test.error,"authentication"));
 if(kind==2)assert(strstr(connection_test.error,"layout"));
 int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 close(server);assert(test_begin(&config,10000));test_tick(json,10010);assert(connection_test.stage==TEST_FAILED);
}
int main(void){
 (void)target_save;(void)target_load;(void)target_open;(void)test_stage_name;
 PlaybackJson json={0};assert(playback_json_init(&json));
 fixture(&json,0);fixture(&json,1);fixture(&json,2);
 int peers[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,peers)==0);
 connection_test.fd=peers[0];connection_test.stage=TEST_LAYOUT;connection_test.started=20000;connection_test.deadline=23000;
 test_tick(&json,23000);assert(connection_test.stage==TEST_FAILED && strstr(connection_test.error,"timed out"));close(peers[1]);
 puts("PASS: read-only connection test, fragmented response, layout validation, rejected token, connection refusal and timeout");return 0;
}
