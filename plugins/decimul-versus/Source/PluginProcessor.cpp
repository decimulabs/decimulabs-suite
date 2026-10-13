#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }
}

DecimulVersusProcessor::DecimulVersusProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& t : loadToken) t.store(0);
    for (auto& p : slotPos) p.store(0);
    for (auto& s : loopStartSec) s.store(0.0f);
    for (auto& e : loopEndSec) e.store(0.0f);
    for (auto& b : mixBandDb) b.store(-120.0f);

    refGain.setCurrentAndTargetValue(1.0f);
    abMix.setCurrentAndTargetValue(0.0f);
}

DecimulVersusProcessor::~DecimulVersusProcessor()
{
    shuttingDown.store(true);
    loaderPool.removeAllJobs(true, 20000);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulVersusProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::refActive), "Listen B (Reference)", false));
    params.push_back(std::make_unique<AudioParameterInt>(
        pid(ParamIDs::slot), "Reference Slot", 1, numSlots, 1));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::matchMode), "Loudness Match", StringArray { "Auto", "Off" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::trim), "Reference Trim", NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::monitor), "Monitor", StringArray { "Stereo", "Mono", "Side" }, 0));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::band), "Band Solo", StringArray { "Full", "Low", "Mid", "High" }, 0));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::followHost), "Follow Host Transport", true));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::loopEnabled), "Loop", false));
    params.push_back(std::make_unique<AudioParameterFloat>(
        pid(ParamIDs::outputTrim), "Output", NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulVersusProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono();
}

void DecimulVersusProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs(sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;

    outputGain.prepare(spec);
    outputGain.setRampDurationSeconds(0.02);

    lowLP.prepare(spec);  lowLP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);   lowLP.setCutoffFrequency(150.0f);
    midHP.prepare(spec);  midHP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);  midHP.setCutoffFrequency(150.0f);
    midLP.prepare(spec);  midLP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);   midLP.setCutoffFrequency(2500.0f);
    highHP.prepare(spec); highHP.setType(juce::dsp::LinkwitzRileyFilterType::highpass); highHP.setCutoffFrequency(2500.0f);

    abMix.reset(sampleRate, 0.015);
    refGain.reset(sampleRate, 0.4);

    refBuffer.setSize(2, samplesPerBlock);
    refBuffer.clear();
    monoScratch.assign((size_t) samplesPerBlock, 0.0f);

    mixLoudness.prepare(sampleRate);
    mixBands.prepare(sampleRate);
    accLL = accRR = accLR = accM = accS = 0.0;
    mixPeakLin = 0.0f;
    publishCounter = 0;
    wasPlaying = false;

    if (rateChanged)
    {
        reloadAllRequested.store(true);
        triggerAsyncUpdate();
    }
}

void DecimulVersusProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    if (getMainBusNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    if (buffer.getNumChannels() < 2)
        return;

    if (refBuffer.getNumSamples() < numSamples)
        refBuffer.setSize(2, numSamples, false, false, true);
    if ((int) monoScratch.size() < numSamples)
        monoScratch.resize((size_t) numSamples);

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    // ====================== Análisis de la mezcla (siempre, A o B) ======================
    if (resetMixRequested.exchange(false))
    {
        mixLoudness.reset();
        mixBands.reset();
        accLL = accRR = accLR = accM = accS = 0.0;
        mixPeakLin = 0.0f;
        mixBandsValid.store(false);
    }

    {
        const float* inL = buffer.getReadPointer(0);
        const float* inR = buffer.getReadPointer(1);

        mixLoudness.process(inL, inR, numSamples);

        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = inL[i], r = inR[i];
            blockPeak = std::max({ blockPeak, std::abs(l), std::abs(r) });
            monoScratch[(size_t) i] = 0.5f * (l + r);

            if ((double) l * l + (double) r * r > 1.0e-8)
            {
                accLL += (double) l * l;
                accRR += (double) r * r;
                accLR += (double) l * r;
                const double m = 0.5 * ((double) l + r), s = 0.5 * ((double) l - r);
                accM += m * m;
                accS += s * s;
            }
        }

        mixBands.push(monoScratch.data(), numSamples);
        mixPeakLin = std::max(mixPeakLin, blockPeak);
        inputPeakDb.store(juce::Decibels::gainToDecibels(blockPeak, -100.0f));

        publishCounter += numSamples;
        if (publishCounter >= (int) (currentSampleRate * 0.25))
        {
            publishCounter = 0;
            mixIntegratedLufs.store(mixLoudness.getIntegratedLufs());
            mixShortTermLufs.store(mixLoudness.getShortTermLufs());
            mixPeakDb.store(juce::Decibels::gainToDecibels(mixPeakLin, -100.0f));
            mixCorrelation.store((accLL > 0.0 && accRR > 0.0) ? (float) (accLR / std::sqrt(accLL * accRR)) : 1.0f);
            mixWidthPct.store((accM + accS) > 0.0 ? (float) (100.0 * accS / (accM + accS)) : 0.0f);

            std::array<float, VersusBands::numBands> bands {};
            if (mixBands.getAverageDb(bands))
            {
                for (int b = 0; b < VersusBands::numBands; ++b)
                    mixBandDb[(size_t) b].store(bands[(size_t) b]);
                mixBandsValid.store(true);
            }
        }
    }

    if (get(ParamIDs::bypass) > 0.5f)
    {
        referenceRunning.store(false);
        outputPeakDb.store(inputPeakDb.load());
        return;
    }

    // ====================== Referencia ======================
    // En un render/bounce offline SIEMPRE pasa la mezcla (A): si el plugin quedó en B
    // al exportar, el archivo saldría con la referencia en vez de tu mezcla.
    const bool wantB = get(ParamIDs::refActive) > 0.5f && !isNonRealtime();
    const bool followHost = get(ParamIDs::followHost) > 0.5f;
    const bool loopOn = get(ParamIDs::loopEnabled) > 0.5f;
    const bool matchAuto = (int) get(ParamIDs::matchMode) == 0;
    const float trimDb = get(ParamIDs::trim);
    const int slotParam = juce::jlimit(0, numSlots - 1, (int) std::lround(get(ParamIDs::slot)) - 1);

    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
            hostPlaying = position->getIsPlaying();

    const bool wantPlay = userPlaying.load() || (followHost && hostPlaying);

    bool slotChanged = false;
    if (slotParam != currentSlot)
    {
        slotPos[(size_t) currentSlot].store(currentPos);
        prevSlot = currentSlot;
        prevPos = currentPos;
        xfadeTotal = std::max(1, (int) (0.02 * currentSampleRate));
        xfadeLeft = xfadeTotal;

        currentSlot = slotParam;
        currentPos = slotPos[(size_t) currentSlot].load();
        slotChanged = true;
    }

    if (const int resetSlot = resetPosSlot.exchange(-1); resetSlot >= 0)
    {
        slotPos[(size_t) resetSlot].store(0);
        if (resetSlot == currentSlot)
            currentPos = 0;
    }

    if (const auto seek = seekRequest.exchange(-1); seek >= 0)
        currentPos = seek;

    // El audio thread nunca espera: si el message thread está intercambiando un slot
    // justo ahora, este bloque suena solo la mezcla.
    const juce::SpinLock::ScopedTryLockType tryLock(slotLock);
    const ReferenceTrack* cur = nullptr;
    const ReferenceTrack* prev = nullptr;
    if (tryLock.isLocked())
    {
        auto usable = [this](const ReferenceTrack* t)
        {
            return t != nullptr && t->audio.getNumSamples() > 0
                && std::abs(t->sampleRate - currentSampleRate) < 0.5;
        };
        const ReferenceTrack* c = slots[(size_t) currentSlot].get();
        cur = usable(c) ? c : nullptr;
        if (prevSlot >= 0 && xfadeLeft > 0)
        {
            const ReferenceTrack* p = slots[(size_t) prevSlot].get();
            prev = usable(p) ? p : nullptr;
        }
    }
    if (xfadeLeft <= 0)
        prevSlot = -1;

    // --- Ganancia de la referencia: igualado por LUFS + trim manual, con tope de pico ---
    if (cur != nullptr)
    {
        float totalDb = trimDb;
        if (matchAuto)
        {
            const float mixLufs = mixIntegratedLufs.load();
            if (mixLufs > -69.0f && cur->integratedLufs > -69.0f)
                totalDb += juce::jlimit(-30.0f, 30.0f, mixLufs - cur->integratedLufs);
        }

        const float maxDb = -cur->peakDb;
        const bool limited = totalDb > maxDb;
        if (limited)
            totalDb = maxDb;

        const float target = juce::Decibels::decibelsToGain(totalDb);
        if (slotChanged)
            refGain.setCurrentAndTargetValue(target);
        else
            refGain.setTargetValue(target);

        appliedRefGainDb.store(totalDb);
        refGainLimitedByPeak.store(limited);
    }

    // --- Render de la referencia a refBuffer ---
    float* refL = refBuffer.getWritePointer(0);
    float* refR = refBuffer.getWritePointer(1);

    bool loopActive = false;
    juce::int64 loopS = 0, loopE = 0;
    if (cur != nullptr)
    {
        const juce::int64 length = cur->audio.getNumSamples();

        if (loopOn)
        {
            const auto s = (juce::int64) ((double) loopStartSec[(size_t) currentSlot].load() * currentSampleRate);
            const auto e = (juce::int64) ((double) loopEndSec[(size_t) currentSlot].load() * currentSampleRate);
            if (e - s > (juce::int64) (0.05 * currentSampleRate) && e <= length)
            {
                loopActive = true;
                loopS = s;
                loopE = e;
            }
        }

        // Al arrancar el play desde el final del tema, vuelve al principio (o al loop).
        if (wantPlay && !wasPlaying && currentPos >= length - 1)
            currentPos = loopActive ? loopS : 0;

        const float* cl = cur->audio.getReadPointer(0);
        const float* cr = cur->audio.getReadPointer(1);
        const juce::int64 prevLength = prev != nullptr ? (juce::int64) prev->audio.getNumSamples() : 0;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = 0.0f, r = 0.0f;

            if (wantPlay)
            {
                if (loopActive && currentPos >= loopE)
                    currentPos = loopS;

                if (currentPos >= 0 && currentPos < length)
                {
                    l = cl[currentPos];
                    r = cr[currentPos];
                    ++currentPos;
                }
            }

            if (xfadeLeft > 0)
            {
                float pl = 0.0f, pr = 0.0f;
                if (prev != nullptr && wantPlay && prevPos >= 0 && prevPos < prevLength)
                {
                    pl = prev->audio.getReadPointer(0)[prevPos];
                    pr = prev->audio.getReadPointer(1)[prevPos];
                    ++prevPos;
                }

                const float w = (float) xfadeLeft / (float) xfadeTotal;
                l = l * (1.0f - w) + pl * w;
                r = r * (1.0f - w) + pr * w;
                --xfadeLeft;
            }

            refL[i] = l;
            refR[i] = r;
        }
    }
    else
    {
        refBuffer.clear(0, numSamples);
        xfadeLeft = 0;
    }

    wasPlaying = wantPlay;
    playheadSamples.store(currentPos);
    referenceRunning.store(wantPlay && cur != nullptr);

    // --- Crossfade A/B (potencia constante) ---
    abMix.setTargetValue((wantB && cur != nullptr) ? 1.0f : 0.0f);

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    constexpr float halfPi = 1.57079632679f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float t = abMix.getNextValue();
        const float g = refGain.getNextValue();
        const float wMix = std::cos(t * halfPi);
        const float wRef = std::sin(t * halfPi) * g;

        outL[i] = outL[i] * wMix + refL[i] * wRef;
        outR[i] = outR[i] * wMix + refR[i] * wRef;
    }

    // ====================== Monitoreo ======================
    const int monitorMode = (int) get(ParamIDs::monitor);
    if (monitorMode != 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = monitorMode == 1 ? 0.5f * (outL[i] + outR[i])
                                             : 0.5f * (outL[i] - outR[i]);
            outL[i] = outR[i] = v;
        }
    }

    const int bandMode = (int) get(ParamIDs::band);
    if (bandMode != lastBand)
    {
        lowLP.reset();
        midHP.reset();
        midLP.reset();
        highHP.reset();
        lastBand = bandMode;
    }

    if (bandMode != 0)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto stereo = block.getSubsetChannelBlock(0, 2).getSubBlock(0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context(stereo);

        if (bandMode == 1)
        {
            lowLP.process(context);
        }
        else if (bandMode == 2)
        {
            midHP.process(context);
            midLP.process(context);
        }
        else
        {
            highHP.process(context);
        }
    }

    // ====================== Salida ======================
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto stereo = block.getSubsetChannelBlock(0, 2).getSubBlock(0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context(stereo);
        outputGain.setGainDecibels(get(ParamIDs::outputTrim));
        outputGain.process(context);
    }

    outputPeakDb.store(juce::Decibels::gainToDecibels(buffer.getMagnitude(0, numSamples), -100.0f));
}

