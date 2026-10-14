#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }

    constexpr int delayMask = (1 << 14) - 1;
}

DecimulTriggerProcessor::DecimulTriggerProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& v : velLo) v.store(1);
    for (auto& v : velHi) v.store(127);
    for (auto& t : loadToken) t.store(0);

    delayL.assign((size_t) delayBufferSize, 0.0f);
    delayR.assign((size_t) delayBufferSize, 0.0f);
    pendingOffs.reserve(256);

    startTimer(100);
}

DecimulTriggerProcessor::~DecimulTriggerProcessor()
{
    stopTimer();
    shuttingDown.store(true);
    loaderPool.removeAllJobs(true, 20000);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulTriggerProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto dbAttr = AudioParameterFloatAttributes().withLabel("dB");
    auto msAttr = AudioParameterFloatAttributes().withLabel("ms");
    auto hzAttr = AudioParameterFloatAttributes().withLabel("Hz");

    // Detección
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::threshold), "Threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -30.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::maxLevel), "Max Level", NormalisableRange<float>(-40.0f, 0.0f, 0.1f), -6.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::scan), "Scan", NormalisableRange<float>(0.5f, 10.0f, 0.1f), 2.0f, msAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::retrigger), "Retrigger", NormalisableRange<float>(5.0f, 250.0f, 1.0f, 0.5f), 35.0f, msAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::lowCut), "Low Cut", NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 60.0f, hzAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::highCut), "High Cut", NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.4f), 14000.0f, hzAttr));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::listen), "Listen Detection", false));

    // Velocity
    params.push_back(std::make_unique<AudioParameterInt>(pid(ParamIDs::minVel), "Min Velocity", 1, 127, 20));
    params.push_back(std::make_unique<AudioParameterInt>(pid(ParamIDs::maxVel), "Max Velocity", 1, 127, 127));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::curve), "Velocity Curve", NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::fixedVel), "Fixed Velocity", false));

    // MIDI
    params.push_back(std::make_unique<AudioParameterInt>(
        pid(ParamIDs::note), "Note", 0, 127, 38,
        AudioParameterIntAttributes().withStringFromValueFunction(
            [](int v, int) { return MidiMessage::getMidiNoteName(v, true, true, 4) + " (" + String(v) + ")"; })));
    params.push_back(std::make_unique<AudioParameterInt>(pid(ParamIDs::channel), "Channel", 1, 16, 10));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::noteLen), "Note Length", NormalisableRange<float>(5.0f, 500.0f, 1.0f, 0.5f), 40.0f, msAttr));

    // Reproductor propio
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::sampler), "Sampler", StringArray { "Off", "Reinforce", "Replace" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::sampleLevel), "Sample Level", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::origLevel), "Original Level", NormalisableRange<float>(-60.0f, 0.0f, 0.1f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::velSens), "Velocity Sensitivity", NormalisableRange<float>(0.0f, 100.0f, 1.0f), 60.0f,
        AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::align), "Align Original", false));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::choke), "Choke", false));

    // Salida
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, dbAttr));
    params.push_back(std::make_unique<AudioParameterBool>(pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulTriggerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out || (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void DecimulTriggerProcessor::prepareToPlay(double sampleRate, int)
{
    const bool rateChanged = std::abs(sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate;

    detector.prepare(sampleRate);
    detHighpass.reset();
    detLowpass.reset();
    lastLowCut = lastHighCut = -1.0f;

    std::fill(delayL.begin(), delayL.end(), 0.0f);
    std::fill(delayR.begin(), delayR.end(), 0.0f);
    delayWrite = 0;
    reportedLatency = -1;
    updateReportedLatency();

    pendingOffs.clear();
    for (auto& v : voices)
        v.active = false;

    scopeFrameSamples = std::max(1, (int) std::lround(sampleRate / (double) scopeFramesPerSecond));
    scopeFrameLeft = scopeFrameSamples;
    scopeFramePeak = 0.0f;
    scopeFrameVelocity = 0;

    if (rateChanged)
    {
        reloadAllRequested.store(true);
        triggerAsyncUpdate();
    }
}

// La latencia que se informa al host es lo que retrasa el original en modo "alinear".
// Se actualiza desde el message thread (timer): avisar cambios de latencia desde el
// audio thread no es seguro en todos los hosts.
void DecimulTriggerProcessor::updateReportedLatency()
{
    const bool align = apvts.getRawParameterValue(ParamIDs::align)->load() > 0.5f;
    const int scanSamples = std::max(1, (int) std::lround(
        (double) apvts.getRawParameterValue(ParamIDs::scan)->load() * 0.001 * currentSampleRate));
    const int desired = align ? std::min(scanSamples, delayBufferSize - 1) : 0;

    if (desired != reportedLatency)
    {
        reportedLatency = desired;
        setLatencySamples(desired);
    }
}

void DecimulTriggerProcessor::updateFilters(float lowHz, float highHz)
{
    if (lowHz != lastLowCut)
    {
        detHighpass.setHighpass(currentSampleRate, lowHz);
        lastLowCut = lowHz;
    }
    if (highHz != lastHighCut)
    {
        detLowpass.setLowpass(currentSampleRate, highHz);
        lastHighCut = highHz;
    }
}

void DecimulTriggerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();   // este plugin genera el MIDI: no deja pasar el que venga

    const int n = buffer.getNumSamples();
    auto main = getBusBuffer(buffer, true, 0);
    if (n <= 0 || main.getNumChannels() < 1)
        return;

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    if (get(ParamIDs::bypass) > 0.5f)
    {
        outputPeakDb.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, n), -100.0f));
        return;
    }

    const bool stereo = main.getNumChannels() > 1;
    float* L = main.getWritePointer(0);
    float* R = stereo ? main.getWritePointer(1) : L;

    // Señal de detección: el sidechain del plugin si está activo; si no, la entrada.
    const float* detL = L;
    const float* detR = R;
    if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled())
    {
        auto sc = getBusBuffer(buffer, true, 1);
        if (sc.getNumChannels() >= 1)
        {
            detL = sc.getReadPointer(0);
            detR = sc.getNumChannels() > 1 ? sc.getReadPointer(1) : detL;
        }
    }

    // ---- Parámetros ----
    const float thresholdDb = get(ParamIDs::threshold);
    const float maxLevelDb = get(ParamIDs::maxLevel);
    const float curve = get(ParamIDs::curve);
    const int minVel = (int) std::lround(get(ParamIDs::minVel));
    const int maxVel = (int) std::lround(get(ParamIDs::maxVel));
    const bool fixedVel = get(ParamIDs::fixedVel) > 0.5f;
    const int note = (int) std::lround(get(ParamIDs::note));
    const int channel = (int) std::lround(get(ParamIDs::channel));
    const bool listen = get(ParamIDs::listen) > 0.5f;
    const int samplerMode = (int) std::lround(get(ParamIDs::sampler));
    const bool align = get(ParamIDs::align) > 0.5f;
    const bool choke = get(ParamIDs::choke) > 0.5f;

    const double sr = currentSampleRate;
    const int scanSamples = std::max(1, (int) std::lround((double) get(ParamIDs::scan) * 0.001 * sr));
    const float retriggerMs = get(ParamIDs::retrigger);
    const int noteLenSamples = std::max(1, (int) std::lround((double) get(ParamIDs::noteLen) * 0.001 * sr));

    updateFilters(get(ParamIDs::lowCut), get(ParamIDs::highCut));

    HitDetector::Params dp;
    dp.thresholdLin = juce::Decibels::decibelsToGain(thresholdDb);
    dp.scanSamples = scanSamples;
    dp.lockoutSamples = std::max(1, (int) std::lround((double) retriggerMs * 0.001 * sr));
    dp.maskDecay = std::exp(-1.0f / (retriggerMs * 0.001f * (float) sr));

    // El original se retrasa lo que tarda la medición, así el MIDI/sample coincide con él.
    // (La latencia se le avisa al host desde el message thread: ver updateReportedLatency.)
    const int delaySamples = align ? std::min(scanSamples, delayBufferSize - 1) : 0;

    const float outTrim = juce::Decibels::decibelsToGain(get(ParamIDs::outputTrim));
    const float sampleGain = juce::Decibels::decibelsToGain(get(ParamIDs::sampleLevel));
    const float origDb = get(ParamIDs::origLevel);
    float origGain = 1.0f;
    if (samplerMode == samplerReplace)
        origGain = 0.0f;
    else if (samplerMode == samplerReinforce)
        origGain = origDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain(origDb);
    const float velAmount = get(ParamIDs::velSens) * 0.01f;
    const int chokeFade = std::max(1, (int) (0.004 * sr));

    // Las muestras no se pueden cambiar mientras este bloque las usa. Si el message
    // thread las está intercambiando justo ahora, este bloque no las reproduce.
    const juce::SpinLock::ScopedTryLockType slotTryLock(slotLock);
    const bool slotsAvailable = slotTryLock.isLocked() && samplerMode != samplerOff;

    auto startVoice = [&](int velocity)
    {
        if (!slotsAvailable)
            return;

        // Round-robin: de las muestras cuyo rango de velocity incluye este golpe, la que
        // hace más tiempo que no suena.
        int best = -1;
        juce::uint64 bestTick = std::numeric_limits<juce::uint64>::max();
        for (int s = 0; s < numSlots; ++s)
        {
            if (slots[(size_t) s] == nullptr)
                continue;
            if (velocity < velLo[(size_t) s].load() || velocity > velHi[(size_t) s].load())
                continue;
            if (lastUsedTick[(size_t) s] < bestTick)
            {
                bestTick = lastUsedTick[(size_t) s];
                best = s;
            }
        }
        if (best < 0)
            return;

        lastUsedTick[(size_t) best] = ++tickCounter;

        if (choke)
            for (auto& v : voices)
                if (v.active && v.fadeLeft == 0)
                {
                    v.fadeTotal = chokeFade;
                    v.fadeLeft = chokeFade;
                }

        Voice* target = nullptr;
        for (auto& v : voices)
            if (!v.active)
            {
                target = &v;
                break;
            }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.order < target->order)
                    target = &v;
        }

        const auto& audio = slots[(size_t) best]->audio;
        target->left = audio.getReadPointer(0);
        target->right = audio.getNumChannels() > 1 ? audio.getReadPointer(1) : audio.getReadPointer(0);
        target->length = audio.getNumSamples();
        target->pos = 0;
        target->gain = sampleGain * ((1.0f - velAmount) + velAmount * ((float) velocity / 127.0f));
        target->fadeLeft = 0;
        target->order = ++tickCounter;
        target->active = true;
        lastSlotPlayed.store(best);
    };

    // ---- Muestra a muestra ----
    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i];
        const float inR = R[i];

        // Detección: mono, pasa-banda
        float d = 0.5f * (detL[i] + detR[i]);
        d = detLowpass.process(detHighpass.process(d));

        // Audio original, con el retardo opcional
        delayL[(size_t) delayWrite] = inL;
        delayR[(size_t) delayWrite] = inR;
        const int readIndex = (delayWrite - delaySamples) & delayMask;
        const float origL = delayL[(size_t) readIndex];
        const float origR = delayR[(size_t) readIndex];
        delayWrite = (delayWrite + 1) & delayMask;

        float peak = 0.0f;
        if (detector.process(d, dp, peak))
        {
            const int velocity = fixedVel ? std::clamp(maxVel, 1, 127)
                                          : velocityFromPeak(peak, thresholdDb, maxLevelDb, curve, minVel, maxVel);

            midi.addEvent(juce::MidiMessage::noteOn(channel, note, (juce::uint8) velocity), i);
            pendingOffs.push_back({ sampleCounter + noteLenSamples, note, channel });

            startVoice(velocity);

            lastVelocity.store(velocity);
            hitCount.fetch_add(1);
            scopeFrameVelocity = std::max(scopeFrameVelocity, velocity);
        }

        // Note-offs que vencen
        for (size_t k = 0; k < pendingOffs.size();)
        {
            if (pendingOffs[k].due <= sampleCounter)
            {
                midi.addEvent(juce::MidiMessage::noteOff(pendingOffs[k].channel, pendingOffs[k].note), i);
                pendingOffs.erase(pendingOffs.begin() + (std::ptrdiff_t) k);
            }
            else
            {
                ++k;
            }
        }
        ++sampleCounter;

        // Voces del reproductor
        float sL = 0.0f, sR = 0.0f;
        for (auto& v : voices)
        {
            if (!v.active)
                continue;

            if (v.pos >= v.length)
            {
                v.active = false;
                continue;
            }

            float g = v.gain;
            if (v.fadeLeft > 0)
            {
                g *= (float) v.fadeLeft / (float) v.fadeTotal;
                if (--v.fadeLeft == 0)
                    v.active = false;
            }

            sL += v.left[v.pos] * g;
            sR += v.right[v.pos] * g;
            ++v.pos;
        }

        float outL = origL * origGain + sL;
        float outR = origR * origGain + sR;
        if (listen)
            outL = outR = d;

        L[i] = outL * outTrim;
        if (stereo)
            R[i] = outR * outTrim;

        // Monitor de golpes
        scopeFramePeak = std::max(scopeFramePeak, std::abs(d));
        if (--scopeFrameLeft <= 0)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            scopeFifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                scopeBuffer[(size_t) start1] = { scopeFramePeak, scopeFrameVelocity };
                scopeFifo.finishedWrite(1);
            }
            scopeFrameLeft = scopeFrameSamples;
            scopeFramePeak = 0.0f;
            scopeFrameVelocity = 0;
        }
    }

    outputPeakDb.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, n), -100.0f));
}

