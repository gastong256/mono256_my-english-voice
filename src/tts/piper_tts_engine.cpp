#include "mev/tts/piper_tts_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <utility>

#include "mev/core/logger.hpp"

#if defined(MEV_ENABLE_ESPEAK)
#include <espeak-ng/speak_lib.h>
#endif

namespace mev {

namespace {

std::optional<std::string> read_text_file(const std::string& path, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "PiperTTSEngine: unable to read '" + path + "'";
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string unescape_json_string(std::string_view escaped) {
  std::string out;
  out.reserve(escaped.size());

  for (std::size_t i = 0; i < escaped.size(); ++i) {
    const char ch = escaped[i];
    if (ch != '\\' || (i + 1U) >= escaped.size()) {
      out.push_back(ch);
      continue;
    }

    const char next = escaped[++i];
    switch (next) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      default:
        out.push_back(next);
        break;
    }
  }

  return out;
}

bool extract_json_object(const std::string& json, const std::string& key, std::string& object_body) {
  const std::string needle = "\"" + key + "\"";
  const std::size_t key_pos = json.find(needle);
  if (key_pos == std::string::npos) {
    return false;
  }

  std::size_t brace_pos = json.find('{', key_pos + needle.size());
  if (brace_pos == std::string::npos) {
    return false;
  }

  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = brace_pos; i < json.size(); ++i) {
    const char ch = json[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
      continue;
    }

    if (ch == '{') {
      ++depth;
      continue;
    }

    if (ch == '}') {
      --depth;
      if (depth == 0) {
        object_body = json.substr(brace_pos + 1U, i - brace_pos - 1U);
        return true;
      }
    }
  }

