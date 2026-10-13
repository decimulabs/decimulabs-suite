#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
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
    for (auto& b : mixLiveDb) b.store(-120.0f);

    gainA.setCurrentAndTargetValue(1.0f);
    gainB.setCurrentAndTargetValue(1.0f);
    abMix.setCurrentAndTargetValue(0.0f);

    featureRing.assign((size_t) featureRingFrames * VersusBands::numBands, -120.0f);
    abxXIsB.store(abxRandom.nextBool());

    // Cada frame de espectro de la mezcla alimenta el analizador en tiempo real y el
    // anillo que usa la alineación de secciones. Corre en el audio thread: solo
    // atómicos y un try-lock.
    mixBands.onFrame = [this](const std::array<float, VersusBands::numBands>& db, bool silent)
    {
        for (int b = 0; b < VersusBands::numBands; ++b)
            mixLiveDb[(size_t) b].store(db[(size_t) b]);
        mixLiveActive.store(!silent);

        const juce::SpinLock::ScopedTryLockType lock(featureLock);
        if (lock.isLocked())
        {
            std::copy(db.begin(), db.end(),
                      featureRing.begin() + (size_t) featureHead * VersusBands::numBands);
            featureHead = (featureHead + 1) % featureRingFrames;
            featureCount = std::min(featureCount + 1, featureRingFrames);
        }
    };

    startTimer(300);
}