// =============================== UI / carga ===============================

juce::AudioProcessorEditor* DecimulVersusProcessor::createEditor()
{
    return new DecimulVersusEditor(*this);
}

int DecimulVersusProcessor::getSelectedSlot() const
{
    return juce::jlimit(0, numSlots - 1,
                        (int) std::lround(apvts.getRawParameterValue(ParamIDs::slot)->load()) - 1);
}

std::shared_ptr<const ReferenceTrack> DecimulVersusProcessor::getTrack(int slot) const
{
    const juce::SpinLock::ScopedLockType lock(slotLock);
    return slots[(size_t) slot];
}

void DecimulVersusProcessor::loadReference(int slot, const juce::File& file)
{
    startLoading(juce::jlimit(0, numSlots - 1, slot), file);
}

void DecimulVersusProcessor::startLoading(int slot, const juce::File& file)
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
        result.track = ReferenceTrack::load(file, rate, result.error, [this, slot, token]
        {
            return shuttingDown.load() || loadToken[(size_t) slot].load() != token;
        });

        if (shuttingDown.load())
            return;

        {
            const juce::ScopedLock sl(resultsLock);
            pendingResults.push_back(std::move(result));
        }
        triggerAsyncUpdate();
    });
}

void DecimulVersusProcessor::clearReference(int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    ++loadToken[(size_t) slot];   // descarta cualquier carga en curso para este slot

    std::shared_ptr<ReferenceTrack> old;
    {
        const juce::SpinLock::ScopedLockType lock(slotLock);
        old = std::move(slots[(size_t) slot]);
    }

    slotInfo[(size_t) slot] = {};
    slotPos[(size_t) slot].store(0);
    loopStartSec[(size_t) slot].store(0.0f);
    loopEndSec[(size_t) slot].store(0.0f);
    resetPosSlot.store(slot);
}