  return false;
}

std::optional<std::string> extract_json_string(const std::string& json, const std::string& key) {
  const std::regex rx("\"" + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
  std::smatch match;
  if (!std::regex_search(json, match, rx)) {
    return std::nullopt;
  }

  return unescape_json_string(match[1].str());
}

std::optional<double> extract_json_number(const std::string& json, const std::string& key) {
  const std::regex rx("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
  std::smatch match;
  if (!std::regex_search(json, match, rx)) {
    return std::nullopt;
  }

  try {
    return std::stod(match[1].str());
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<std::int64_t> parse_int_array(std::string_view values) {
  std::vector<std::int64_t> out;
  const std::string materialized{values};
  const std::regex number_rx("-?[0-9]+");
  for (std::sregex_iterator it(materialized.begin(), materialized.end(), number_rx), end;
       it != end; ++it) {
    out.push_back(std::stoll((*it)[0].str()));
  }
  return out;
}

bool next_utf8_codepoint(const std::string& input, std::size_t& offset, char32_t& codepoint) {
  if (offset >= input.size()) {
    return false;
  }

  const unsigned char lead = static_cast<unsigned char>(input[offset]);
  std::size_t extra_bytes = 0;
  char32_t value = 0;

  if ((lead & 0x80U) == 0U) {
    value = lead;
  } else if ((lead & 0xE0U) == 0xC0U) {
    extra_bytes = 1;
    value = lead & 0x1FU;
  } else if ((lead & 0xF0U) == 0xE0U) {
    extra_bytes = 2;
    value = lead & 0x0FU;
  } else if ((lead & 0xF8U) == 0xF0U) {
    extra_bytes = 3;
    value = lead & 0x07U;
  } else {
    ++offset;
    codepoint = U'?';
    return true;
  }

  if ((offset + extra_bytes) >= input.size()) {
    offset = input.size();
    codepoint = U'?';
    return true;
  }

  for (std::size_t i = 1; i <= extra_bytes; ++i) {
    const unsigned char byte = static_cast<unsigned char>(input[offset + i]);
    if ((byte & 0xC0U) != 0x80U) {
      offset += i;
      codepoint = U'?';
      return true;
    }
    value = (value << 6U) | (byte & 0x3FU);
  }

  offset += extra_bytes + 1U;
  codepoint = value;
  return true;
}

std::vector<char32_t> utf8_to_codepoints(std::string_view text) {
  const std::string materialized{text};
  std::vector<char32_t> codepoints;
  for (std::size_t offset = 0; offset < materialized.size();) {
    char32_t codepoint = U'\0';
    if (next_utf8_codepoint(materialized, offset, codepoint)) {
      codepoints.push_back(codepoint);
    }
  }
  return codepoints;
}

bool parse_phoneme_id_map(const std::string& body,
                          PiperTTSEngine::PhonemeIdMap& out,
                          std::string& error) {
  const std::regex entry_rx("\"((?:\\\\.|[^\"\\\\])*)\"\\s*:\\s*\\[([^\\]]*)\\]");
  for (std::sregex_iterator it(body.begin(), body.end(), entry_rx), end; it != end; ++it) {
    const std::string phoneme = unescape_json_string((*it)[1].str());
    const auto codepoints = utf8_to_codepoints(phoneme);
    if (codepoints.size() != 1U) {
      error = "PiperTTSEngine: phoneme_id_map contains a non-single-codepoint key";
      return false;
    }

    auto ids = parse_int_array((*it)[2].str());
    if (ids.empty()) {
      error = "PiperTTSEngine: phoneme_id_map entry is empty";
      return false;
    }

    out.emplace(codepoints.front(), std::move(ids));
  }

  if (out.empty()) {
    error = "PiperTTSEngine: phoneme_id_map missing or empty";
    return false;
  }

  return true;
}

bool parse_phoneme_map(const std::string& body,
                       PiperTTSEngine::PhonemeMap& out,
                       std::string& error) {
  const std::regex entry_rx("\"((?:\\\\.|[^\"\\\\])*)\"\\s*:\\s*\\[([^\\]]*)\\]");
  const std::regex phoneme_rx("\"((?:\\\\.|[^\"\\\\])*)\"");

  for (std::sregex_iterator it(body.begin(), body.end(), entry_rx), end; it != end; ++it) {
    const std::string from_phoneme = unescape_json_string((*it)[1].str());
    const auto from_codepoints = utf8_to_codepoints(from_phoneme);
    if (from_codepoints.size() != 1U) {
      error = "PiperTTSEngine: phoneme_map contains a non-single-codepoint key";
      return false;
    }

    std::vector<char32_t> replacements;
    const std::string values = (*it)[2].str();
    for (std::sregex_iterator value_it(values.begin(), values.end(), phoneme_rx), value_end;
         value_it != value_end; ++value_it) {
      const std::string replacement = unescape_json_string((*value_it)[1].str());
      const auto replacement_codepoints = utf8_to_codepoints(replacement);
      if (replacement_codepoints.size() != 1U) {
        error = "PiperTTSEngine: phoneme_map replacement must be a single codepoint";
        return false;
      }
      replacements.push_back(replacement_codepoints.front());
    }

    if (!replacements.empty()) {
      out.emplace(from_codepoints.front(), std::move(replacements));
    }
  }

  return true;
}

std::string lowercase_ascii_copy(std::string text) {
  for (char& ch : text) {
    if (static_cast<unsigned char>(ch) < 0x80U) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  return text;
}

std::vector<char32_t> apply_phoneme_map(const std::vector<char32_t>& input,
                                        const PiperTTSEngine::PhonemeMap& phoneme_map) {
  std::vector<char32_t> output;
  for (char32_t codepoint : input) {
    const auto it = phoneme_map.find(codepoint);
    if (it == phoneme_map.end()) {
      output.push_back(codepoint);
      continue;
    }

    output.insert(output.end(), it->second.begin(), it->second.end());
  }
  return output;
}

}  // namespace

bool PiperTTSEngine::assets_exist(std::string& error) const {
  if (config_.model_path.empty()) {
    error = "PiperTTSEngine: model_path is empty";
    return false;
  }
  if (!std::filesystem::exists(config_.model_path)) {
    error = "PiperTTSEngine: model file not found at '" + config_.model_path + "'";
    return false;
  }
  if (config_.piper_data_path.empty()) {
    error = "PiperTTSEngine: piper_data_path is empty";
    return false;
  }
  if (!std::filesystem::exists(config_.piper_data_path)) {
    error = "PiperTTSEngine: Piper JSON file not found at '" + config_.piper_data_path + "'";
    return false;
  }
  return true;
}

bool PiperTTSEngine::load_voice_config(std::string& error) {
  const auto json = read_text_file(config_.piper_data_path, error);
  if (!json) {
    return false;
  }

  phoneme_type_ = extract_json_string(*json, "phoneme_type").value_or("text");
  espeak_voice_ = "en";
  noise_scale_ = 0.667F;
  length_scale_ = 1.0F;
  noise_w_ = 0.8F;
  num_speakers_ = 1;

  if (std::string espeak_section; extract_json_object(*json, "espeak", espeak_section)) {
    espeak_voice_ = extract_json_string(espeak_section, "voice").value_or(espeak_voice_);
  }

  if (std::string audio_section; extract_json_object(*json, "audio", audio_section)) {
    if (const auto sample_rate = extract_json_number(audio_section, "sample_rate")) {
      output_sample_rate_ = static_cast<int>(*sample_rate);
    }
  }

  if (std::string inference_section; extract_json_object(*json, "inference", inference_section)) {
    if (const auto noise_scale = extract_json_number(inference_section, "noise_scale")) {
      noise_scale_ = static_cast<float>(*noise_scale);
    }
    if (const auto length_scale = extract_json_number(inference_section, "length_scale")) {
      length_scale_ = static_cast<float>(*length_scale);
    }
    if (const auto noise_w = extract_json_number(inference_section, "noise_w")) {
      noise_w_ = static_cast<float>(*noise_w);
    }
  }

  if (const auto speakers = extract_json_number(*json, "num_speakers")) {
    num_speakers_ = static_cast<std::uint32_t>(std::max(*speakers, 1.0));
  }

  phoneme_id_map_.clear();
  phoneme_map_.clear();

  std::string phoneme_id_map_body;
  if (!extract_json_object(*json, "phoneme_id_map", phoneme_id_map_body) ||
      !parse_phoneme_id_map(phoneme_id_map_body, phoneme_id_map_, error)) {
    if (error.empty()) {
      error = "PiperTTSEngine: failed to parse phoneme_id_map";
    }
    return false;
  }

  if (std::string phoneme_map_body; extract_json_object(*json, "phoneme_map", phoneme_map_body)) {
    if (!parse_phoneme_map(phoneme_map_body, phoneme_map_, error)) {
      return false;
    }
  }

  if (output_sample_rate_ <= 0) {
    error = "PiperTTSEngine: invalid output sample rate from Piper JSON";
    return false;
  }

  return true;
}

bool PiperTTSEngine::load_onnx_session(std::string& error) {
#if defined(MEV_ENABLE_ONNXRUNTIME)
  try {
    ort_env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "mev_piper");

    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

#if defined(_WIN32)
    const std::wstring model_path = std::filesystem::path(config_.model_path).wstring();
    session_ = std::make_unique<Ort::Session>(*ort_env_, model_path.c_str(), session_options);
#else
    session_ = std::make_unique<Ort::Session>(*ort_env_, config_.model_path.c_str(), session_options);
#endif
  } catch (const Ort::Exception& ex) {
    error = std::string("PiperTTSEngine: ONNX session creation failed: ") + ex.what();
    return false;
  }

  return true;
#else
  (void)error;
  return false;
#endif
}

bool PiperTTSEngine::initialize(const TTSConfig& config, std::string& error) {
  config_ = config;
  output_sample_rate_ = static_cast<int>(config.output_sample_rate);
  initialized_ = false;

#if defined(MEV_ENABLE_ONNXRUNTIME)
  if (!assets_exist(error)) {
    MEV_LOG_ERROR(error);
    return false;
  }

  if (!load_voice_config(error)) {
    MEV_LOG_ERROR(error);
    return false;
  }

  if (config.output_sample_rate != 0 &&
      static_cast<int>(config.output_sample_rate) != output_sample_rate_) {
    MEV_LOG_WARN("PiperTTSEngine: config output_sample_rate=", config.output_sample_rate,
                 " differs from model sample rate=", output_sample_rate_,
                 " — using model sample rate");
  }

  if (phoneme_type_ == "espeak") {
#if defined(MEV_ENABLE_ESPEAK)
    const int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, nullptr,
                                       espeakINITIALIZE_DONT_EXIT);
    if (rate < 0) {
      error = "PiperTTSEngine: failed to initialize eSpeak phonemizer";
      MEV_LOG_ERROR(error);
      return false;
    }
    espeak_SetVoiceByName(espeak_voice_.c_str());
#else
    error = "PiperTTSEngine: Piper voice requires eSpeak phonemization "
            "(compile with MEV_ENABLE_ESPEAK=ON)";
    MEV_LOG_ERROR(error);
    return false;
#endif
  }

  if (!load_onnx_session(error)) {
    MEV_LOG_ERROR(error);
    return false;
  }

  initialized_ = true;
  MEV_LOG_INFO("PiperTTSEngine initialised (phoneme_type=", phoneme_type_,
               " gpu_requested=", config_.gpu_enabled,
               " sr=", output_sample_rate_,
               " speakers=", num_speakers_,
               " model='", config_.model_path,
               "')");
  return true;
#else
  error = "PiperTTSEngine: ONNX Runtime not compiled in (MEV_ENABLE_ONNXRUNTIME=OFF)";
  MEV_LOG_ERROR(error);
  return false;
#endif
}

void PiperTTSEngine::warmup() {
  if (!initialized_) {
    return;
  }

  std::vector<float> pcm;
  if (!synthesize("warmup", pcm)) {
    MEV_LOG_WARN("PiperTTSEngine warmup failed");
    return;
  }

  MEV_LOG_INFO("PiperTTSEngine warmup done (generated ", pcm.size(), " samples)");
}

bool PiperTTSEngine::text_to_phoneme_ids(const std::string& text,
                                         std::vector<std::int64_t>& phoneme_ids,
                                         std::string& error) const {
  std::vector<char32_t> phonemes;

  if (phoneme_type_ == "text") {
    const std::string normalized = lowercase_ascii_copy(text);
    phonemes = utf8_to_codepoints(normalized);
  } else if (phoneme_type_ == "espeak") {
#if defined(MEV_ENABLE_ESPEAK)
    const std::string normalized = lowercase_ascii_copy(text);
    const void* text_ptr = normalized.c_str();
    while (text_ptr != nullptr) {
      const void* previous = text_ptr;
      const char* phoneme_chunk =
          espeak_TextToPhonemes(&text_ptr, espeakCHARS_UTF8, espeakPHONEMES_IPA);
      if (phoneme_chunk == nullptr || *phoneme_chunk == '\0') {
        if (text_ptr == previous) {
          break;
        }
        const char* tail = static_cast<const char*>(text_ptr);
        if (tail == nullptr || *tail == '\0') {
          break;
        }
        continue;
      }

      const auto chunk_codepoints = utf8_to_codepoints(phoneme_chunk);
      phonemes.insert(phonemes.end(), chunk_codepoints.begin(), chunk_codepoints.end());

      const char* tail = static_cast<const char*>(text_ptr);
      if (tail == nullptr || *tail == '\0') {
        break;
      }
    }
#else
    error = "PiperTTSEngine: Piper voice requires eSpeak phonemization";
    return false;
#endif
  } else {
    error = "PiperTTSEngine: unsupported phoneme_type '" + phoneme_type_ + "'";
    return false;
  }

  if (phonemes.empty()) {
    error = "PiperTTSEngine: phonemization produced no phonemes";
    return false;
  }

  phonemes = apply_phoneme_map(phonemes, phoneme_map_);

  const auto bos_it = phoneme_id_map_.find(U'^');
  const auto eos_it = phoneme_id_map_.find(U'$');
  const auto pad_it = phoneme_id_map_.find(U'_');
  if (bos_it == phoneme_id_map_.end() || eos_it == phoneme_id_map_.end() ||
      pad_it == phoneme_id_map_.end()) {
    error = "PiperTTSEngine: Piper JSON is missing required ^ / _ / $ phoneme ids";
    return false;
  }

  phoneme_ids.clear();
  phoneme_ids.insert(phoneme_ids.end(), bos_it->second.begin(), bos_it->second.end());

  std::size_t missing_count = 0;
  for (char32_t phoneme : phonemes) {
    const auto it = phoneme_id_map_.find(phoneme);
    if (it == phoneme_id_map_.end()) {
      if (std::iswspace(static_cast<wint_t>(phoneme)) != 0) {
        continue;
      }
      ++missing_count;
      continue;
    }

    phoneme_ids.insert(phoneme_ids.end(), it->second.begin(), it->second.end());
    phoneme_ids.insert(phoneme_ids.end(), pad_it->second.begin(), pad_it->second.end());
  }

  phoneme_ids.insert(phoneme_ids.end(), eos_it->second.begin(), eos_it->second.end());

  if (phoneme_ids.size() <= bos_it->second.size() + eos_it->second.size()) {
    error = "PiperTTSEngine: no mapped phonemes available for synthesis";
    return false;
  }

  if (missing_count > 0U) {
    MEV_LOG_WARN("PiperTTSEngine: skipped ", missing_count,
                 " phoneme(s) missing from phoneme_id_map");
  }

  return true;
}

bool PiperTTSEngine::synthesize_ids(const std::vector<std::int64_t>& phoneme_ids,
                                    std::vector<float>& pcm_out,
                                    std::string& error) {
#if defined(MEV_ENABLE_ONNXRUNTIME)
  if (!session_) {
    error = "PiperTTSEngine: ONNX session is not ready";
    return false;
  }

  try {
    Ort::MemoryInfo memory_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::array<std::int64_t, 2> input_shape{1, static_cast<std::int64_t>(phoneme_ids.size())};
    std::array<std::int64_t, 1> input_length_shape{1};
    std::array<std::int64_t, 1> input_lengths{static_cast<std::int64_t>(phoneme_ids.size())};
    std::array<float, 3> scales{noise_scale_, length_scale_, noise_w_};
    std::array<std::int64_t, 1> scales_shape{3};

    std::vector<Ort::Value> input_tensors;
    std::vector<const char*> input_names;

    input_names.push_back("input");
    input_tensors.emplace_back(Ort::Value::CreateTensor<std::int64_t>(
        memory_info, const_cast<std::int64_t*>(phoneme_ids.data()), phoneme_ids.size(),
        input_shape.data(), input_shape.size()));

    input_names.push_back("input_lengths");
    input_tensors.emplace_back(Ort::Value::CreateTensor<std::int64_t>(
        memory_info, input_lengths.data(), input_lengths.size(),
        input_length_shape.data(), input_length_shape.size()));

    input_names.push_back("scales");
    input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
        memory_info, scales.data(), scales.size(),
        scales_shape.data(), scales_shape.size()));

    std::array<std::int64_t, 1> speaker_id{static_cast<std::int64_t>(config_.speaker_id)};
    std::array<std::int64_t, 1> speaker_shape{1};
    if (num_speakers_ > 1U) {
      input_names.push_back("sid");
      input_tensors.emplace_back(Ort::Value::CreateTensor<std::int64_t>(
          memory_info, speaker_id.data(), speaker_id.size(),
          speaker_shape.data(), speaker_shape.size()));
    }

    const char* output_names[] = {"output"};
    Ort::RunOptions run_options;
    auto output_tensors = session_->Run(
        run_options, input_names.data(), input_tensors.data(), input_tensors.size(),
        output_names, 1);

    if (output_tensors.empty() || !output_tensors.front().IsTensor()) {
      error = "PiperTTSEngine: ONNX inference returned no tensor output";
      return false;
    }

    auto& output_tensor = output_tensors.front();
    const auto shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.empty()) {
      error = "PiperTTSEngine: ONNX output tensor has no shape";
      return false;
    }

    const std::size_t sample_count = static_cast<std::size_t>(std::max<std::int64_t>(shape.back(), 0));
    const float* samples = output_tensor.GetTensorData<float>();
    pcm_out.assign(samples, samples + sample_count);
  } catch (const Ort::Exception& ex) {
    error = std::string("PiperTTSEngine: ONNX inference failed: ") + ex.what();
    return false;
  }

