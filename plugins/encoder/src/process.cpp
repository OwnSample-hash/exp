#include <process.hpp>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace enc {

namespace {
void closeFd(int &fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}
} // namespace

std::string which(const std::string &exe) {
  if (exe.empty())
    return "";
  // An explicit path is used verbatim when it exists and is executable.
  if (exe.find('/') != std::string::npos)
    return (::access(exe.c_str(), X_OK) == 0) ? exe : "";

  const char *path = std::getenv("PATH");
  if (!path)
    path = "/usr/bin:/bin";
  std::string dirs(path);
  size_t start = 0;
  while (start <= dirs.size()) {
    size_t end = dirs.find(':', start);
    if (end == std::string::npos)
      end = dirs.size();
    std::string dir = dirs.substr(start, end - start);
    if (!dir.empty()) {
      std::filesystem::path candidate = std::filesystem::path(dir) / exe;
      if (::access(candidate.c_str(), X_OK) == 0)
        return candidate.string();
    }
    start = end + 1;
  }
  return "";
}

ProcResult runProcess(const std::vector<std::string> &argv, const std::vector<uint8_t> &input) {
  ProcResult result;
  if (argv.empty())
    return result;

  int inPipe[2] = {-1, -1};
  int outPipe[2] = {-1, -1};
  int errPipe[2] = {-1, -1};
  if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0 || ::pipe(errPipe) != 0) {
    result.err = std::string("pipe() failed: ") + std::strerror(errno);
    for (int *p : {inPipe, outPipe, errPipe}) {
      closeFd(p[0]);
      closeFd(p[1]);
    }
    return result;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    result.err = std::string("fork() failed: ") + std::strerror(errno);
    for (int *p : {inPipe, outPipe, errPipe}) {
      closeFd(p[0]);
      closeFd(p[1]);
    }
    return result;
  }

  if (pid == 0) {
    // Child: wire up std streams, then exec.
    ::dup2(inPipe[0], STDIN_FILENO);
    ::dup2(outPipe[1], STDOUT_FILENO);
    ::dup2(errPipe[1], STDERR_FILENO);
    for (int *p : {inPipe, outPipe, errPipe}) {
      ::close(p[0]);
      ::close(p[1]);
    }
    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto &a : argv)
      if (!a.empty())
        cargv.push_back(const_cast<char *>(a.c_str()));
    cargv.push_back(nullptr);
    ::execvp(cargv[0], cargv.data());
    // execvp only returns on failure.
    _exit(127);
  }

  // Parent.
  closeFd(inPipe[0]);
  closeFd(outPipe[1]);
  closeFd(errPipe[1]);
  result.spawned = true;

  size_t written = 0;
  if (input.empty())
    closeFd(inPipe[1]);

  while (true) {
    struct pollfd fds[3];
    int n = 0;
    const int inIdx = (inPipe[1] >= 0) ? n : -1;
    if (inPipe[1] >= 0)
      fds[n++] = {inPipe[1], POLLOUT, 0};
    const int outIdx = (outPipe[0] >= 0) ? n : -1;
    if (outPipe[0] >= 0)
      fds[n++] = {outPipe[0], POLLIN, 0};
    const int errIdx = (errPipe[0] >= 0) ? n : -1;
    if (errPipe[0] >= 0)
      fds[n++] = {errPipe[0], POLLIN, 0};
    if (n == 0)
      break;

    if (::poll(fds, n, -1) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (inIdx >= 0 && (fds[inIdx].revents & (POLLOUT | POLLERR | POLLHUP))) {
      const size_t chunk = input.size() - written;
      ssize_t w = (chunk > 0) ? ::write(inPipe[1], input.data() + written, chunk) : 0;
      if (w > 0)
        written += static_cast<size_t>(w);
      if (w < 0 || written >= input.size())
        closeFd(inPipe[1]);
    }
    char buf[4096];
    if (outIdx >= 0 && (fds[outIdx].revents & (POLLIN | POLLERR | POLLHUP))) {
      ssize_t r = ::read(outPipe[0], buf, sizeof(buf));
      if (r > 0)
        result.out.insert(result.out.end(), buf, buf + r);
      else
        closeFd(outPipe[0]);
    }
    if (errIdx >= 0 && (fds[errIdx].revents & (POLLIN | POLLERR | POLLHUP))) {
      ssize_t r = ::read(errPipe[0], buf, sizeof(buf));
      if (r > 0)
        result.err.append(buf, static_cast<size_t>(r));
      else
        closeFd(errPipe[0]);
    }
  }

  closeFd(inPipe[1]);
  closeFd(outPipe[0]);
  closeFd(errPipe[0]);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (WIFEXITED(status))
    result.exitCode = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    result.exitCode = -WTERMSIG(status);
  return result;
}

} // namespace enc

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
