#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

typedef void (*work_t)(void);

pid_t waitpid_eintr(int *status);

void watchdog_worker_timer(work_t work, long long timeout);

void handle_alarm_signal(int signal, siginfo_t *info, void *context);
void setup_timer(long long timeout);
void system_timers(work_t work, long long timeout);

void endless_loop(void);
void short_task(void);

int main() {
  system_timers(endless_loop, 1000);
  system_timers(short_task, 1000);

  watchdog_worker_timer(endless_loop, 1000);
  watchdog_worker_timer(short_task, 1000);

  return 0;
}

pid_t waitpid_eintr(int *status) {
  pid_t pid = 0;
  while ( (pid = waitpid(WAIT_ANY, status, 0)) == -1 ) {
    if (errno == EINTR) {
      continue;
    } else {
      perror("waitpid");
      abort();
    }
  }
  return pid;
}

void watchdog_worker_timer(work_t work, long long timeout) {
  const pid_t timer_pid = fork();
  if (timer_pid == -1) {
    perror("fork timer");
    abort();
  }

  if (timer_pid == 0) {
    /// Timer process
    usleep(timeout * 1000);
    exit(0);
  }

  const pid_t worker_pid = fork();
  if (worker_pid == -1) {
    perror("fork worker");
    abort();
  }
  if (worker_pid == 0) {
    /// Worker process
    work();
    exit(0);
  }

  int status = 0;
  const pid_t finished_first = waitpid_eintr(&status);
  if (finished_first == timer_pid) {
    printf("timed out\n");
    kill(worker_pid, SIGKILL);
  } else if (finished_first == worker_pid) {
    printf("all good\n");
    kill(timer_pid, SIGKILL);
  } else {
    assert(0 && "Something went wrong");
  }

  waitpid_eintr(&status);
}

void handle_alarm_signal(int signal, siginfo_t *info, void *context) {
  _exit(112);
}

void setup_timer(long long timeout) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = &handle_alarm_signal;
  if (sigaction(SIGALRM, &action, NULL) != 0) {
    perror("sigaction");
    abort();
  }

  struct itimerval timer;
  timer.it_value.tv_sec = timeout / 1000;
  /// Cut off seconds, and convert what's left into microseconds
  timer.it_value.tv_usec = (timeout % 1000) * 1000;

  /// Do not repeat
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;

  if (setitimer(ITIMER_REAL, &timer, NULL) != 0) {
    perror("setitimer");
    abort();
  }
}

void system_timers(work_t work, long long timeout) {
  const pid_t worker_pid = fork();
  if (worker_pid == -1) {
    perror("fork worker");
    abort();
  }
  if (worker_pid == 0) {
    setup_timer(timeout);
    work();
    exit(0);
  }

  int status = 0;
  waitpid_eintr(&status);

  if (WIFEXITED(status) && WEXITSTATUS(status) == 112) {
    printf("timed out\n");
  }

  else if (WIFEXITED(status) && WEXITSTATUS(status) != 144) {
    printf("all good\n");
  }
}

void endless_loop(void) {
  printf("starting endless loop\n");

  /// Avoiding UB
  volatile int x = 0;
  while (1) {
    if (x != 0) {
      break;
    }
  }
}

void short_task(void) {
  printf("finished short task\n");
}


