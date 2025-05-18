#ifndef PIPER_H_
#define PIPER_H_

#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <piper-phonemize/phoneme_ids.hpp>
#include <piper-phonemize/phonemize.hpp>
#include <piper-phonemize/tashkeel.hpp>

#include "json.hpp"

using json = nlohmann::json;

namespace piper {

typedef int64_t SpeakerId;
enum PhonemeType { eSpeakPhonemes, TextPhonemes };

struct eSpeakConfig {
    std::string voice = "en-us";
};

struct ModelSession {
    Ort::Session onnx;
    Ort::SessionOptions options;
    Ort::Env env;

    ModelSession() : onnx(nullptr){};
};

struct PhonemizeConfig {
    PhonemeType phonemeType = eSpeakPhonemes;
    std::optional<std::map<Phoneme, std::vector<Phoneme>>> phonemeMap;
    std::map<Phoneme, std::vector<PhonemeId>> phonemeIdMap;

    PhonemeId idPad = 0; // padding (optionally interspersed)
    PhonemeId idBos = 1; // beginning of sentence
    PhonemeId idEos = 2; // end of sentence
    bool interspersePad = true;

    eSpeakConfig eSpeak;
};

struct SynthesisConfig {
    // VITS inference settings
    float noiseScale = 0.667f;
    float lengthScale = 1.0f;
    float noiseW = 0.8f;

    // Audio settings
    int sampleRate = 22050;
    int sampleWidth = 2; // 16-bit
    int channels = 1;    // mono

    // Speaker id from 0 to numSpeakers - 1
    std::optional<SpeakerId> speakerId;

    // Extra silence
    float sentenceSilenceSeconds = 0.2f;
    std::optional<std::map<piper::Phoneme, float>> phonemeSilenceSeconds;
};

struct ModelConfig {
    int numSpeakers;
    // speaker name -> id
    std::optional<std::map<std::string, SpeakerId>> speakerIdMap;
};

struct Voice {
    json              configRoot;
    PhonemizeConfig   phonemizeConfig;
    SynthesisConfig   synthesisConfig;
    ModelConfig       modelConfig;
    ModelSession      session;
};



} // namespace piper
#endif
