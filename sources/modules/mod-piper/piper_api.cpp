/**
 * v1.0
 * (C)2025 aks
 **/
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <onnxruntime_cxx_api.h>
#include <piper-phonemize/phoneme_ids.hpp>
#include <piper-phonemize/phonemize.hpp>
#include <piper-phonemize/tashkeel.hpp>

#include "json.hpp"
#include "piper.hpp"
#include "utf8.hpp"

#include <ttsd.h>
#include <piper.hpp>
#include <mod-piper.h>

extern mod_piper_global_t *globals;
const float MAX_WAV_VALUE = 32767.0f;

class PiperClient;
class PiperClient {
    public:
        PiperClient(mod_piper_model_descr_t *model) {
            wstk_sdprintf(&instance_id, "piper_%s", model->lang);

            log_notice("Loading model (%s)...", model->model);
            voice.session.env = Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, instance_id); // ORT_LOGGING_LEVEL_VERBOSE
            voice.session.env.DisableTelemetryEvents();

            if(globals->fl_use_gpu) {
                OrtCUDAProviderOptions cuda_options{};
                cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
                voice.session.options.AppendExecutionProvider_CUDA(cuda_options);
            }

            //voice.session.options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
            voice.session.options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            voice.session.options.DisableCpuMemArena();
            voice.session.options.DisableMemPattern();
            voice.session.options.DisableProfiling();

            voice.session.onnx = Ort::Session(voice.session.env, model->model, voice.session.options);

            //
            char *mcname = NULL;
            wstk_sdprintf(&mcname, "%s.json", model->model);

            std::ifstream modelConfigFile(mcname);
            voice.configRoot = json::parse(modelConfigFile);
            modelConfigFile.close();
            wstk_mem_deref(mcname);

            //
            parsePhonemizeConfig(voice.configRoot, voice.phonemizeConfig);
            parseSynthesisConfig(voice.configRoot, voice.synthesisConfig);
            parseModelConfig(voice.configRoot, voice.modelConfig);

