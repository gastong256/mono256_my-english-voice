#include "mev/audio/portaudio_output.hpp"

#include <cstring>

#include "mev/audio/audio_types.hpp"
#include "mev/core/logger.hpp"

namespace mev {

PortAudioOutput::PortAudioOutput(std::uint32_t sample_rate, std::uint16_t channels,
                                 std::uint32_t frames_per_buffer, std::string device_name)
    : sample_rate_(sample_rate),
      frames_per_buffer_(frames_per_buffer),
      channels_(channels),
      device_name_(std::move(device_name)) {}

PortAudioOutput::~PortAudioOutput() { stop(); }

bool PortAudioOutput::start(AudioOutputCallback callback) {
#if defined(MEV_ENABLE_PORTAUDIO)
  callback_ = std::move(callback);

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    MEV_LOG_ERROR("PortAudioOutput: Pa_Initialize failed: ", Pa_GetErrorText(err));
    return false;
  }

  // Find device by name or use default.
  PaDeviceIndex device_index = Pa_GetDefaultOutputDevice();
  if (!device_name_.empty() && device_name_ != "virtual_mic" && device_name_ != "default") {
    const int num_devices = Pa_GetDeviceCount();
    for (int i = 0; i < num_devices; ++i) {
      const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
      if (info != nullptr && info->maxOutputChannels > 0 &&
          std::string(info->name).find(device_name_) != std::string::npos) {
        device_index = static_cast<PaDeviceIndex>(i);
        MEV_LOG_INFO("PortAudioOutput: found device '", info->name, "' at index ", i);
        break;
      }
    }
  }

  if (device_index == paNoDevice) {
    MEV_LOG_ERROR("PortAudioOutput: no suitable output device found");
    Pa_Terminate();
    return false;
  }

  const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(device_index);
  MEV_LOG_INFO("PortAudioOutput: opening device '",
               dev_info != nullptr ? dev_info->name : "?",
               "' sr=", sample_rate_, " ch=", channels_,
               " frames_per_buffer=", frames_per_buffer_);

  PaStreamParameters output_params{};
  output_params.device                    = device_index;
  output_params.channelCount              = static_cast<int>(channels_);
  output_params.sampleFormat              = paFloat32;
  output_params.suggestedLatency          = dev_info != nullptr
                                                ? dev_info->defaultLowOutputLatency
                                                : 0.02;
  output_params.hostApiSpecificStreamInfo = nullptr;

  err = Pa_OpenStream(&stream_, nullptr, &output_params,
                      static_cast<double>(sample_rate_),
                      static_cast<unsigned long>(frames_per_buffer_),
                      paClipOff, pa_output_callback, this);
  if (err != paNoError) {
    MEV_LOG_ERROR("PortAudioOutput: Pa_OpenStream failed: ", Pa_GetErrorText(err));
    Pa_Terminate();
    return false;
  }

  err = Pa_StartStream(stream_);
  if (err != paNoError) {
    MEV_LOG_ERROR("PortAudioOutput: Pa_StartStream failed: ", Pa_GetErrorText(err));
    Pa_CloseStream(stream_);
    stream_ = nullptr;
    Pa_Terminate();
    return false;
  }

  MEV_LOG_INFO("PortAudioOutput: stream started");
  return true;
#else
  (void)callback;
  MEV_LOG_WARN("PortAudioOutput: not compiled (MEV_ENABLE_PORTAUDIO=OFF), using stub");
  return false;
#endif
}

void PortAudioOutput::stop() {
#if defined(MEV_ENABLE_PORTAUDIO)
  if (stream_ != nullptr) {
    Pa_StopStream(stream_);
    Pa_CloseStream(stream_);
    stream_ = nullptr;
    Pa_Terminate();
    MEV_LOG_INFO("PortAudioOutput: stream stopped");
  }
#endif
}

std::string PortAudioOutput::name() const {
  return "portaudio:" + device_name_;
}

void PortAudioOutput::list_devices() {
#if defined(MEV_ENABLE_PORTAUDIO)
  if (Pa_Initialize() != paNoError) return;
  const int num_devices = Pa_GetDeviceCount();
  MEV_LOG_INFO("PortAudio output devices (", num_devices, " total):");
  for (int i = 0; i < num_devices; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (info != nullptr && info->maxOutputChannels > 0) {
      MEV_LOG_INFO("  [", i, "] ", info->name,
                   " ch=", info->maxOutputChannels,
                   " sr=", static_cast<int>(info->defaultSampleRate));
    }
  }
  Pa_Terminate();
#else
  MEV_LOG_WARN("PortAudioOutput::list_devices: not compiled (MEV_ENABLE_PORTAUDIO=OFF)");
#endif
}

#if defined(MEV_ENABLE_PORTAUDIO)
// RT callback — no allocations, no logging, no locks.
int PortAudioOutput::pa_output_callback(const void* /*input*/, void* output,
                                        unsigned long frame_count,
                                        const PaStreamCallbackTimeInfo* /*time_info*/,
                                        PaStreamCallbackFlags /*status_flags*/,
                                        void* userdata) {
  auto* self = static_cast<PortAudioOutput*>(userdata);
  auto* out  = static_cast<float*>(output);

  if (self->callback_) {
    self->callback_(out, static_cast<std::size_t>(frame_count),
                    self->channels_, self->sample_rate_);
  } else {
    // Silence if no callback yet.
    std::memset(out, 0, frame_count * self->channels_ * sizeof(float));
  }

  return paContinue;
}
#endif

}  // namespace mev
