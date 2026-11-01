// The SDK's POSIX signal chain (patches/rexglue-sentry-signal-chain.patch): a fault
// a handler recovers resumes, and a fault no handler takes reaches the handler
// installed before (Crashpad's in the game) with its signal information and
// context, or the default action when there was none. The runtime installs its
// SEH handler between them (patches/rexglue-seh-signal-chain.patch): outside an
// SEH region it passes the fault on in the same way. Each case in a child process.
#include <rex/exception_handler.h>
#include <rex/platform/seh.h>
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

namespace {
bool Recover(rex::arch::Exception*, void*) { return true; }
bool Decline(rex::arch::Exception*, void*) { return false; }
void Previous(int number, siginfo_t* info, void* context) {
  _exit(info && context && info->si_signo == number ? 42 : 43);
}
// The cases: 0 recovered; 1 declined, to the prior handler; 2 declined, no prior
// handler; 3 and 4 as 1 and 2 with the runtime's SEH handler installed after the
// prior one and before the SDK's (the order of the game: Crashpad, the runtime's
// SEH, the SDK's handlers).
bool Run(int number, int kind) {
  const pid_t child = fork();
  if (child < 0) return false;
  if (!child) {
    alarm(3); // A swallowed fatal signal must fail promptly, never hang CI.
    struct sigaction prior {};
    prior.sa_sigaction = Previous;
    prior.sa_flags = SA_SIGINFO;
    sigemptyset(&prior.sa_mask);
    const bool with_prior = kind == 0 || kind == 1 || kind == 3;
    if (with_prior) sigaction(number, &prior, nullptr);
    else signal(number, SIG_DFL);
    if (kind >= 3) rex::platform::seh_initialize();
    rex::arch::ExceptionHandler::Install(kind == 0 ? Recover : Decline, nullptr);
    raise(number);
    _exit(0);
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child) return false;
  if (kind == 2 || kind == 4) return WIFSIGNALED(status) && WTERMSIG(status) == number;
  return WIFEXITED(status) && WEXITSTATUS(status) == (kind == 0 ? 0 : 42);
}
}  // namespace

int main() {
  for (const int signal : {SIGSEGV, SIGILL}) {
    for (int kind = 0; kind != 5; ++kind) {
      if (!Run(signal, kind)) {
        std::fprintf(stderr, "FAIL: signal %d, case %d\n", signal, kind);
        return 1;
      }
    }
  }
  std::puts("PASS: 10 cases; recovered faults, prior context through the SEH handler too, default fatal signals");
}