  if (pcm_out.empty()) {
    error = "PiperTTSEngine: ONNX inference produced empty PCM";
    return false;
  }

  return true;
#else
  (void)phoneme_ids;
  (void)pcm_out;
  error = "PiperTTSEngine: ONNX Runtime not compiled in";
  return false;
#endif
}

bool PiperTTSEngine::synthesize(const std::string& text, std::vector<float>& pcm_out) {
  pcm_out.clear();
  if (!initialized_) {
    return false;
  }

#if defined(MEV_ENABLE_ONNXRUNTIME)
  std::string error;
  if (!assets_exist(error)) {
    MEV_LOG_ERROR(error);
    return false;
  }

  std::vector<std::int64_t> phoneme_ids;
  if (!text_to_phoneme_ids(text, phoneme_ids, error)) {
    MEV_LOG_ERROR(error);
    return false;
  }

  if (!synthesize_ids(phoneme_ids, pcm_out, error)) {
    MEV_LOG_ERROR(error);
    return false;
  }

  return true;
#else
  (void)text;
  return false;
#endif
}

void PiperTTSEngine::shutdown() {
  initialized_ = false;
#if defined(MEV_ENABLE_ONNXRUNTIME)
  session_.reset();
  ort_env_.reset();
#endif
}

}  // namespace mev
