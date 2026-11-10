#import <Foundation/Foundation.h>

#include "metal_archiver_mac.h"

#include <dirent.h>
#include <fcntl.h>
#include <libproc.h>
#include <spawn.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern char** environ;

namespace {
constexpr const char* kReap = "--metal-archiver-reap";
// Intentionally retained until kernel process teardown, after every Metal user
// and static destructor. CLOEXEC prevents compiler subprocesses retaining them.
int session_lock = -1;
int lifetime_pipe = -1;

class File {
 public:
  explicit File(int fd = -1) : fd_(fd) {}
  ~File() { if (fd_ >= 0) close(fd_); }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  int get() const { return fd_; }
  int release() { int fd = fd_; fd_ = -1; return fd; }
 private:
  int fd_;
};

int GuardFailure() {
  std::fprintf(stderr, "gtaiv: Cannot secure the app's Metal cache lifecycle lock. Check the macOS user cache folder permissions and symlinks.\n");
  return 1;
}

bool Owned(int fd, bool directory) {
  struct stat info{};
  return fd >= 0 && fstat(fd, &info) == 0 && info.st_uid == getuid() &&
    !(info.st_mode & 0022) && (directory ? S_ISDIR(info.st_mode) :
    S_ISREG(info.st_mode) && info.st_nlink == 1);
}

// Descriptor-relative traversal never follows symlinks, including intermediate
// directories. Unlinking a nested symlink removes only that link, not its target.
bool RemoveContents(int fd, dev_t device) {
  File copy(dup(fd));
  DIR* entries = fdopendir(copy.get());
  if (!entries) return false;
  copy.release();  // closedir owns the duplicate.
  bool ok = true;
  errno = 0;
  while (dirent* entry = readdir(entries)) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    struct stat info{};
    if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
    if (info.st_uid != getuid() || info.st_dev != device) { ok = false; break; }
    if (S_ISDIR(info.st_mode)) {
      File child(openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      struct stat opened{};
      if (child.get() < 0 || fstat(child.get(), &opened) != 0 || opened.st_ino != info.st_ino ||
          opened.st_dev != info.st_dev || !RemoveContents(child.get(), device) ||
          unlinkat(fd, entry->d_name, AT_REMOVEDIR) != 0) { ok = false; break; }
    } else if (unlinkat(fd, entry->d_name, 0) != 0) { ok = false; break; }
    errno = 0;
  }
  if (errno) ok = false;
  closedir(entries);
  return ok;
}

bool IsReaper(pid_t pid) {
  int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
  size_t size = 0;
  if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0) return false;
  std::vector<char> args(size);
  if (sysctl(mib, 3, args.data(), &size, nullptr, 0) != 0 || size <= sizeof(int)) return false;
  size_t offset = sizeof(int);
  auto skip = [&] { while (offset < size && args[offset]) ++offset; };
  skip();  // executable path
  while (offset < size && !args[offset]) ++offset;
  skip();  // argv[0]
  if (offset < size) ++offset;
  return offset < size && strnlen(args.data() + offset, size - offset) == strlen(kReap) &&
    !memcmp(args.data() + offset, kReap, strlen(kReap));
}

// Older app builds do not hold our lock. Conservatively defer when another
// executable from the same bundle identity is alive. New builds additionally
// use flock to close the scan/start race, regardless of selected shader root.
bool OtherAppProcess(NSString* identity) {
  int bytes = proc_listpids(PROC_UID_ONLY, getuid(), nullptr, 0);
  if (bytes <= 0) return true;
  std::vector<pid_t> pids(bytes / sizeof(pid_t) + 256);
  bytes = proc_listpids(PROC_UID_ONLY, getuid(), pids.data(), int(pids.size() * sizeof(pid_t)));
  if (bytes <= 0 || bytes >= int(pids.size() * sizeof(pid_t))) return true;
  for (int i = 0; i < bytes / int(sizeof(pid_t)); ++i) {
    pid_t pid = pids[i];
    if (pid <= 0 || pid == getpid()) continue;
    char path[PROC_PIDPATHINFO_MAXSIZE]{};
    if (proc_pidpath(pid, path, sizeof(path)) <= 0) {
      // A process disappearing during the scan is normal. Other errors fail closed.
      if (errno != ESRCH && errno != ENOENT) return true;
      continue;
    }
    NSString* executable = @(path);
    NSString* macos = executable.stringByDeletingLastPathComponent;
    if (![macos.lastPathComponent isEqualToString:@"MacOS"]) continue;
    NSString* contents = macos.stringByDeletingLastPathComponent;
    NSDictionary* plist = [NSDictionary dictionaryWithContentsOfFile:
      [contents stringByAppendingPathComponent:@"Info.plist"]];
    if ([plist[@"CFBundleIdentifier"] isEqual:identity] && !IsReaper(pid)) return true;
  }
  return false;
}