int DecimulTriggerProcessor::readScopeFrames(ScopeFrame* dest, int maxFrames)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    scopeFifo.prepareToRead(std::min(maxFrames, scopeFifo.getNumReady()), start1, size1, start2, size2);

    int copied = 0;
    for (int i = 0; i < size1; ++i)
        dest[copied++] = scopeBuffer[(size_t) (start1 + i)];
    for (int i = 0; i < size2; ++i)
        dest[copied++] = scopeBuffer[(size_t) (start2 + i)];

    scopeFifo.finishedRead(size1 + size2);
    return copied;
}

// =============================== Muestras ===============================

void DecimulTriggerProcessor::loadSample(int slot, const juce::File& file)
{
    startLoading(juce::jlimit(0, numSlots - 1, slot), file);
}

void DecimulTriggerProcessor::startLoading(int slot, const juce::File& file)
{
    const int token = ++loadToken[(size_t) slot];

    auto& info = slotInfo[(size_t) slot];
    info.file = file;
    info.name = file.getFileNameWithoutExtension();
    info.loading = true;
    info.error.clear();

    const double rate = currentSampleRate;

    loaderPool.addJob([this, slot, token, file, rate]
    {
        LoadResult result;
        result.slot = slot;
        result.token = token;

        if (!file.existsAsFile())
        {
            result.error = "No existe el archivo";
        }
        else
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            {
                result.error = "Formato no soportado";
            }
            else
            {
                const int channels = (int) std::min<unsigned int>(2u, reader->numChannels);
                const auto length = (int) std::min<juce::int64>(reader->lengthInSamples,
                                                                 (juce::int64) (maxSampleSeconds * reader->sampleRate));
                juce::AudioBuffer<float> source(channels, length + 64);
                source.clear();
                reader->read(&source, 0, length, 0, true, channels > 1);

                auto sample = std::make_shared<TriggerSample>();
                if (std::abs(reader->sampleRate - rate) > 0.5)
                {
                    const double ratio = reader->sampleRate / rate;
                    const int outLength = (int) std::ceil((double) length / ratio);
                    sample->audio.setSize(channels, outLength);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        juce::LagrangeInterpolator interpolator;
                        interpolator.process(ratio, source.getReadPointer(ch), sample->audio.getWritePointer(ch), outLength);
                    }
                }
                else
                {
                    sample->audio.setSize(channels, length);
                    for (int ch = 0; ch < channels; ++ch)
                        sample->audio.copyFrom(ch, 0, source, ch, 0, length);
                }
                result.sample = sample;
            }
        }

        if (shuttingDown.load())
            return;

        {
            const juce::ScopedLock sl(resultsLock);
            pendingResults.push_back(std::move(result));
        }
        triggerAsyncUpdate();
    });
}

