// test_gpu_scheduler.cpp
// Tests GpuScheduler: ASR priority, TTS contention, RAII guard.

#include <cassert>
#include <thread>

#include "mev/infra/gpu_scheduler.hpp"

using namespace mev;

static void test_asr_always_acquires() {
  GpuScheduler sched;
  assert(!sched.is_asr_active());

  sched.asr_acquire();
  assert(sched.is_asr_active());

  sched.asr_release();
  assert(!sched.is_asr_active());
}

static void test_tts_blocked_while_asr_active() {
  GpuScheduler sched;
  sched.asr_acquire();

  // TTS must not acquire while ASR is active.
  const bool tts_got_gpu = sched.tts_try_acquire();
  assert(!tts_got_gpu && "TTS must be blocked while ASR holds GPU");
  assert(sched.contention_events() == 1 && "contention counter must increment");

  sched.asr_release();
}

static void test_tts_acquires_when_asr_idle() {
  GpuScheduler sched;
  assert(!sched.is_asr_active());

  const bool ok = sched.tts_try_acquire();
  assert(ok && "TTS must acquire GPU when ASR is idle");
  assert(sched.is_tts_active());

  sched.tts_release();
  assert(!sched.is_tts_active());
}

static void test_multiple_asr_nested() {
  GpuScheduler sched;

  // Simulates two concurrent ASR inferences (future multi-ASR config).
  sched.asr_acquire();
  sched.asr_acquire();
  assert(sched.is_asr_active());

  sched.asr_release();
  assert(sched.is_asr_active() && "one ASR still running");

  sched.asr_release();
  assert(!sched.is_asr_active() && "all ASR released");
}

static void test_asr_guard_raii() {
  GpuScheduler sched;
  {
    AsrGpuGuard guard(sched);
    assert(sched.is_asr_active());

    // TTS blocked.
    assert(!sched.tts_try_acquire());
  }
  // Guard destructor released ASR.
  assert(!sched.is_asr_active());

  // TTS should now succeed.
  assert(sched.tts_try_acquire());
  sched.tts_release();
}

static void test_contention_counter_resets() {
  GpuScheduler sched;
  sched.asr_acquire();
  (void)sched.tts_try_acquire();  // increment
  (void)sched.tts_try_acquire();  // increment
  sched.asr_release();

  assert(sched.contention_events() == 2);
  sched.reset_contention_count();
  assert(sched.contention_events() == 0);
}

static void test_concurrent_asr_tts() {
  // Smoke test: ASR thread holds GPU while TTS thread tries to acquire.
  GpuScheduler sched;

  std::atomic<bool> done{false};
  std::atomic<int> tts_blocked{0};

  std::thread asr_thread([&] {
    AsrGpuGuard guard(sched);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    done.store(true);
  });

  std::thread tts_thread([&] {
    while (!done.load()) {
      if (!sched.tts_try_acquire()) {
        tts_blocked.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else {
        sched.tts_release();
      }
    }
  });

  asr_thread.join();
  tts_thread.join();

  assert(tts_blocked.load() > 0 && "TTS must have been blocked at least once");
  assert(!sched.is_asr_active());
}

int main() {
  test_asr_always_acquires();
  test_tts_blocked_while_asr_active();
  test_tts_acquires_when_asr_idle();
  test_multiple_asr_nested();
  test_asr_guard_raii();
  test_contention_counter_resets();
  test_concurrent_asr_tts();
  return 0;
}
