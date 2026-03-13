#include "mev/audio/portaudio_input.hpp"

#include <cstring>

#include "mev/audio/audio_types.hpp"
#include "mev/core/logger.hpp"

namespace mev {

PortAudioInput::PortAudioInput(std::uint32_t sample_rate, std::uint16_t channels,
                               std::uint32_t frames_per_buffer, std::string device_name)
    : sample_rate_(sample_rate),
      frames_per_buffer_(frames_per_buffer),
      channels_(channels),
      device_name_(std::move(device_name)) {}

PortAudioInput::~PortAudioInput() { stop(); }

bool PortAudioInput::start(AudioInputCallback callback) {
#if defined(MEV_ENABLE_PORTAUDIO)
  callback_ = std::move(callback);

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    MEV_LOG_ERROR("PortAudioInput: Pa_Initialize failed: ", Pa_GetErrorText(err));
    return false;
  }

  // Find device by name or use default.
  PaDeviceIndex device_index = Pa_GetDefaultInputDevice();
  if (!device_name_.empty() && device_name_ != "default") {
    const int num_devices = Pa_GetDeviceCount();
    for (int i = 0; i < num_devices; ++i) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      if (info != nullptr && info->maxInputChannels > 0 &&
          std::string(info->name).find(device_name_) != std::string::npos) {
        device_index = static_cast<PaDeviceIndex>(i);
        MEV_LOG_INFO("PortAudioInput: found device '", info->name, "' at index ", i);
        break;
      }
    }
  }

  if (device_index == paNoDevice) {
    MEV_LOG_ERROR("PortAudioInput: no suitable input device found");
    Pa_Terminate();
    return false;
  }

  const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(device_index);
  MEV_LOG_INFO("PortAudioInput: opening device '",
               dev_info != nullptr ? dev_info->name : "?",
               "' sr=", sample_rate_, " ch=", channels_,
               " frames_per_buffer=", frames_per_buffer_);

  PaStreamParameters input_params{};
  input_params.device                    = device_index;
  input_params.channelCount              = static_cast<int>(channels_);
  input_params.sampleFormat              = paFloat32;
  input_params.suggestedLatency          = dev_info != nullptr
                                               ? dev_info->defaultLowInputLatency
                                               : 0.02;
  input_params.hostApiSpecificStreamInfo = nullptr;

  err = Pa_OpenStream(&stream_, &input_params, nullptr,
                      static_cast<double>(sample_rate_),
                      static_cast<unsigned long>(frames_per_buffer_),
                      paClipOff, pa_input_callback, this);
  if (err != paNoError) {
    MEV_LOG_ERROR("PortAudioInput: Pa_OpenStream failed: ", Pa_GetErrorText(err));
    Pa_Terminate();
    return false;
  }

  err = Pa_StartStream(stream_);
  if (err != paNoError) {
    MEV_LOG_ERROR("PortAudioInput: Pa_StartStream failed: ", Pa_GetErrorText(err));
    Pa_CloseStream(stream_);
    stream_ = nullptr;
    Pa_Terminate();
    return false;
  }

  MEV_LOG_INFO("PortAudioInput: stream started");
  return true;
#else
  (void)callback;
  MEV_LOG_WARN("PortAudioInput: not compiled (MEV_ENABLE_PORTAUDIO=OFF), using stub");
  return false;
#endif
}

void PortAudioInput::stop() {
#if defined(MEV_ENABLE_PORTAUDIO)
  if (stream_ != nullptr) {
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_ = nullptr;
    Pa_Terminate();
    MEV_LOG_INFO("PortAudioInput: stream stopped");
  }
#endif
}

std::string PortAudioInput::name() const {
  return "portaudio:" + device_name_;
}

void PortAudioInput::list_devices() {
#if defined(MEV_ENABLE_PORTAUDIO)
  if (Pa_Initialize() != paNoError) return;
  const int num_devices = Pa_GetDeviceCount();
  MEV_LOG_INFO("PortAudio input devices (", num_devices, " total):");
  for (int i = 0; i < num_devices; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (info != nullptr && info->maxInputChannels > 0) {
      MEV_LOG_INFO("  [", i, "] ", info->name,
                   " ch=", info->maxInputChannels,
                   " sr=", static_cast<int>(info->defaultSampleRate));
    }
  }
  Pa_Terminate();
#else
  MEV_LOG_WARN("PortAudioInput::list_devices: not compiled (MEV_ENABLE_PORTAUDIO=OFF)");
#endif
}

#if defined(MEV_ENABLE_PORTAUDIO)
// RT callback — no allocations, no logging, no locks.
int PortAudioInput::pa_input_callback(const void* input, void* /*output*/,
                                      unsigned long frame_count,
                                      const PaStreamCallbackTimeInfo* /*time_info*/,
                                      PaStreamCallbackFlags /*status_flags*/,
                                      void* userdata) {
  auto* self = static_cast<PortAudioInput*>(userdata);

  if (frame_count > kMaxFramesPerBlock) {
    self->overrun_count_.fetch_add(1, std::memory_order_relaxed);
    return paContinue;
  }

  if (self->callback_ && input != nullptr) {
    const auto* pcm = static_cast<const float*>(input);
    self->callback_(pcm, static_cast<std::size_t>(frame_count),
                    self->channels_, self->sample_rate_);
  }

  return paContinue;
}
#endif

}  // namespace mev
