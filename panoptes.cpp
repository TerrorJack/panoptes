#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void log_json_str(FILE *fp, const std::string &str) {
  fputc('"', fp);
  for (const char c : str) {
    switch (c) {
    case '\"': {
      fputs("\\\"", fp);
      break;
    }
    case '\\': {
      fputs("\\\\", fp);
      break;
    }
    case '\n': {
      fputs("\\n", fp);
      break;
    }
    case '\r': {
      fputs("\\r", fp);
      break;
    }
    case '\t': {
      fputs("\\t", fp);
      break;
    }
    default: {
      fputc(c, fp);
      break;
    }
    }
  }
  fputc('"', fp);
}

static void log_json_strs(FILE *fp, const std::vector<std::string> &strs) {
  if (strs.empty()) {
    fputs("[]", fp);
    return;
  }

  fputc('[', fp);
  log_json_str(fp, strs[0]);
  for (int i = 1; i < strs.size(); ++i) {
    fputc(',', fp);
    log_json_str(fp, strs[i]);
  }
  fputc(']', fp);
}

static void lex_rsp(std::vector<std::string> &result, const std::string &rsp) {
  std::string token;
  bool in_quotes = false;

  for (int i = 0;;) {
    // Ensure at least 1 char left
    if (i >= rsp.size()) {
      break;
    }

    // Change lexer state, no new char
    if (rsp[i] == '"') {
      if (in_quotes) {
        result.push_back(token);
        token.clear();
      }
      in_quotes = !in_quotes;
      ++i;
      continue;
    }

    // Assume \" can only occur within quotes
    if (i + 1 < rsp.size() && rsp[i] == '\\' && rsp[i + 1] == '"') {
      token.push_back('"');
      i += 2;
      continue;
    }

    // Whitespace not in quotes is delimiter
    if (isspace(rsp[i]) && !in_quotes) {
      if (!token.empty()) {
        result.push_back(token);
        token.clear();
      }
      ++i;
      continue;
    }

    // Regular char
    token.push_back(rsp[i]);
    ++i;
  }

  // Handle last token
  if (!token.empty()) {
    result.push_back(token);
  }
}

static void expand_argv(std::vector<std::string> &result,
                        const std::string &argv) {
  if (!argv.empty() && argv[0] == '@') {
    std::ifstream f(argv.substr(1));
    std::stringstream buf;
    buf << f.rdbuf();
    lex_rsp(result, buf.str());
    return;
  }
  result.push_back(argv);
}

static void log_exec_record(FILE *fp, const char *file, char *const argv[],
                            const char *cwd) {
  fputc('{', fp);
  log_json_str(fp, "arguments");
  fputc(':', fp);

  std::vector<std::string> args{file};
  for (int i = 1; argv[i] != nullptr; ++i) {
    expand_argv(args, argv[i]);
  }

  log_json_strs(fp, args);

  fputc(',', fp);
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
  const static auto orig_execvp =
      reinterpret_cast<ty_execvp>(dlsym(RTLD_NEXT, "execvp"));
  return orig_execvp(file, argv);
}

extern "C" int execv(const char *path, char *const argv[]) {
  log_exec(path, argv);
  using ty_execv = int (*)(const char *path, char *const argv[]);
  const static auto orig_execv =
      reinterpret_cast<ty_execv>(dlsym(RTLD_NEXT, "execv"));
  return orig_execv(path, argv);
}

extern "C" int execvpe(const char *file, char *const argv[],
                       char *const envp[]) {
  log_exec(file, argv);
  using ty_execvpe =
      int (*)(const char *file, char *const argv[], char *const envp[]);
  const static auto orig_execvpe =
      reinterpret_cast<ty_execvpe>(dlsym(RTLD_NEXT, "execvpe"));
  return orig_execvpe(file, argv, envp);
}

extern "C" int execve(const char *filename, char *const argv[],
                      char *const envp[]) {
  log_exec(filename, argv);
  using ty_execve =
      int (*)(const char *filename, char *const argv[], char *const envp[]);
  const static auto orig_execve =
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
  const static auto orig_posix_spawn =
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
  const static auto orig_posix_spawnp =
      reinterpret_cast<ty_posix_spawnp>(dlsym(RTLD_NEXT, "posix_spawnp"));
  return orig_posix_spawnp(pid, file, file_actions, attrp, argv, envp);
}