void Clean(int app, NSString* identity) {
  if (OtherAppProcess(identity)) return;
  File archive(openat(app, "com.apple.gpuarchiver", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (archive.get() < 0) {
    if (errno != ENOENT) std::fprintf(stderr, "gtaiv: GPU archiver cleanup skipped: %s\n", strerror(errno));
    return;
  }
  struct stat info{};
  if (!Owned(archive.get(), true) || fstat(archive.get(), &info) != 0 ||
      !RemoveContents(archive.get(), info.st_dev))
    std::fprintf(stderr, "gtaiv: GPU archiver cleanup incomplete; retained files will be retried when idle.\n");
}

int StartReaper() {
  int ends[2];
  if (pipe(ends) != 0) return errno;
  File read_end(ends[0]), write_end(ends[1]);
  if (fcntl(read_end.get(), F_SETFD, FD_CLOEXEC) != 0 ||
      fcntl(write_end.get(), F_SETFD, FD_CLOEXEC) != 0) return errno;
  std::string executable = NSBundle.mainBundle.executablePath.fileSystemRepresentation;
  // A pipe handshake is immune to PID reuse and also handles SIGKILL/crashes.
  // The child has no Metal users and waits for kernel closure of the parent's end.
  std::string parent = std::to_string(getpid());
  char* args[] = {executable.data(), const_cast<char*>(kReap), parent.data(), nullptr};
  // Injected diagnostics belong to the game process. A second probe would
  // truncate the same output files and sample the helper instead of the game.
  std::vector<char*> environment;
  for (char** entry = environ; *entry; ++entry) {
    if (!std::strncmp(*entry, "DYLD_INSERT_LIBRARIES=", 22) ||
        !std::strncmp(*entry, "LIBERTY_MEASURE_", 16)) continue;
    environment.push_back(*entry);
  }
  environment.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (error) return error;
  error = posix_spawn_file_actions_adddup2(&actions, read_end.get(), 3);
  for (int fd = 0; fd < 3 && !error; ++fd)
    error = posix_spawn_file_actions_addopen(&actions, fd, "/dev/null", O_RDWR, 0);
  pid_t child = 0;
  if (!error) error = posix_spawn(&child, executable.c_str(), &actions, nullptr, args, environment.data());
  posix_spawn_file_actions_destroy(&actions);
  if (!error) lifetime_pipe = write_end.release();
  return error;
}
}  // namespace

int GtaivMetalArchiverSession(int argc, char** argv) {
  @autoreleasepool {
    const bool reaper = argc >= 2 && !strcmp(argv[1], kReap);
    if (reaper) {
      if (argc != 3) return 1;
      char* end = nullptr;
      long parent = std::strtol(argv[2], &end, 10);
      if (!end || *end || parent <= 1 || parent != getppid()) return 0;
      File events(kqueue());
      if (events.get() < 0) return 1;
      struct kevent change{}, event{};
      EV_SET(&change, parent, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
      if (kevent(events.get(), &change, 1, nullptr, 0, nullptr) != 0) return 1;
      // EOF closes the lifetime handshake; NOTE_EXIT additionally waits for
      // kernel process teardown, rather than relying on descriptor-close order.
      char byte;
      ssize_t count;
      do { count = read(3, &byte, 1); } while (count < 0 && errno == EINTR);
      close(3);
      if (count != 0) return 1;
      int result;
      do { result = kevent(events.get(), nullptr, 0, &event, 1, nullptr); }
      while (result < 0 && errno == EINTR);
      if (result != 1 || !(event.fflags & NOTE_EXIT) || (event.flags & EV_ERROR)) return 1;
    }
    NSString* identity = NSBundle.mainBundle.bundleIdentifier;
    // Plain command-line tools use the generic Apple cache, which is never ours.
    if (!identity.length || ![NSBundle.mainBundle.bundlePath hasSuffix:@".app"])
      return reaper ? 0 : -1;
    NSCharacterSet* allowed = [NSCharacterSet characterSetWithCharactersInString:
      @"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_"];
    if ([identity rangeOfCharacterFromSet:allowed.invertedSet].location != NSNotFound ||
        [identity isEqual:@"."] || [identity isEqual:@".."]) return GuardFailure();
    size_t size = confstr(_CS_DARWIN_USER_CACHE_DIR, nullptr, 0);
    std::vector<char> cache(size);
    if (!size || confstr(_CS_DARWIN_USER_CACHE_DIR, cache.data(), size) != size) return GuardFailure();
    File root(open(cache.data(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!Owned(root.get(), true)) return GuardFailure();
    if (mkdirat(root.get(), identity.UTF8String, 0700) != 0 && errno != EEXIST) return GuardFailure();
    File app(openat(root.get(), identity.UTF8String, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!Owned(app.get(), true)) return GuardFailure();
    File lock(openat(app.get(), "liberty-metal-archiver.lock", O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (!Owned(lock.get(), false)) return GuardFailure();
    if (flock(lock.get(), LOCK_EX | LOCK_NB) == 0) {
      Clean(app.get(), identity);
      if (reaper) return 0;
    } else if (reaper) return 0;
    // LOCK_SH waits for an in-progress cleanup before any Metal initialization.
    if (flock(lock.get(), LOCK_SH) != 0) return GuardFailure();
    session_lock = lock.release();
    if (int error = StartReaper())
      std::fprintf(stderr, "gtaiv: GPU archiver exit cleanup unavailable: %s\n", strerror(error));
    return -1;
  }
}