void DecimulVersusProcessor::handleAsyncUpdate()
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
            continue;   // llegó tarde: el slot ya se recargó o se vació

        auto& info = slotInfo[(size_t) r.slot];
        info.loading = false;

        std::shared_ptr<ReferenceTrack> old;
        {
            const juce::SpinLock::ScopedLockType lock(slotLock);
            old = std::move(slots[(size_t) r.slot]);
            slots[(size_t) r.slot] = r.track;
        }

        if (r.track != nullptr)
        {
            info.loaded = true;
            info.error.clear();
            info.name = r.track->displayName;
            slotPos[(size_t) r.slot].store(0);
            resetPosSlot.store(r.slot);
        }
        else
        {
            info.loaded = false;
            info.error = r.error;
        }
        // `old` se libera acá, en el message thread.
    }
}

void DecimulVersusProcessor::seekSeconds(double seconds)
{
    const auto samples = (juce::int64) (juce::jmax(0.0, seconds) * currentSampleRate);
    seekRequest.store(samples);
    playheadSamples.store(samples);
}

void DecimulVersusProcessor::setLoopRegion(int slot, double startSeconds, double endSeconds)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    loopStartSec[(size_t) slot].store((float) startSeconds);
    loopEndSec[(size_t) slot].store((float) endSeconds);
}

void DecimulVersusProcessor::getLoopRegion(int slot, double& startSeconds, double& endSeconds) const
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    startSeconds = (double) loopStartSec[(size_t) slot].load();
    endSeconds = (double) loopEndSec[(size_t) slot].load();
}

double DecimulVersusProcessor::getPlayheadSeconds() const
{
    return currentSampleRate > 0.0 ? (double) playheadSamples.load() / currentSampleRate : 0.0;
}

// =============================== Estado ===============================

void DecimulVersusProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (!state.isValid())
        return;

    if (auto xml = state.createXml())
    {
        auto* refs = xml->createNewChildElement("VERSUS_REFS");
        for (int s = 0; s < numSlots; ++s)
        {
            auto* e = refs->createNewChildElement("REF");
            e->setAttribute("slot", s);
            e->setAttribute("path", slotInfo[(size_t) s].file.getFullPathName());
            e->setAttribute("loopStart", (double) loopStartSec[(size_t) s].load());
            e->setAttribute("loopEnd", (double) loopEndSec[(size_t) s].load());
        }
        copyXmlToBinary(*xml, destData);
    }
}

void DecimulVersusProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    std::vector<std::pair<int, juce::File>> toLoad;

    if (auto* refs = xml->getChildByName("VERSUS_REFS"))
    {
        for (auto* e : refs->getChildIterator())
        {
            const int s = e->getIntAttribute("slot", -1);
            if (s < 0 || s >= numSlots)
                continue;

            loopStartSec[(size_t) s].store((float) e->getDoubleAttribute("loopStart", 0.0));
            loopEndSec[(size_t) s].store((float) e->getDoubleAttribute("loopEnd", 0.0));

            const juce::File file(e->getStringAttribute("path"));
            if (file != juce::File() && file.existsAsFile())
                toLoad.emplace_back(s, file);
        }
        xml->removeChildElement(refs, true);
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
    return new DecimulVersusProcessor();
}
