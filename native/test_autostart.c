#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define TEST_ROOT "/tmp/hyperdrm-autostart-test"
#define BOOT_DIRECTORY TEST_ROOT "/init.d"
#define BOOT_SCRIPT TEST_ROOT "/boot.sh"
#include "autostart.h"
static void put_file(const char *path,const char *data){
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0700);assert(fd>=0);
 size_t size=strlen(data);assert(write(fd,data,size)==(ssize_t)size);assert(close(fd)==0);
}
int main(void){
 /* Fail rather than touching another run's files. */
 assert(mkdir(TEST_ROOT,0700)==0);put_file(BOOT_SCRIPT,"#!/bin/sh\nexit 0\n");
 assert(!boot_enabled() && set_boot(false));
 assert(set_boot(true));assert(boot_hook_kind()==2 && boot_enabled());
 assert(set_boot(true));assert(set_boot(false) && boot_hook_kind()==0);
 put_file(BOOT_HOOK,legacy_boot_script);assert(boot_hook_kind()==1);
 assert(set_boot(true) && boot_hook_kind()==2);assert(set_boot(false));
 put_file(BOOT_HOOK,"foreign startup\n");assert(!set_boot(true) && !set_boot(false));
 assert(unlink(BOOT_HOOK)==0);
 assert(symlink("/foreign/script",BOOT_HOOK)==0);assert(!set_boot(true) && !set_boot(false));
 assert(unlink(BOOT_HOOK)==0);
 put_file(BOOT_HOOK ".tmp","foreign temporary\n");assert(!set_boot(true));assert(boot_hook_kind()==0);
 assert(unlink(BOOT_HOOK ".tmp")==0);
 assert(set_boot(true));assert(unlink(BOOT_SCRIPT)==0);assert(!boot_enabled());
 assert(!set_boot(true));assert(set_boot(false));
 assert(rmdir(BOOT_DIRECTORY)==0 && rmdir(TEST_ROOT)==0);
 puts("PASS: symlink enable/disable, legacy migration, foreign files preserved, missing target harmless");
}
