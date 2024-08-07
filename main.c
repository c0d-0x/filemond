#define _GNU_SOURCE
#include "./src/filemond.h"
#include "./src/logger.h"
#include <fcntl.h>

config_t *config_obj = NULL;
char *buffer = NULL;
FILE *fp_log = NULL;

void signal_handler(int sig);

int main(int argc, char *argv[]) {

  int fd, poll_num;
  nfds_t nfds;
  struct pollfd fds;

  int fan_fd;
  size_t i = 0;
  struct sigaction sigact;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_handler = signal_handler;
  sigact.sa_flags = SA_RESTART;
  if (sigaction(SIGHUP, &sigact, NULL) != 0 ||
      sigaction(SIGTERM, &sigact, NULL) != 0 ||
      sigaction(SIGUSR1, &sigact, NULL) != 0 ||
      sigaction(SIGINT, &sigact, NULL) != 0) {
    errx(EXIT_FAILURE, "Fail to make reception for signals\n");
  }

  if (check_lock(LOCK_FILE) != 0) {
    exit(EXIT_FAILURE);
  }

  if ((fp_log = fopen(LOCK_FILE, "a")) == NULL) {
    perror("Fail to open logfile");
    exit(EXIT_FAILURE);
  }

  // fanotify for mornitoring files.
  fan_fd = fanotify_init(FAN_CLOEXEC | FAN_CLASS_CONTENT | FAN_NONBLOCK,
                         O_RDONLY | O_LARGEFILE);
  if (fan_fd == -1) {
    perror("Fanotify_Init Failed");
    exit(EXIT_FAILURE);
  }

  config_obj = load_config_file(CONFIG_FILE);
  if (config_obj->watchlist_len == 0 || config_obj->watchlist->path == NULL) {
    errx(EXIT_SUCCESS, "%s is empty! Add files or dirs to be watched\n",
         CONFIG_FILE);
  }

  while (i < config_obj->watchlist_len) {

    // if (config_obj->watchlist[i].F_TYPE == F_NT_FND) {
    // i++;
    //   continue;
    // }

    if (fanotify_mark(fan_fd,
                      (config_obj->watchlist[i].F_TYPE)
                          ? FAN_MARK_ADD | FAN_MARK_ONLYDIR
                          : FAN_MARK_ADD,
                      FAN_OPEN_PERM | FAN_MODIFY | FAN_EVENT_ON_CHILD, AT_FDCWD,
                      config_obj->watchlist[i].path) == -1) {
      perror("Fanotify_Mark");
      exit(EXIT_FAILURE);
    }

    // for debugging and testing purposes
    printf("[%ld]-Path: %s - %ld \n", i, (config_obj->watchlist[i].path),
           (config_obj->watchlist[i].F_TYPE));
    i++;
  }

  config_obj_cleanup(config_obj);
  nfds = 1;
  fds.fd = fan_fd; /* Fanotify input */
  fds.events = POLLIN;

  while (true) {

    poll_num = poll(&fds, nfds, -1);
    if (poll_num == -1) {
      if (errno == EINTR) /* Interrupted by a signal */
        continue;         /* Restart poll() */

      perror("Poll Failed"); /* Unexpected error */
      exit(EXIT_FAILURE);
    }

    if (poll_num > 0) {
      if (fds.revents & POLLIN)
        fan_event_handler(fan_fd);
    }
  }
}
void signal_handler(int sig) {

  switch (sig) {

  case SIGHUP:
    config_obj = load_config_file(CONFIG_FILE);
    //[TODO]: Add to the watch list
    //[TODO]: Modify load_config_file() func to filter the watchlist.
    printf("\n From SIGHUP\n");
    for (size_t i = 0; i < config_obj->watchlist_len; i++) {
      printf("[%ld]-Path: %s\n", i, (config_obj->watchlist[i].path));
    }
    config_obj_cleanup(config_obj);
    return;

  case SIGUSR1:

    if (fp_log != NULL)
      fclose(fp_log);

    if ((fp_log = fopen(LOG_FILE, "r")) != NULL) {

      if ((buffer = calloc(256, sizeof(char))) == NULL) {
        fclose(fp_log);
        perror("Fail to allocate memory");
        exit(EXIT_FAILURE);
      }

      while (fgets(buffer, sizeof(buffer), fp_log) != NULL) {
        fprintf(stdout, "%s", buffer);
      }

      fclose(fp_log);
      free(buffer);
      fp_log = fopen(LOG_FILE, "a");
    } else {
      fprintf(stderr, "No File have been modified\n");
    }
    return;
  }

  if (sig == SIGTERM || sig == SIGINT) {

    // necessary clean up then exit
    if (fp_log != NULL)
      fclose(fp_log);
    remove(LOCK_FILE);

    exit(EXIT_SUCCESS);
  }
}
