#include <cassert>
#include <iostream>

#if defined(MEV_ENABLE_PORTAUDIO)
#include <portaudio.h>
#endif

int main() {
#if !defined(MEV_ENABLE_PORTAUDIO)
  std::cout << "[SKIP] test_portaudio_enum: MEV_ENABLE_PORTAUDIO=OFF\n";
  return 0;
#else
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "[FAIL] Pa_Initialize failed: " << Pa_GetErrorText(err) << "\n";
    return 1;
  }

  const int count = Pa_GetDeviceCount();
  // In a CI/headless environment there may be 0 devices; just verify no crash.
  std::cout << "[INFO] PortAudio device count: " << count << "\n";

  for (int i = 0; i < count; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (info != nullptr) {
      std::cout << "  [" << i << "] " << info->name
                << " in=" << info->maxInputChannels
                << " out=" << info->maxOutputChannels << "\n";
    }
  }

  Pa_Terminate();
  std::cout << "[PASS] test_portaudio_enum\n";
  return 0;
#endif
}
