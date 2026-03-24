#pragma once

#include <string>
#include <vector>

#include "mev/tts/tts_config.hpp"
#include "mev/tts/tts_types.hpp"

// NEXT STEP: XTTSv2Engine cuando exista export ONNX estable.

namespace mev {

// ---------------------------------------------------------------------------
// ITTSEngine — interface for all TTS backend implementations.
//
// Concrete implementations:
//   - StubTTSEngine   : generates silence or sine wave (testing)
//   - PiperTTSEngine  : Piper VITS via ONNX Runtime (~50-150ms GPU)
//   - EspeakTTSEngine : eSpeak-ng fallback (~5ms, robotic quality)
//
// Lifecycle: initialize() → warmup() → synthesize()... → shutdown()
// ---------------------------------------------------------------------------
class ITTSEngine {
 public:
  virtual ~ITTSEngine() = default;

  // Load model, validate paths, set up execution provider.
  // Returns true on success; error string is set on failure.
  virtual bool initialize(const TTSConfig& config, std::string& error) = 0;

  // Run a dummy inference to trigger CUDA kernel compilation / weight loading.
  // Called once during pipeline startup before any real audio is processed.
  virtual void warmup() = 0;

  // Synthesize `text` to PCM float32 samples at output_sample_rate() Hz, mono.
  // Returns true on success; pcm_out is filled with synthesized samples.
  // Returns false if synthesis fails; pcm_out may be empty or partial.
  [[nodiscard]] virtual bool synthesize(const std::string& text, std::vector<float>& pcm_out) = 0;

  // Low-latency clause/partial synthesis entrypoint. Implementations may vary
  // voice speed or execution policy for partial chunks, but they must not
  // rewrite already-emitted audio.
  [[nodiscard]] virtual bool synthesize_chunk(const SpeechChunk& chunk,
                                              std::vector<float>& pcm_out) = 0;

  // Sample rate of the PCM produced by synthesize().
  [[nodiscard]] virtual int output_sample_rate() const = 0;

  // Human-readable engine name for logging.
  [[nodiscard]] virtual std::string engine_name() const = 0;

  // Runtime placement and provider status for self-tests and diagnostics.
  [[nodiscard]] virtual bool gpu_requested() const = 0;
  [[nodiscard]] virtual bool using_gpu() const = 0;
  [[nodiscard]] virtual std::string runtime_summary() const = 0;

  // Release model handles and GPU memory.
  virtual void shutdown() = 0;
};

}  // namespace mev