void DecimulTriggerProcessor::clearSample(int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    ++loadToken[(size_t) slot];

    std::shared_ptr<TriggerSample> old;
    {
        const juce::SpinLock::ScopedLockType lock(slotLock);
        old = std::move(slots[(size_t) slot]);
    }
    if (old != nullptr)
        retired.push_back({ std::move(old), juce::Time::getMillisecondCounter() });

    slotInfo[(size_t) slot] = {};
}

void DecimulTriggerProcessor::handleAsyncUpdate()
{
    if (reloadAllRequested.exchange(false))
    {
        for (int s = 0; s < numSlots; ++s)
        {
            const auto file = slotInfo[(size_t) s].file;
            if (file != juce::File() && file.existsAsFile())
                startLoading(s, file);
        }
    }

    std::vector<std::pair<int, juce::File>> requests;
    std::vector<LoadResult> results;
    {
        const juce::ScopedLock sl(resultsLock);
        requests.swap(pendingLoadRequests);
        results.swap(pendingResults);
    }

    for (auto& [slot, file] : requests)
        startLoading(slot, file);

    for (auto& r : results)
    {
        if (r.token != loadToken[(size_t) r.slot].load())
            continue;

        auto& info = slotInfo[(size_t) r.slot];
        info.loading = false;

        std::shared_ptr<TriggerSample> old;
        {
            const juce::SpinLock::ScopedLockType lock(slotLock);
            old = std::move(slots[(size_t) r.slot]);
            slots[(size_t) r.slot] = r.sample;
        }
        if (old != nullptr)
            retired.push_back({ std::move(old), juce::Time::getMillisecondCounter() });

        info.loaded = r.sample != nullptr;
        info.error = r.sample != nullptr ? juce::String() : r.error;
    }
}

