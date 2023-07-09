#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

static void log_json_str(FILE *fp, const char *str) {
  fputc('"', fp);
  for (; *str != '\0'; ++str) {
    switch (*str) {
    case '\"': {
      fputs("\\\"", fp);
      break;
    }
    case '\\': {
      fputs("\\\\", fp);
      break;
    }
    default: {
      fputc(*str, fp);
      break;
    }
    }
  }
  fputc('"', fp);
}

static void log_exec_record(FILE *fp, const char *file, char *const argv[],
                            const char *cwd) {
  fputc('{', fp);
  log_json_str(fp, "arguments");
  fputs(":[", fp);
  log_json_str(fp, file);
  for (int i = 1; argv[i] != nullptr; ++i) {
    fputc(',', fp);
    log_json_str(fp, argv[i]);
  }
  fputs("],", fp);
  log_json_str(fp, "directory");
  fputc(':', fp);
  log_json_str(fp, cwd);
  fputs("}\n", fp);
}

static void log_exec(const char *file, char *const argv[]) {
  int fd = open("/tmp/log.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
  flock(fd, LOCK_EX);
  FILE *fp = fdopen(fd, "a");
  char *cwd = get_current_dir_name();
  log_exec_record(fp, file, argv, cwd);
  free(cwd);
  fclose(fp);
}

extern "C" int execvp(const char *file, char *const argv[]) {
  log_exec(file, argv);
  using ty_execvp = int (*)(const char *file, char *const argv[]);
  const auto orig_execvp =
      reinterpret_cast<ty_execvp>(dlsym(RTLD_NEXT, "execvp"));
  return orig_execvp(file, argv);
}

extern "C" int execv(const char *path, char *const argv[]) {
  log_exec(path, argv);
  using ty_execv = int (*)(const char *path, char *const argv[]);
  const auto orig_execv = reinterpret_cast<ty_execv>(dlsym(RTLD_NEXT, "execv"));
  return orig_execv(path, argv);
}

extern "C" int execvpe(const char *file, char *const argv[],
                       char *const envp[]) {
  log_exec(file, argv);
  using ty_execvpe =
      int (*)(const char *file, char *const argv[], char *const envp[]);
  const auto orig_execvpe =
      reinterpret_cast<ty_execvpe>(dlsym(RTLD_NEXT, "execvpe"));
  return orig_execvpe(file, argv, envp);
}

extern "C" int execve(const char *filename, char *const argv[],
                      char *const envp[]) {
  log_exec(filename, argv);
  using ty_execve =
      int (*)(const char *filename, char *const argv[], char *const envp[]);
  const auto orig_execve =
      reinterpret_cast<ty_execve>(dlsym(RTLD_NEXT, "execve"));
  return orig_execve(filename, argv, envp);
}

extern "C" int posix_spawn(pid_t *pid, const char *path,
                           const posix_spawn_file_actions_t *file_actions,
                           const posix_spawnattr_t *attrp, char *const argv[],
                           char *const envp[]) {
  log_exec(path, argv);
  using ty_posix_spawn = int (*)(pid_t *pid, const char *path,
                                 const posix_spawn_file_actions_t *file_actions,
                                 const posix_spawnattr_t *attrp,
                                 char *const argv[], char *const envp[]);
  const auto orig_posix_spawn =
      reinterpret_cast<ty_posix_spawn>(dlsym(RTLD_NEXT, "posix_spawn"));
  return orig_posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

extern "C" int posix_spawnp(pid_t *pid, const char *file,
                            const posix_spawn_file_actions_t *file_actions,
                            const posix_spawnattr_t *attrp, char *const argv[],
                            char *const envp[]) {
  log_exec(file, argv);
  using ty_posix_spawnp = int (*)(
      pid_t *pid, const char *file,
      const posix_spawn_file_actions_t *file_actions,
      const posix_spawnattr_t *attrp, char *const argv[], char *const envp[]);
  const auto orig_posix_spawnp =
      reinterpret_cast<ty_posix_spawnp>(dlsym(RTLD_NEXT, "posix_spawnp"));
  return orig_posix_spawnp(pid, file, file_actions, attrp, argv, envp);
}