            log_notice("Model (%s) ready", model->model);
        }

        ~PiperClient() {
            if(fl_destroyed) { return; }
            fl_destroyed = true;

#ifdef MOD_PIPER_DEBUG
            log_debug("PiperClientDestroy (%s) [refs=%u]", instance_id, refs);
#endif

            if(voice.session.options) {
                voice.session.options.release();
            }
            if(voice.session.onnx) {
                voice.session.onnx.release();
            }
            if(voice.session.env) {
                voice.session.env.release();
            }

            voice.phonemizeConfig.phonemeIdMap.clear();
            voice.phonemizeConfig.phonemeMap.value().clear();

            voice.configRoot.clear();

            wstk_mem_deref(instance_id);
        }

        wstk_status_t synthesize(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params) {
            wstk_status_t status = WSTK_STATUS_FALSE;

            if(fl_destroyed) {
                return WSTK_STATUS_DESTROYED;
            }

            std::size_t sentenceSilenceSamples = 0;
            if (voice.synthesisConfig.sentenceSilenceSeconds > 0) {
                sentenceSilenceSamples = (std::size_t)(voice.synthesisConfig.sentenceSilenceSeconds * voice.synthesisConfig.sampleRate * voice.synthesisConfig.channels);
            }

            std::vector<int16_t> audioBuffer;
            std::vector<std::vector<piper::Phoneme>> phonemes;
            char *sentences[32] = { 0 };
            uint32_t scnt = wstk_str_separate(params->text, '\n', sentences, ARRAY_SIZE(sentences));

            piper::eSpeakPhonemeConfig eSpeakConfig;
            eSpeakConfig.voice = voice.phonemizeConfig.eSpeak.voice;

            for(int i = 0; i < scnt; i++) {
                char *txt = sentences[i];

                if(wstk_str_is_empty(txt)) continue;

                wstk_mutex_lock(globals->mutex_espeak);
                phonemize_eSpeak(txt, eSpeakConfig, phonemes);
                wstk_mutex_unlock(globals->mutex_espeak);

                std::vector<piper::PhonemeId> phonemeIds;
                std::map<piper::Phoneme, std::size_t> missingPhonemes;

                for(auto phonemesIter = phonemes.begin(); phonemesIter != phonemes.end(); ++phonemesIter) {
                    std::vector<piper::Phoneme> &sentencePhonemes = *phonemesIter;
                    std::vector<size_t> phraseSilenceSamples;
                    std::vector<std::shared_ptr<std::vector<piper::Phoneme>>> phrasePhonemes;
                    piper::PhonemeIdConfig idConfig;
                    idConfig.phonemeIdMap = std::make_shared<piper::PhonemeIdMap>(voice.phonemizeConfig.phonemeIdMap);

#ifdef MOD_PIPER_DEBUG
                    if(true) {
                        std::string phonemesStr;
                        for (auto phoneme : sentencePhonemes) { utf8::append(phoneme, std::back_inserter(phonemesStr)); }
                        log_debug("Converting phoneme's to ids (%s)", phonemesStr.c_str());
                    }
#endif

                    if (voice.synthesisConfig.phonemeSilenceSeconds) {
                        std::map<piper::Phoneme, float> &phonemeSilenceSeconds = *voice.synthesisConfig.phonemeSilenceSeconds;

                        auto currentPhrasePhonemes = std::make_shared<std::vector<piper::Phoneme>>();
                        phrasePhonemes.push_back(currentPhrasePhonemes);

                        for (auto sentencePhonemesIter = sentencePhonemes.begin(); sentencePhonemesIter != sentencePhonemes.end(); sentencePhonemesIter++) {
                            piper::Phoneme &currentPhoneme = *sentencePhonemesIter;
                            currentPhrasePhonemes->push_back(currentPhoneme);

                            if (phonemeSilenceSeconds.count(currentPhoneme) > 0) {
                                phraseSilenceSamples.push_back( (std::size_t)(phonemeSilenceSeconds[currentPhoneme] * voice.synthesisConfig.sampleRate * voice.synthesisConfig.channels) );

                                currentPhrasePhonemes = std::make_shared<std::vector<piper::Phoneme>>();
                                phrasePhonemes.push_back(currentPhrasePhonemes);
                            }
                        }
                    } else {
                        phrasePhonemes.push_back(std::make_shared<std::vector<piper::Phoneme>>(sentencePhonemes));
                    }

                    while (phraseSilenceSamples.size() < phrasePhonemes.size()) {
                        phraseSilenceSamples.push_back(0);
                    }

                    for (size_t phraseIdx = 0; phraseIdx < phrasePhonemes.size(); phraseIdx++) {
                        if (phrasePhonemes[phraseIdx]->size() <= 0) continue;
                        phonemes_to_ids(*(phrasePhonemes[phraseIdx]), idConfig, phonemeIds, missingPhonemes);
#ifdef MOD_PIPER_DEBUG
                        if (true) {
                            std::stringstream phonemeIdsStr;
                            for (auto phonemeId : phonemeIds) { phonemeIdsStr << phonemeId << ", "; }
                            log_debug("Converted phonemes to phoneme ids (%s)", phonemeIdsStr.str().c_str());
                        }
#endif

                        doSynthesize(phonemeIds, voice.synthesisConfig, voice.session, audioBuffer);

                        for (std::size_t i = 0; i < phraseSilenceSamples[phraseIdx]; i++) {
                            audioBuffer.push_back(0);
                        }

                        phonemeIds.clear();
                    } /* audio */

                    if (sentenceSilenceSamples > 0) {
                        for (std::size_t i = 0; i < sentenceSilenceSamples; i++) {
                            audioBuffer.push_back(0);
                        }
                    }

                    phrasePhonemes.clear();
                    phraseSilenceSamples.clear();
                } /* for(auto */

                if (missingPhonemes.size() > 0) {
                    log_warn("Missed (%u) phonemes", missingPhonemes.size());
                }

                phonemes.clear();
                phonemeIds.clear();
                missingPhonemes.clear();
            } /* for sencences */

            if(audioBuffer.size() > 0) {
                ttsd_synthesis_result_t *tts_result = NULL;
                int16_t *pv = &audioBuffer[0];

                status = ttsd_synthesis_result_allocate(&tts_result,  voice.synthesisConfig.sampleRate, voice.synthesisConfig.channels, audioBuffer.size(), pv);
                if(status == WSTK_STATUS_SUCCESS) {
                    *result = tts_result;
                }
            }

            phonemes.clear();
            audioBuffer.clear();

            return status;

        }

    private:
        char                *instance_id = NULL;
        char                *model_cname = NULL;
        bool                fl_destroyed = false;
        uint32_t            refs = 0;
        piper::Voice        voice;

        bool isSingleCodepoint(std::string s) {
            return utf8::distance(s.begin(), s.end()) == 1;
        }

        piper::Phoneme getCodepoint(std::string s) {
            utf8::iterator character_iter(s.begin(), s.begin(), s.end());
            return *character_iter;
        }

        void parsePhonemizeConfig(json &configRoot, piper::PhonemizeConfig &phonemizeConfig) {
            if (configRoot.contains("espeak")) {
                auto espeakValue = configRoot["espeak"];
                if (espeakValue.contains("voice")) {
                    phonemizeConfig.eSpeak.voice = espeakValue["voice"].get<std::string>();
                }
            }

            if (configRoot.contains("phoneme_type")) {
                auto phonemeTypeStr = configRoot["phoneme_type"].get<std::string>();
                if (phonemeTypeStr == "text") {
                    phonemizeConfig.phonemeType = piper::TextPhonemes;
                }
            }

            if (configRoot.contains("phoneme_id_map")) {
                auto phonemeIdMapValue = configRoot["phoneme_id_map"];
                for (auto &fromPhonemeItem : phonemeIdMapValue.items()) {
                    std::string fromPhoneme = fromPhonemeItem.key();
                    if (!isSingleCodepoint(fromPhoneme)) {
                        std::stringstream idsStr;
                        for (auto &toIdValue : fromPhonemeItem.value()) {
                            piper::PhonemeId toId = toIdValue.get<piper::PhonemeId>();
                            idsStr << toId << ",";
                        }

                        log_error("Phonemes must be one codepoint (phoneme id map)");
                        throw std::runtime_error("Phonemes must be one codepoint (phoneme id map)");
                    }

                    auto fromCodepoint = getCodepoint(fromPhoneme);
                    for (auto &toIdValue : fromPhonemeItem.value()) {
                        piper::PhonemeId toId = toIdValue.get<piper::PhonemeId>();
                        phonemizeConfig.phonemeIdMap[fromCodepoint].push_back(toId);
                    }
                }
            }

            if (configRoot.contains("phoneme_map")) {
                if (!phonemizeConfig.phonemeMap) {
                    phonemizeConfig.phonemeMap.emplace();
                }

                auto phonemeMapValue = configRoot["phoneme_map"];
                for (auto &fromPhonemeItem : phonemeMapValue.items()) {
                    std::string fromPhoneme = fromPhonemeItem.key();
                    if (!isSingleCodepoint(fromPhoneme)) {
                        log_error("Phonemes must be one codepoint (phoneme map)");
                        throw std::runtime_error("Phonemes must be one codepoint (phoneme map)");
                    }

                    auto fromCodepoint = getCodepoint(fromPhoneme);
                    for (auto &toPhonemeValue : fromPhonemeItem.value()) {
                        std::string toPhoneme = toPhonemeValue.get<std::string>();
                        if (!isSingleCodepoint(toPhoneme)) {
                            log_error("Phonemes must be one codepoint (phoneme map)");
                            throw std::runtime_error("Phonemes must be one codepoint (phoneme map)");
                        }

                        auto toCodepoint = getCodepoint(toPhoneme);
                        (*phonemizeConfig.phonemeMap)[fromCodepoint].push_back(toCodepoint);
                    }
                }
            }
        }

        void parseSynthesisConfig(json &configRoot, piper::SynthesisConfig &synthesisConfig) {
            if (configRoot.contains("audio")) {
                auto audioValue = configRoot["audio"];
                if (audioValue.contains("sample_rate")) {
                    synthesisConfig.sampleRate = audioValue.value("sample_rate", 22050);
                }
            }
            if (configRoot.contains("inference")) {
                auto inferenceValue = configRoot["inference"];
                if (inferenceValue.contains("noise_scale")) {
                    synthesisConfig.noiseScale = inferenceValue.value("noise_scale", 0.667f);
                }
                if (inferenceValue.contains("length_scale")) {
                    synthesisConfig.lengthScale = inferenceValue.value("length_scale", 1.0f);
                }
                if (inferenceValue.contains("noise_w")) {
                    synthesisConfig.noiseW = inferenceValue.value("noise_w", 0.8f);
                }
                if (inferenceValue.contains("phoneme_silence")) {
                    synthesisConfig.phonemeSilenceSeconds.emplace();
                    auto phonemeSilenceValue = inferenceValue["phoneme_silence"];
                    for (auto &phonemeItem : phonemeSilenceValue.items()) {
                        std::string phonemeStr = phonemeItem.key();
                        if (!isSingleCodepoint(phonemeStr)) {
                            log_error("Phonemes must be one codepoint (phoneme silence)");
                            throw std::runtime_error("Phonemes must be one codepoint (phoneme silence)");
                        }

                        auto phoneme = getCodepoint(phonemeStr);
                        (*synthesisConfig.phonemeSilenceSeconds)[phoneme] = phonemeItem.value().get<float>();
                    }
                }
            }
        }

        void parseModelConfig(json &configRoot, piper::ModelConfig &modelConfig) {
            modelConfig.numSpeakers = configRoot["num_speakers"].get<piper::SpeakerId>();

            if (configRoot.contains("speaker_id_map")) {
                if (!modelConfig.speakerIdMap) {
                    modelConfig.speakerIdMap.emplace();
                }

                auto speakerIdMapValue = configRoot["speaker_id_map"];
                for (auto &speakerItem : speakerIdMapValue.items()) {
                    std::string speakerName = speakerItem.key();
                    (*modelConfig.speakerIdMap)[speakerName] = speakerItem.value().get<piper::SpeakerId>();
                }
            }
        }

        void doSynthesize(std::vector<piper::PhonemeId> &phonemeIds, piper::SynthesisConfig &synthesisConfig, piper::ModelSession &session, std::vector<int16_t> &audioBuffer) {
            auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

            std::vector<int64_t> phonemeIdsShape{1, (int64_t)phonemeIds.size()};
            std::vector<int64_t> phonemeIdLengths{(int64_t)phonemeIds.size()};
            std::vector<float> scales{synthesisConfig.noiseScale, synthesisConfig.lengthScale, synthesisConfig.noiseW};

            std::vector<Ort::Value> inputTensors;
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(memoryInfo, phonemeIds.data(), phonemeIds.size(), phonemeIdsShape.data(), phonemeIdsShape.size()));

            std::vector<int64_t> phomemeIdLengthsShape{(int64_t)phonemeIdLengths.size()};
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(memoryInfo, phonemeIdLengths.data(), phonemeIdLengths.size(), phomemeIdLengthsShape.data(), phomemeIdLengthsShape.size()));

            std::vector<int64_t> scalesShape{(int64_t)scales.size()};
            inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, scales.data(), scales.size(), scalesShape.data(), scalesShape.size()));

            std::vector<int64_t> speakerId{(int64_t)synthesisConfig.speakerId.value_or(0)};
            std::vector<int64_t> speakerIdShape{(int64_t)speakerId.size()};

            if (synthesisConfig.speakerId) {
                inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(memoryInfo, speakerId.data(), speakerId.size(), speakerIdShape.data(), speakerIdShape.size()));
            }

            std::array<const char *, 4> inputNames = {"input", "input_lengths", "scales", "sid"};
            std::array<const char *, 1> outputNames = {"output"};

            auto outputTensors = session.onnx.Run(Ort::RunOptions{nullptr}, inputNames.data(), inputTensors.data(), inputTensors.size(), outputNames.data(), outputNames.size());

            if ((outputTensors.size() != 1) || (!outputTensors.front().IsTensor())) {
                throw std::runtime_error("Invalid output tensors");
            }

            const float *audio = outputTensors.front().GetTensorData<float>();
            auto audioShape = outputTensors.front().GetTensorTypeAndShapeInfo().GetShape();
            int64_t audioCount = audioShape[audioShape.size() - 1];


            /* TODO: fix it */

            // Get max audio value for scaling
            float maxAudioValue = 0.01f;
            for (int64_t i = 0; i < audioCount; i++) {
                float audioValue = abs(audio[i]);
                if (audioValue > maxAudioValue) maxAudioValue = audioValue;
            }

            audioBuffer.reserve(audioCount);

            float audioScale = (MAX_WAV_VALUE / std::max(0.01f, maxAudioValue));
            for (int64_t i = 0; i < audioCount; i++) {
                int16_t intAudioValue = static_cast<int16_t>(std::clamp(audio[i] * audioScale, static_cast<float>(std::numeric_limits<int16_t>::min()), static_cast<float>(std::numeric_limits<int16_t>::max())));
                audioBuffer.push_back(intAudioValue);
            }

            for (std::size_t i = 0; i < outputTensors.size(); i++) {
                Ort::detail::OrtRelease(outputTensors[i].release());
            }
            for (std::size_t i = 0; i < inputTensors.size(); i++) {
                Ort::detail::OrtRelease(inputTensors[i].release());
            }

            inputTensors.clear();
            phonemeIdLengths.clear();
            phomemeIdLengthsShape.clear();
            phonemeIdsShape.clear();
            scalesShape.clear();
            scales.clear();
            speakerId.clear();
            speakerIdShape.clear();
            memoryInfo.release();
        }
};