// Las muestras reemplazadas se guardan un rato por si una voz todavía las está leyendo.
void DecimulTriggerProcessor::timerCallback()
{
    updateReportedLatency();

    const auto now = juce::Time::getMillisecondCounter();
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [now](const Retired& r) { return now - r.atMs > 15000; }),
                  retired.end());
}

int DecimulTriggerProcessor::getResidentSamples() const
{
    const juce::SpinLock::ScopedLockType lock(slotLock);
    int count = 0;
    for (auto& s : slots)
        if (s != nullptr)
            ++count;
    return count;
}

void DecimulTriggerProcessor::setVelocityRange(int slot, int lo, int hi)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    lo = juce::jlimit(1, 127, lo);
    hi = juce::jlimit(1, 127, hi);
    velLo[(size_t) slot].store(std::min(lo, hi));
    velHi[(size_t) slot].store(std::max(lo, hi));
}

void DecimulTriggerProcessor::getVelocityRange(int slot, int& lo, int& hi) const
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    lo = velLo[(size_t) slot].load();
    hi = velHi[(size_t) slot].load();
}

// =============================== Estado ===============================

juce::AudioProcessorEditor* DecimulTriggerProcessor::createEditor()
{
    return new DecimulTriggerEditor(*this);
}

void DecimulTriggerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;

    if (auto xml = state.createXml())
    {
        auto* samples = xml->createNewChildElement("TRIGGER_SAMPLES");
        for (int s = 0; s < numSlots; ++s)
        {
            auto* e = samples->createNewChildElement("SAMPLE");
            e->setAttribute("slot", s);
            e->setAttribute("path", slotInfo[(size_t) s].file.getFullPathName());
            e->setAttribute("lo", velLo[(size_t) s].load());
            e->setAttribute("hi", velHi[(size_t) s].load());
        }
        copyXmlToBinary(*xml, destData);
    }
}

void DecimulTriggerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    std::vector<std::pair<int, juce::File>> toLoad;

    if (auto* samples = xml->getChildByName("TRIGGER_SAMPLES"))
    {
        for (auto* e : samples->getChildIterator())
        {
            const int s = e->getIntAttribute("slot", -1);
            if (s < 0 || s >= numSlots)
                continue;

            setVelocityRange(s, e->getIntAttribute("lo", 1), e->getIntAttribute("hi", 127));

            const juce::File file(e->getStringAttribute("path"));
            if (file != juce::File() && file.existsAsFile())
                toLoad.emplace_back(s, file);
        }
        xml->removeChildElement(samples, true);
    }

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (!toLoad.empty())
    {
        {
            const juce::ScopedLock sl(resultsLock);
            for (auto& item : toLoad)
                pendingLoadRequests.push_back(item);
        }
        triggerAsyncUpdate();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulTriggerProcessor();
}