DecimulVersusProcessor::~DecimulVersusProcessor()
{
    stopTimer();
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
        pid(ParamIDs::slot), "Reference Slot (B)", 1, numSlots, 1));
    params.push_back(std::make_unique<AudioParameterChoice>(
        pid(ParamIDs::sourceA), "Source A",
        StringArray { "Mix", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8" }, 0));
    params.push_back(std::make_unique<AudioParameterBool>(
        pid(ParamIDs::abxMode), "ABX Blind Test", false));
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
    gainA.reset(sampleRate, 0.4);
    gainB.reset(sampleRate, 0.4);

    sideA.setSize(2, samplesPerBlock);
    sideB.setSize(2, samplesPerBlock);
    sideA.clear();
    sideB.clear();
    monoScratch.assign((size_t) samplesPerBlock, 0.0f);

    mixLoudness.prepare(sampleRate);
    mixBands.prepare(sampleRate);
    mixTruePeak.reset();
    accLL = accRR = accLR = accM = accS = 0.0;
    mixPeakLin = mixTruePeakLin = 0.0f;
    publishCounter = 0;
    wasPlaying = false;
    lastAL = lastAR = 0.0f;

    if (rateChanged)
    {
        reloadAllRequested.store(true);
        triggerAsyncUpdate();
    }
}

int DecimulVersusProcessor::getSelectedSlot() const
{
    return juce::jlimit(0, numSlots - 1,
                        (int) std::lround(apvts.getRawParameterValue(ParamIDs::slot)->load()) - 1);
}

int DecimulVersusProcessor::getSourceASlot() const
{
    return juce::jlimit(-1, numSlots - 1,
                        (int) std::lround(apvts.getRawParameterValue(ParamIDs::sourceA)->load()) - 1);
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

    if (sideA.getNumSamples() < numSamples)
    {
        sideA.setSize(2, numSamples, false, false, true);
        sideB.setSize(2, numSamples, false, false, true);
    }
    if ((int) monoScratch.size() < numSamples)
        monoScratch.resize((size_t) numSamples);

    auto get = [this](const juce::String& id) { return apvts.getRawParameterValue(id)->load(); };

    // ====================== Análisis de la mezcla (siempre) ======================
    if (resetMixRequested.exchange(false))
    {
        mixLoudness.reset();
        mixBands.reset();
        accLL = accRR = accLR = accM = accS = 0.0;
        mixPeakLin = mixTruePeakLin = 0.0f;
        mixBandsValid.store(false);
    }

    {
        const float* inL = buffer.getReadPointer(0);
        const float* inR = buffer.getReadPointer(1);

        mixLoudness.process(inL, inR, numSamples);
        mixTruePeakLin = std::max(mixTruePeakLin, mixTruePeak.process(inL, inR, numSamples));

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
            mixLoudnessRange.store(mixLoudness.getLoudnessRange());
            mixTruePeakDb.store(juce::Decibels::gainToDecibels(std::max(mixTruePeakLin, mixPeakLin), -100.0f));
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

    // ====================== Fuentes A y B ======================
    // En un render/bounce offline SIEMPRE pasa la mezcla del DAW, sin voces: si el
    // plugin quedó en B (o con A apuntando a un slot) al exportar, el archivo saldría
    // con una referencia en vez de tu mezcla.
    const bool offline = isNonRealtime();

    const bool abxOn = get(ParamIDs::abxMode) > 0.5f;
    bool wantB;
    if (abxOn)
    {
        const int listen = abxListen.load();
        wantB = listen == 1 || (listen == 2 && abxXIsB.load());
    }
    else
    {
        wantB = get(ParamIDs::refActive) > 0.5f;
    }
    wantB = wantB && !offline;

    const bool followHost = get(ParamIDs::followHost) > 0.5f;
    const bool loopOn = get(ParamIDs::loopEnabled) > 0.5f;
    const bool matchAuto = (int) get(ParamIDs::matchMode) == 0;
    const float trimDb = get(ParamIDs::trim);
    const int slotParam = getSelectedSlot();
    const int srcA = offline ? -1 : getSourceASlot();

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

    bool sourceAChanged = false;
    if (srcA != currentSrcA)
    {
        currentSrcA = srcA;
        declickTotal = std::max(1, (int) (0.01 * currentSampleRate));
        declickLeft = declickTotal;
        sourceAChanged = true;
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
    const ReferenceTrack* curB = nullptr;
    const ReferenceTrack* prevB = nullptr;
    const ReferenceTrack* curA = nullptr;

    if (tryLock.isLocked() && !offline)
    {
        auto usable = [this](const ReferenceTrack* t) -> const ReferenceTrack*
        {
            return (t != nullptr && t->hasAudio() && std::abs(t->sampleRate - currentSampleRate) < 0.5) ? t : nullptr;
        };

        curB = usable(slots[(size_t) currentSlot].get());
        if (prevSlot >= 0 && xfadeLeft > 0)
            prevB = usable(slots[(size_t) prevSlot].get());
        if (srcA >= 0)
            curA = usable(slots[(size_t) srcA].get());
    }
    if (xfadeLeft <= 0)
        prevSlot = -1;

    // --- Ganancias: igualado por LUFS + trim manual (solo lado B), con tope de pico ---
    {
        bool haveTarget = false;
        float targetLufs = 0.0f;

        if (matchAuto)
        {
            if (srcA < 0)
            {
                const float mixLufs = mixIntegratedLufs.load();
                if (mixLufs > -69.0f && curB != nullptr && curB->integratedLufs > -69.0f)
                {
                    targetLufs = mixLufs;
                    haveTarget = true;
                }
            }
            else if (curA != nullptr && curB != nullptr)
            {
                // Dos slots: se baja el más fuerte al nivel del más bajo (nunca se sube).
                targetLufs = std::min(curA->integratedLufs, curB->integratedLufs);
                haveTarget = targetLufs > -69.0f;
            }
        }

        auto computeDb = [&](const ReferenceTrack* t, float extraDb, bool& limited)
        {
            float db = extraDb;
            if (haveTarget && t->integratedLufs > -69.0f)
                db += juce::jlimit(-30.0f, 30.0f, targetLufs - t->integratedLufs);

            const float maxDb = -t->truePeakDb;   // que el pico real no pase de 0 dBFS
            limited = db > maxDb;
            return limited ? maxDb : db;
        };

        bool limitedB = false, limitedA = false;
        if (curB != nullptr)
        {
            const float db = computeDb(curB, trimDb, limitedB);
            const float target = juce::Decibels::decibelsToGain(db);
            if (slotChanged) gainB.setCurrentAndTargetValue(target);
            else             gainB.setTargetValue(target);
            appliedRefGainDb.store(db);
        }
        if (curA != nullptr)
        {
            const float db = computeDb(curA, 0.0f, limitedA);
            const float target = juce::Decibels::decibelsToGain(db);
            if (sourceAChanged) gainA.setCurrentAndTargetValue(target);
            else                gainA.setTargetValue(target);
            appliedGainADb.store(db);
        }
        refGainLimitedByPeak.store(limitedA || limitedB);
    }

    // --- Render de las voces (cabezal compartido) ---
    float* bufL = sideB.getWritePointer(0);
    float* bufR = sideB.getWritePointer(1);
    float* aufL = sideA.getWritePointer(0);
    float* aufR = sideA.getWritePointer(1);

    const bool anyTrack = curB != nullptr || curA != nullptr;
    const juce::int64 lenB = curB != nullptr ? (juce::int64) curB->audio.getNumSamples() : 0;
    const juce::int64 lenA = curA != nullptr ? (juce::int64) curA->audio.getNumSamples() : 0;
    const juce::int64 length = curB != nullptr ? lenB : lenA;
    const juce::int64 maxLength = std::max(lenA, lenB);

    bool loopActive = false;
    juce::int64 loopS = 0, loopE = 0;
    if (anyTrack && loopOn)
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
    if (anyTrack && wantPlay && !wasPlaying && length > 0 && currentPos >= length - 1)
        currentPos = loopActive ? loopS : 0;

    const float* bl = curB != nullptr ? curB->audio.getReadPointer(0) : nullptr;
    const float* br = curB != nullptr ? curB->audio.getReadPointer(1) : nullptr;
    const float* al = curA != nullptr ? curA->audio.getReadPointer(0) : nullptr;
    const float* ar = curA != nullptr ? curA->audio.getReadPointer(1) : nullptr;
    const juce::int64 prevLength = prevB != nullptr ? (juce::int64) prevB->audio.getNumSamples() : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        float lb = 0.0f, rb = 0.0f, la = 0.0f, ra = 0.0f;

        if (wantPlay && anyTrack)
        {
            if (loopActive && currentPos >= loopE)
                currentPos = loopS;

            if (currentPos >= 0 && currentPos < maxLength)
            {
                if (curB != nullptr && currentPos < lenB) { lb = bl[currentPos]; rb = br[currentPos]; }
                if (curA != nullptr && currentPos < lenA) { la = al[currentPos]; ra = ar[currentPos]; }
                ++currentPos;
            }
        }

        if (xfadeLeft > 0)
        {
            float pl = 0.0f, pr = 0.0f;
            if (prevB != nullptr && wantPlay && prevPos >= 0 && prevPos < prevLength)
            {
                pl = prevB->audio.getReadPointer(0)[prevPos];
                pr = prevB->audio.getReadPointer(1)[prevPos];
                ++prevPos;
            }

            const float w = (float) xfadeLeft / (float) xfadeTotal;
            lb = lb * (1.0f - w) + pl * w;
            rb = rb * (1.0f - w) + pr * w;
            --xfadeLeft;
        }

        bufL[i] = lb;  bufR[i] = rb;
        aufL[i] = la;  aufR[i] = ra;
    }

    wasPlaying = wantPlay;
    playheadSamples.store(currentPos);
    referenceRunning.store(wantPlay && anyTrack);

    // --- Crossfade A/B (potencia constante) ---
    abMix.setTargetValue((wantB && curB != nullptr) ? 1.0f : 0.0f);

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    constexpr float halfPi = 1.57079632679f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float t = abMix.getNextValue();
        const float gB = gainB.getNextValue();

        float aL, aR;
        if (srcA < 0)
        {
            aL = outL[i];
            aR = outR[i];
        }
        else
        {
            const float gA = gainA.getNextValue();
            aL = aufL[i] * gA;
            aR = aufR[i] * gA;
        }

        // Al cambiar la fuente de A, se cruza desde el último valor para no hacer click.
        if (declickLeft > 0)
        {
            const float w = (float) declickLeft / (float) declickTotal;
            aL = aL * (1.0f - w) + lastAL * w;
            aR = aR * (1.0f - w) + lastAR * w;
            --declickLeft;
        }
        lastAL = aL;
        lastAR = aR;

        const float wA = std::cos(t * halfPi);
        const float wB = std::sin(t * halfPi) * gB;

        outL[i] = aL * wA + bufL[i] * wB;
        outR[i] = aR * wA + bufR[i] * wB;
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

std::shared_ptr<const ReferenceTrack> DecimulVersusProcessor::getTrack(int slot) const
{
    const juce::SpinLock::ScopedLockType lock(slotLock);
    return slots[(size_t) slot];
}

int DecimulVersusProcessor::getResidentCount() const
{
    const juce::SpinLock::ScopedLockType lock(slotLock);
    int count = 0;
    for (auto& s : slots)
        if (s != nullptr && s->hasAudio())
            ++count;
    return count;
}

void DecimulVersusProcessor::loadReference(int slot, const juce::File& file)
{
    startLoading(juce::jlimit(0, numSlots - 1, slot), file, false);
}

void DecimulVersusProcessor::startLoading(int slot, const juce::File& file, bool keepPosition)
{
    const int token = ++loadToken[(size_t) slot];

    auto& info = slotInfo[(size_t) slot];
    info.file = file;
    info.name = file.getFileNameWithoutExtension();
    info.loading = true;
    info.error.clear();

    const double rate = currentSampleRate;

    loaderPool.addJob([this, slot, token, file, rate, keepPosition]
    {
        LoadResult result;
        result.slot = slot;
        result.token = token;
        result.keepPosition = keepPosition;
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
                startLoading(s, file, false);
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
        startLoading(slot, file, false);

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
            lastUsed[(size_t) r.slot] = ++useTick;

            if (!r.keepPosition)
            {
                slotPos[(size_t) r.slot].store(0);
                resetPosSlot.store(r.slot);
            }
        }
        else
        {
            info.loaded = false;
            info.error = r.error;
        }
        // `old` se libera acá, en el message thread.
    }
}

// Mantiene el audio decodificado solo para los slots que se usan: el elegido (B), el
// de la fuente A y los más recientes, hasta `maxResidentSlots`. El resto conserva el
// análisis (forma de onda, medidas, espectro) y se recarga cuando se vuelve a usar.
void DecimulVersusProcessor::timerCallback()
{
    maintainResidency();
}

void DecimulVersusProcessor::maintainResidency()
{
    const int selected = getSelectedSlot();
    const int sourceA = getSourceASlot();

    lastUsed[(size_t) selected] = ++useTick;
    if (sourceA >= 0)
        lastUsed[(size_t) sourceA] = ++useTick;

    for (int s : { selected, sourceA })
    {
        if (s < 0)
            continue;

        const auto track = getTrack(s);
        auto& info = slotInfo[(size_t) s];
        if (track != nullptr && !track->hasAudio() && !info.loading && info.file.existsAsFile())
            startLoading(s, info.file, true);
    }

    for (;;)
    {
        int resident = 0, victim = -1;
        for (int s = 0; s < numSlots; ++s)
        {
            const auto track = getTrack(s);
            if (track == nullptr || !track->hasAudio())
                continue;

            ++resident;
            if (s == selected || s == sourceA)
                continue;
            if (victim < 0 || lastUsed[(size_t) s] < lastUsed[(size_t) victim])
                victim = s;
        }

        if (resident <= maxResidentSlots || victim < 0)
            break;

        auto lite = getTrack(victim)->makeLite();
        std::shared_ptr<ReferenceTrack> old;
        {
            const juce::SpinLock::ScopedLockType lock(slotLock);
            old = std::move(slots[(size_t) victim]);
            slots[(size_t) victim] = lite;
        }
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

// =============================== ABX ===============================

void DecimulVersusProcessor::abxAnswer(bool guessXIsB)
{
    ++abxTotal;
    if (guessXIsB == abxXIsB.load())
        ++abxCorrect;

    abxXIsB.store(abxRandom.nextBool());   // nueva X para el próximo intento
}

void DecimulVersusProcessor::abxReset()
{
    abxCorrect.store(0);
    abxTotal.store(0);
    abxXIsB.store(abxRandom.nextBool());
}

// ============================ Alineación ============================

bool DecimulVersusProcessor::alignToMix(juce::String& report)
{
    constexpr int bands = VersusBands::numBands;
    constexpr int maxWindow = 96;      // ~8 s a 48 kHz (un frame cada 85 ms)
    constexpr int minWindow = 48;      // ~4 s

    const auto track = getTrack(getSelectedSlot());
    if (track == nullptr || track->frameCount < minWindow + 2)
    {
        report = "Cargá una referencia (de al menos unos segundos) en el slot B.";
        return false;
    }

    // Snapshot de los últimos frames de la mezcla, en orden cronológico.
    std::vector<float> mix;
    int frames = 0;
    {
        const juce::SpinLock::ScopedLockType lock(featureLock);
        frames = std::min(featureCount, maxWindow);
        mix.resize((size_t) frames * bands);
        for (int k = 0; k < frames; ++k)
        {
            const int src = ((featureHead - frames + k) % featureRingFrames + featureRingFrames) % featureRingFrames;
            std::copy_n(featureRing.begin() + (size_t) src * bands, bands, mix.begin() + (size_t) k * bands);
        }
    }

    // Un frame cuenta como "con señal" si alguna banda supera el piso de silencio.
    int audible = 0;
    for (int k = 0; k < frames; ++k)
    {
        float loudest = -200.0f;
        for (int b = 0; b < bands; ++b)
            loudest = std::max(loudest, mix[(size_t) k * bands + (size_t) b]);
        if (loudest > -100.0f)
            ++audible;
    }

    if (frames < minWindow || audible < (frames * 3) / 4)
    {
        report = "Dejá sonar la mezcla unos 5 segundos y volvé a apretar.";
        return false;
    }

    const int K = frames;
    if (track->frameCount < K)
    {
        report = "La referencia es más corta que el tramo de la mezcla.";
        return false;
    }

    auto clampDb = [](float v) { return std::max(v, -90.0f); };

    // La mezcla, sin su nivel medio: se comparan forma tonal y evolución, no volumen.
    double mixMean = 0.0;
    for (auto& v : mix)
    {
        v = clampDb(v);
        mixMean += v;
    }
    mixMean /= (double) mix.size();
    for (auto& v : mix)
        v -= (float) mixMean;

    // Sumas acumuladas por frame de la referencia, para la media de cada ventana.
    const int N = track->frameCount;
    std::vector<double> prefix((size_t) N + 1, 0.0);
    for (int f = 0; f < N; ++f)
    {
        double s = 0.0;
        for (int b = 0; b < bands; ++b)
            s += clampDb(track->frameDb[(size_t) f * bands + (size_t) b]);
        prefix[(size_t) f + 1] = prefix[(size_t) f] + s;
    }

    double best = 1.0e300, total = 0.0;
    int bestStart = 0, candidates = 0;

    for (int s = 0; s + K <= N; ++s)
    {
        const double windowMean = (prefix[(size_t) s + K] - prefix[(size_t) s]) / (double) (K * bands);

        double dist = 0.0;
        for (int k = 0; k < K; ++k)
        {
            const float* r = track->frameDb.data() + (size_t) (s + k) * bands;
            const float* m = mix.data() + (size_t) k * bands;
            for (int b = 0; b < bands; ++b)
            {
                const double d = (clampDb(r[b]) - windowMean) - (double) m[b];
                dist += d * d;
            }
        }
        dist /= (double) (K * bands);

        total += dist;
        ++candidates;
        if (dist < best)
        {
            best = dist;
            bestStart = s;
        }
    }

    // El cabezal tiene que quedar en el "ahora" de la mezcla = el final de la ventana.
    const int endFrame = bestStart + K - 1;
    const double seconds = ((double) endFrame * ReferenceTrack::frameHop + ReferenceTrack::frameWindow * 0.5)
                           / track->sampleRate;
    seekSeconds(seconds);

    const double ratio = candidates > 0 ? best / std::max(1.0e-9, total / (double) candidates) : 1.0;
    const char* confidence = ratio < 0.25 ? "alta" : (ratio < 0.5 ? "media" : "baja");

    const int totalSec = (int) seconds;
    report = "Salté a " + juce::String::formatted("%d:%02d", totalSec / 60, totalSec % 60)
           + " (coincidencia " + confidence + ")";
    return true;
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