// --------------------------------------------------------------------------------------------------------------------------------------------------------
// C-API
// --------------------------------------------------------------------------------------------------------------------------------------------------------
extern "C" {
    wstk_status_t piper_init(mod_piper_model_descr_t *model) {
        wstk_status_t status = WSTK_STATUS_FALSE;
        try {
            model->piper = new PiperClient(model);
            status = WSTK_STATUS_SUCCESS;
        } catch (std::exception& e) {
            log_error("constructor() failed (%s)", e.what());
        }
        return status;
    }

    wstk_status_t piper_destroy(mod_piper_model_descr_t *model) {
        PiperClient *client = (PiperClient *)model->piper;
        if(client) {
            delete client;
        }
        return WSTK_STATUS_SUCCESS;
    }

    wstk_status_t piper_synthesize(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params) {
        wstk_status_t status = WSTK_STATUS_FALSE;
        mod_piper_model_descr_t *model = NULL;
        PiperClient *client = NULL;

        if(!result || !params) {
            return WSTK_STATUS_INVALID_PARAM;
        }

        wstk_mutex_lock(globals->mutex);
        model = (mod_piper_model_descr_t *) wstk_hash_find(globals->models, params->language);
        if(model && !model->piper) {
            status = piper_init(model);
        } else {
            status = WSTK_STATUS_SUCCESS;
        }
        client = (PiperClient *)model->piper;
        wstk_mutex_unlock(globals->mutex);

        if(!model) {
            log_error("Unsupported language (%s)", params->language);
            return WSTK_STATUS_NOT_FOUND;
        }
        if(status != WSTK_STATUS_SUCCESS) {
            return status;
        }

        try {
            status = client->synthesize(result, params);
        } catch (std::exception& e) {
            log_error("synthesize() failed (%s)", e.what());
        }

        return status;
    }
}
