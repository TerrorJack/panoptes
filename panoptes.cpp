#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// Take a valid directory sans trailing slash, return a git repo path
// or the original argument if repo not found
static std::string find_repo(const std::string &path) {
  auto wip = path;
  while (true) {
    auto git_path = wip + "/.git";
    struct stat git_path_info;
    if (stat(git_path.c_str(), &git_path_info) == 0 &&
        git_path_info.st_mode & S_IFDIR) {
      return wip;
    }
    auto last_slash_pos = wip.find_last_of('/');
    if (last_slash_pos == 0) {
      return path;
    }
    wip.resize(last_slash_pos);
  }
}

static std::string my_getcwd() {
  auto buf = get_current_dir_name();
  auto cwd = std::string(buf);
  free(buf);
  return cwd;
}

static std::string events_path() {
  auto events_path_buf = getenv("EVENTS_PATH");
  if (events_path_buf != nullptr) {
    return events_path_buf;
  }
  return find_repo(my_getcwd()) + "/events.jsonl";
}

static void log_json_str(FILE *fp, const std::string &str) {
  fputc('"', fp);
  for (auto c : str) {
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
  for (auto i = 1; i < strs.size(); ++i) {
    fputc(',', fp);
    log_json_str(fp, strs[i]);
  }
  fputc(']', fp);
}

static void lex_rsp(std::vector<std::string> &result, const std::string &rsp) {
  auto token = std::string();
  auto in_quotes = false;

  for (auto i = 0;;) {
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
    auto f = std::ifstream(argv.substr(1));
    auto buf = std::stringstream();
    buf << f.rdbuf();
    lex_rsp(result, buf.str());
    return;
  }
  result.push_back(argv);
}

static void log_exec_record(FILE *fp, const std::string &file,
                            char *const argv[], const std::string &cwd) {
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

static void log_exec(const std::string &file, char *const argv[]) {
  // Since we need to work with flock(), we need to work with raw file
  // descriptors, so we need to stick to C file api instead of C++
  // fstream.
  auto static log_mutex = std::mutex();
  auto log_guard = std::lock_guard(log_mutex);
  auto fd = open(events_path().c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
  flock(fd, LOCK_EX);
  auto fp = fdopen(fd, "a");
  log_exec_record(fp, file, argv, my_getcwd());
  fclose(fp);
}

extern "C" int execvp(const char *file, char *const argv[]) {
  log_exec(file, argv);
  auto orig_execvp =
      reinterpret_cast<decltype(&execvp)>(dlsym(RTLD_NEXT, "execvp"));
  return orig_execvp(file, argv);
}

extern "C" int execv(const char *path, char *const argv[]) {
  log_exec(path, argv);
  auto orig_execv =
      reinterpret_cast<decltype(&execv)>(dlsym(RTLD_NEXT, "execv"));
  return orig_execv(path, argv);
}

extern "C" int execvpe(const char *file, char *const argv[],
                       char *const envp[]) {
  log_exec(file, argv);
  auto orig_execvpe =
      reinterpret_cast<decltype(&execvpe)>(dlsym(RTLD_NEXT, "execvpe"));
  return orig_execvpe(file, argv, envp);
}

extern "C" int execve(const char *filename, char *const argv[],
                      char *const envp[]) {
  log_exec(filename, argv);
  auto orig_execve =
      reinterpret_cast<decltype(&execve)>(dlsym(RTLD_NEXT, "execve"));
  return orig_execve(filename, argv, envp);
}

extern "C" int posix_spawn(pid_t *pid, const char *path,
                           const posix_spawn_file_actions_t *file_actions,
                           const posix_spawnattr_t *attrp, char *const argv[],
                           char *const envp[]) {
  log_exec(path, argv);
  // Make this static. posix_spawn may be called multiple times in the
  // same process, this should avoid the overhead of multiple dlsym
  // calls
  auto static orig_posix_spawn =
      reinterpret_cast<decltype(&posix_spawn)>(dlsym(RTLD_NEXT, "posix_spawn"));
  return orig_posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

extern "C" int posix_spawnp(pid_t *pid, const char *file,
                            const posix_spawn_file_actions_t *file_actions,
                            const posix_spawnattr_t *attrp, char *const argv[],
                            char *const envp[]) {
  log_exec(file, argv);
  auto static orig_posix_spawnp = reinterpret_cast<decltype(&posix_spawnp)>(
      dlsym(RTLD_NEXT, "posix_spawnp"));
  return orig_posix_spawnp(pid, file, file_actions, attrp, argv, envp);
}
