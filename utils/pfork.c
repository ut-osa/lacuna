#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#define CLONE_PRIVATE 0x02000000
#define FORK_FLAGS (CLONE_PRIVATE | SIGCHLD)

extern char **environ;
int main(int argc, char *argv[]) {
   int status,  i = 0;
   char buf[256] = {0};
   if (argc > 1) {
      pid_t pid = syscall(__NR_clone, FORK_FLAGS, NULL);
      if (!pid) {
         while (environ[i++])
            ;
         snprintf(buf, 255, "_=%s", argv[1]);
         i--;
         environ[--i] = buf;

         execve(argv[1], argv + 1, environ);
      } else if (pid > 0) {
          waitpid(pid, &status, 0);
      }
   }
   return 0;
}
