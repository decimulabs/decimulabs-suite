#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using cincel::BandParams;
using cincel::Placement;
using cincel::Shape;

namespace
{
    juce::ParameterID pid(const juce::String& id) { return { id, 1 }; }

    /** Rango logarítmico de verdad (no "skew"): la mitad del recorrido es la media geométrica. */
    juce::NormalisableRange<float> logRange(float lo, float hi)
    {
        return { lo, hi,
                 [](float s, float e, float v) { return s * std::pow(e / s, v); },
                 [](float s, float e, float x) { return std::log(x / s) / std::log(e / s); },
                 [](float s, float e, float x) { return juce::jlimit(s, e, x); } };
    }

    const char* bandKeyNames[CincelIDs::numBandKeys] = { "used", "on", "freq", "gain", "q", "shape", "slope",
                                                         "place", "dyn", "range", "thr", "auto", "sc", "spec" };
}

juce::String CincelIDs::band(int index, BandKey key)
{
    return "b" + juce::String(index + 1) + "_" + bandKeyNames[key];
}

//==============================================================================
DecimulCincelProcessor::DecimulCincelProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "PARAMETERS", createParameterLayout())
{
    using namespace CincelIDs;
    for (int i = 0; i < cincel::maxBands; ++i)
        for (int k = 0; k < numBandKeys; ++k)
            bandRaw[(size_t) i][(size_t) k] = apvts.getRawParameterValue(band(i, (BandKey) k));
    rawMode = apvts.getRawParameterValue(mode);
    rawLpRes = apvts.getRawParameterValue(lpRes);
    rawCharacter = apvts.getRawParameterValue(character);
    rawGainScale = apvts.getRawParameterValue(gainScale);
    rawAutoGain = apvts.getRawParameterValue(autoGain);
    rawGainQ = apvts.getRawParameterValue(gainQ);
    rawAttack = apvts.getRawParameterValue(attack);
    rawRelease = apvts.getRawParameterValue(release);
    rawOutput = apvts.getRawParameterValue(output);
    rawPan = apvts.getRawParameterValue(pan);
    rawPhase = apvts.getRawParameterValue(phase);
    rawBypass = apvts.getRawParameterValue(bypass);

    instanceId = InstanceRegistry::get().add();
    preAnalyzer.prepare(48000.0);
    postAnalyzer.prepare(48000.0);
    scAnalyzer.prepare(48000.0);
    startTimerHz(30);
}

DecimulCincelProcessor::~DecimulCincelProcessor()
{
    stopTimer();
    InstanceRegistry::get().remove(instanceId);
    engine.release();
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulCincelProcessor::createParameterLayout()
{
    using namespace juce;
    using namespace CincelIDs;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    const StringArray shapes { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
    const StringArray slopes { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
    const StringArray places { "Stereo", "Left", "Right", "Mid", "Side" };
    auto hz = AudioParameterFloatAttributes().withLabel("Hz");
    auto db = AudioParameterFloatAttributes().withLabel("dB");
    auto ms = AudioParameterFloatAttributes().withLabel("ms");

    for (int i = 0; i < cincel::maxBands; ++i)
    {
        const String n = "Band " + String(i + 1) + " ";
        const float defFreq = (float) (40.0 * std::pow(2.0, i * 0.4));
        params.push_back(std::make_unique<AudioParameterBool>(pid(band(i, used)), n + "Used", false));
        params.push_back(std::make_unique<AudioParameterBool>(pid(band(i, on)), n + "Enabled", true));
        params.push_back(std::make_unique<AudioParameterFloat>(pid(band(i, freq)), n + "Frequency", logRange(10.0f, 30000.0f),
                                                               jlimit(10.0f, 30000.0f, defFreq), hz));
        params.push_back(std::make_unique<AudioParameterFloat>(pid(band(i, gain)), n + "Gain",
                                                               NormalisableRange<float>(-30.0f, 30.0f, 0.01f), 0.0f, db));
        params.push_back(std::make_unique<AudioParameterFloat>(pid(band(i, q)), n + "Q", logRange(0.025f, 40.0f), 1.0f));
        params.push_back(std::make_unique<AudioParameterChoice>(pid(band(i, shape)), n + "Shape", shapes, 0));
        params.push_back(std::make_unique<AudioParameterChoice>(pid(band(i, slope)), n + "Slope", slopes, 1));
        params.push_back(std::make_unique<AudioParameterChoice>(pid(band(i, place)), n + "Placement", places, 0));
        params.push_back(std::make_unique<AudioParameterBool>(pid(band(i, dyn)), n + "Dynamic", false));
        params.push_back(std::make_unique<AudioParameterFloat>(pid(band(i, range)), n + "Dynamic Range",
                                                               NormalisableRange<float>(-30.0f, 30.0f, 0.01f), -6.0f, db));
        params.push_back(std::make_unique<AudioParameterFloat>(pid(band(i, thr)), n + "Threshold",
                                                               NormalisableRange<float>(-80.0f, 0.0f, 0.1f), -30.0f, db));
        params.push_back(std::make_unique<AudioParameterBool>(pid(band(i, autoThr)), n + "Auto Threshold", false));
        params.push_back(std::make_unique<AudioParameterBool>(pid(band(i, sc)), n + "External Sidechain", false));
        params.push_back(std::make_unique<AudioParameterBool>(pid(band(i, spec)), n + "Spectral", false));
    }

    params.push_back(std::make_unique<AudioParameterChoice>(pid(mode), "Processing Mode",
                                                            StringArray { "Zero Latency", "Natural Phase", "Linear Phase" }, 0));
    params.push_back(std::make_unique<AudioParameterChoice>(pid(lpRes), "Linear Phase Resolution",
                                                            StringArray { "Low", "Medium", "High", "Very High", "Maximum" }, 2));
    params.push_back(std::make_unique<AudioParameterChoice>(pid(character), "Character", StringArray { "Clean", "Subtle", "Warm" }, 0));
    params.push_back(std::make_unique<AudioParameterFloat>(pid(gainScale), "Gain Scale", NormalisableRange<float>(-100.0f, 200.0f, 0.1f),
                                                           100.0f, AudioParameterFloatAttributes().withLabel("%")));
    params.push_back(std::make_unique<AudioParameterBool>(pid(autoGain), "Auto Gain", false));
    params.push_back(std::make_unique<AudioParameterBool>(pid(gainQ), "Gain-Q Interaction", false));
    params.push_back(std::make_unique<AudioParameterFloat>(pid(attack), "Dynamics Attack", logRange(0.1f, 200.0f), 10.0f, ms));
    params.push_back(std::make_unique<AudioParameterFloat>(pid(release), "Dynamics Release", logRange(5.0f, 2000.0f), 120.0f, ms));
    params.push_back(std::make_unique<AudioParameterFloat>(pid(output), "Output Gain", NormalisableRange<float>(-36.0f, 36.0f, 0.01f), 0.0f, db));
    params.push_back(std::make_unique<AudioParameterFloat>(pid(pan), "Output Pan", NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<AudioParameterBool>(pid(phase), "Phase Invert", false));
    params.push_back(std::make_unique<AudioParameterBool>(pid(bypass), "Bypass", false));

    return { params.begin(), params.end() };
}

bool DecimulCincelProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    // El sidechain es opcional: apagado, mono o estéreo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

void DecimulCincelProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    stereoLayout = getMainBusNumOutputChannels() >= 2;
    engine.prepare(sampleRate, std::max(64, samplesPerBlock), stereoLayout);
    lastOffline = isNonRealtime();
    engine.setOffline(lastOffline);
    preAnalyzer.prepare(sampleRate);
    postAnalyzer.prepare(sampleRate);
    scAnalyzer.prepare(sampleRate);

    reportedLatency = predictedLatency();
    setLatencySamples(reportedLatency);
}

void DecimulCincelProcessor::releaseResources()
{
    engine.release();
}

void DecimulCincelProcessor::fillBands(std::array<BandParams, cincel::maxBands>& out) const
{
    using namespace CincelIDs;
    for (int i = 0; i < cincel::maxBands; ++i)
    {
        const auto& r = bandRaw[(size_t) i];
        auto& b = out[(size_t) i];
        b.used = r[used]->load() > 0.5f;
        b.enabled = r[on]->load() > 0.5f;
        b.freq = r[freq]->load();
        b.gainDb = r[gain]->load();
        b.q = r[q]->load();
        b.shape = (Shape) juce::jlimit(0, cincel::numShapes - 1, (int) std::lround(r[shape]->load()));
        b.slope = juce::jlimit(0, cincel::numSlopes - 1, (int) std::lround(r[slope]->load()));
        b.place = (Placement) juce::jlimit(0, cincel::numPlacements - 1, (int) std::lround(r[place]->load()));
        b.dynamic = r[dyn]->load() > 0.5f;
        b.rangeDb = r[range]->load();
        b.thresholdDb = r[thr]->load();
        b.autoThreshold = r[autoThr]->load() > 0.5f;
        b.sidechain = r[sc]->load() > 0.5f;
        b.spectral = r[spec]->load() > 0.5f;
    }
}

void DecimulCincelProcessor::fillGlobals(cincel::GlobalParams& g) const
{
    g.mode = (cincel::Mode) juce::jlimit(0, 2, (int) std::lround(rawMode->load()));
    g.lpResolution = juce::jlimit(0, cincel::numLpResolutions - 1, (int) std::lround(rawLpRes->load()));
    g.character = (cincel::Character) juce::jlimit(0, 2, (int) std::lround(rawCharacter->load()));
    g.gainScale = rawGainScale->load() * 0.01;
    g.autoGain = rawAutoGain->load() > 0.5f;
    g.gainQ = rawGainQ->load() > 0.5f;
    g.dyn.attackMs = rawAttack->load();
    g.dyn.releaseMs = rawRelease->load();
    g.outputDb = rawOutput->load();
    g.pan = rawPan->load() * 0.01;
    g.phaseInvert = rawPhase->load() > 0.5f;
    g.bypass = rawBypass->load() > 0.5f;
    g.soloBand = soloBand.load();
    g.reserveSpectral = reserveSpectral.load();
}

void DecimulCincelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0)
        return;

    auto main = getBusBuffer(buffer, true, 0);
    if (main.getNumChannels() < 1)
        return;
    float* L = main.getWritePointer(0);
    float* R = main.getNumChannels() > 1 ? main.getWritePointer(1) : nullptr;

    preAnalyzer.push(L, R, n);

    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount(true) > 1)
        if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer(buffer, true, 1);
            if (sc.getNumChannels() >= 1)
            {
                scL = sc.getReadPointer(0);
                scR = sc.getNumChannels() > 1 ? sc.getReadPointer(1) : nullptr;
                scAnalyzer.push(scL, scR, n);
            }
        }
    sidechainConnected.store(scL != nullptr);

    const bool offline = isNonRealtime();
    if (offline != lastOffline)
    {
        engine.setOffline(offline);
        lastOffline = offline;
    }

    fillBands(audioBands);
    cincel::GlobalParams g;
    fillGlobals(g);
    engine.process(L, R, scL, scR, n, audioBands.data(), g);

    postAnalyzer.push(L, R, n);
    outPeakL.store(juce::Decibels::gainToDecibels(main.getMagnitude(0, 0, n), -100.0f));
    outPeakR.store(juce::Decibels::gainToDecibels(main.getMagnitude(R != nullptr ? 1 : 0, 0, n), -100.0f));
}

int DecimulCincelProcessor::predictedLatency() const
{
    std::array<BandParams, cincel::maxBands> b;
    fillBands(b);
    bool spectral = false;
    for (const auto& x : b)
        spectral = spectral || cincel::usesSpectral(x);
    spectral = spectral || reserveSpectral.load();
    const auto m = (cincel::Mode) juce::jlimit(0, 2, (int) std::lround(rawMode->load()));
    const int res = juce::jlimit(0, cincel::numLpResolutions - 1, (int) std::lround(rawLpRes->load()));
    return engine.latencyFor(m, res, spectral);
}

void DecimulCincelProcessor::timerCallback()
{
    const int lat = predictedLatency();
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples(lat);
    }

    auto& reg = InstanceRegistry::get();
    const bool watching = reg.anyoneWatching();
    if (!editorOpen.load() && !watching)
        return;

    auto ui = uiState();
    const int res = (int) ui.getProperty("anaRes", 2);
    const int speed = (int) ui.getProperty("anaSpeed", 2);
    const bool freeze = (bool) ui.getProperty("anaFreeze", false);
    preAnalyzer.update(res, speed, freeze);
    postAnalyzer.update(res, speed, freeze);
    scAnalyzer.update(res, speed, freeze);

    std::array<BandParams, cincel::maxBands> b;
    readAllBands(b);
    const double scale = globalValue(CincelIDs::gainScale) * 0.01;
    const bool gq = globalValue(CincelIDs::gainQ) > 0.5f;
    bool changed = !curveValid || scale != curveScale || gq != curveGainQ;
    for (int i = 0; i < cincel::maxBands && !changed; ++i)
    {
        const auto& x = b[(size_t) i];
        const auto& y = curveBands[(size_t) i];
        changed = x.used != y.used || x.enabled != y.enabled || x.freq != y.freq || x.gainDb != y.gainDb || x.q != y.q
                  || x.shape != y.shape || x.slope != y.slope || x.place != y.place;
    }
    if (changed)
    {
        curves::total(b.data(), scale, gq, globalValue(CincelIDs::mode) > 1.5f, totalCurve);
        curveBands = b;
        curveScale = scale;
        curveGainQ = gq;
        curveValid = true;
    }
    if (watching)
        reg.publish(instanceId, trackName, postAnalyzer.db.data(), totalCurve.data());
}

//==============================================================================
juce::RangedAudioParameter* DecimulCincelProcessor::bandParam(int index, CincelIDs::BandKey key) const
{
    return apvts.getParameter(CincelIDs::band(index, key));
}

BandParams DecimulCincelProcessor::readBand(int index) const
{
    std::array<BandParams, cincel::maxBands> all;
    fillBands(all);
    return all[(size_t) juce::jlimit(0, cincel::maxBands - 1, index)];
}

void DecimulCincelProcessor::readAllBands(std::array<BandParams, cincel::maxBands>& out) const
{
    fillBands(out);
}

void DecimulCincelProcessor::writeBand(int index, const BandParams& b)
{
    using namespace CincelIDs;
    auto set = [&](BandKey key, float plain)
    {
        auto* p = bandParam(index, key);
        const float norm = p->convertTo0to1(plain);
        if (std::abs(p->getValue() - norm) < 1.0e-7f)
            return;
        p->beginChangeGesture();
        p->setValueNotifyingHost(norm);
        p->endChangeGesture();
    };
    set(freq, b.freq);
    set(gain, b.gainDb);
    set(q, b.q);
    set(shape, (float) (int) b.shape);
    set(slope, (float) b.slope);
    set(place, (float) (int) b.place);
    set(dyn, b.dynamic ? 1.0f : 0.0f);
    set(range, b.rangeDb);
    set(thr, b.thresholdDb);
    set(autoThr, b.autoThreshold ? 1.0f : 0.0f);
    set(sc, b.sidechain ? 1.0f : 0.0f);
    set(spec, b.spectral ? 1.0f : 0.0f);
    set(on, b.enabled ? 1.0f : 0.0f);
    set(used, b.used ? 1.0f : 0.0f);
}

int DecimulCincelProcessor::firstFreeBand() const
{
    for (int i = 0; i < cincel::maxBands; ++i)
        if (bandRaw[(size_t) i][CincelIDs::used]->load() < 0.5f)
            return i;
    return -1;
}

void DecimulCincelProcessor::setGlobal(const juce::String& id, float plainValue)
{
    auto* p = apvts.getParameter(id);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    p->endChangeGesture();
}

void DecimulCincelProcessor::setReserveSpectral(bool reserve)
{
    reserveSpectral.store(reserve);
    uiState().setProperty("specReserve", reserve, nullptr);
}

juce::ValueTree DecimulCincelProcessor::uiState()
{
    return apvts.state.getOrCreateChildWithName("UI", nullptr);
}

void DecimulCincelProcessor::resetAll(bool globalsToo)
{
    for (int i = 0; i < cincel::maxBands; ++i)
    {
        BandParams b;
        b.freq = bandParam(i, CincelIDs::freq)->convertFrom0to1(bandParam(i, CincelIDs::freq)->getDefaultValue());
        writeBand(i, b);
    }
    if (globalsToo)
        for (const auto* id : { &CincelIDs::mode, &CincelIDs::lpRes, &CincelIDs::character, &CincelIDs::gainScale,
                                &CincelIDs::autoGain, &CincelIDs::gainQ, &CincelIDs::attack, &CincelIDs::release,
                                &CincelIDs::output, &CincelIDs::pan, &CincelIDs::phase })
        {
            auto* p = apvts.getParameter(*id);
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            p->endChangeGesture();
        }
}

//==============================================================================
// Presets de fábrica: puntos de partida razonables, no recetas. Cada uno arranca de cero.
namespace
{
    struct PB
    {
        Shape shape;
        float freq, gain, q;
        int slope = 1;
        Placement place = Placement::Stereo;
        bool dyn = false;
        float range = -6.0f, thr = -30.0f;
        bool spectral = false;
    };

    struct Preset
    {
        const char* name;
        int mode;
        std::vector<PB> bands;
    };

    const std::vector<Preset>& factoryPresets()
    {
        static const std::vector<Preset> presets = {
            { "Init (flat)", 0, {} },
            { "Vocal - presence and air", 0,
              { { Shape::LowCut, 80.0f, 0.0f, 0.71f, 3 }, { Shape::Bell, 260.0f, -2.0f, 1.2f }, { Shape::Bell, 3200.0f, 2.5f, 0.9f },
                { Shape::HighShelf, 10000.0f, 3.0f, 0.71f } } },
            { "Vocal - dynamic de-esser", 0,
              { { Shape::LowCut, 70.0f, 0.0f, 0.71f, 3 },
                { Shape::Bell, 6800.0f, 0.0f, 2.5f, 1, Placement::Stereo, true, -7.0f, -32.0f, true } } },
            { "Kick - punch, no mud", 0,
              { { Shape::LowCut, 28.0f, 0.0f, 0.71f, 6 }, { Shape::Bell, 62.0f, 3.0f, 1.2f }, { Shape::Bell, 350.0f, -4.0f, 1.5f },
                { Shape::Bell, 3500.0f, 3.0f, 1.0f } } },
            { "Bass - definition", 0,
              { { Shape::LowCut, 32.0f, 0.0f, 0.71f, 3 }, { Shape::Bell, 100.0f, 2.0f, 1.0f }, { Shape::Bell, 800.0f, 2.5f, 1.2f },
                { Shape::HighCut, 9000.0f, 0.0f, 0.71f, 1 } } },
            { "Acoustic guitar - clean up", 0,
              { { Shape::LowCut, 90.0f, 0.0f, 0.71f, 3 }, { Shape::Bell, 210.0f, -3.0f, 1.0f }, { Shape::Bell, 5000.0f, 2.0f, 0.9f },
                { Shape::HighShelf, 12000.0f, 2.0f, 0.71f } } },
            { "Mix bus - gentle sheen", 2,
              { { Shape::LowCut, 22.0f, 0.0f, 0.71f, 3 }, { Shape::LowShelf, 70.0f, 1.0f, 0.71f }, { Shape::Bell, 300.0f, -1.0f, 0.8f },
                { Shape::HighShelf, 12000.0f, 1.5f, 0.71f } } },
            { "Mid/Side - wider sides", 0,
              { { Shape::LowCut, 160.0f, 0.0f, 0.71f, 3, Placement::Side }, { Shape::HighShelf, 7000.0f, 3.0f, 0.71f, 1, Placement::Side },
                { Shape::Bell, 2500.0f, -1.5f, 0.8f, 1, Placement::Mid } } },
            { "Resonance tamer (spectral)", 0,
              { { Shape::Bell, 400.0f, 0.0f, 0.6f, 1, Placement::Stereo, true, -5.0f, -38.0f, true },
                { Shape::Bell, 2500.0f, 0.0f, 0.6f, 1, Placement::Stereo, true, -5.0f, -40.0f, true },
                { Shape::Bell, 7000.0f, 0.0f, 0.7f, 1, Placement::Stereo, true, -5.0f, -44.0f, true } } },
            { "Telephone", 0,
              { { Shape::LowCut, 400.0f, 0.0f, 0.71f, 6 }, { Shape::HighCut, 3200.0f, 0.0f, 0.71f, 6 }, { Shape::Bell, 1500.0f, 6.0f, 1.2f } } },
            { "Tilt - darker", 0, { { Shape::FlatTilt, 1000.0f, -6.0f, 1.0f } } },
            { "Tilt - brighter", 0, { { Shape::FlatTilt, 1000.0f, 6.0f, 1.0f } } },
        };
        return presets;
    }
}

juce::StringArray DecimulCincelProcessor::factoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add(p.name);
    return names;
}

void DecimulCincelProcessor::loadFactoryPreset(int index)
{
    const auto& all = factoryPresets();
    if (index < 0 || index >= (int) all.size())
        return;
    const auto& p = all[(size_t) index];
    undoManager.beginNewTransaction("Preset");
    resetAll(true);
    setGlobal(CincelIDs::mode, (float) p.mode);
    int i = 0;
    for (const auto& pb : p.bands)
    {
        BandParams b;
        b.used = true;
        b.shape = pb.shape;
        b.freq = pb.freq;
        b.gainDb = pb.gain;
        b.q = pb.q;
        b.slope = pb.slope;
        b.place = pb.place;
        b.dynamic = pb.dyn;
        b.rangeDb = pb.range;
        b.thresholdDb = pb.thr;
        b.spectral = pb.spectral;
        writeBand(i++, b);
    }
    currentPresetName = p.name;
}

juce::File DecimulCincelProcessor::userPresetFolder() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("decimulabs").getChildFile("Cincel Presets");
    dir.createDirectory();
    return dir;
}

bool DecimulCincelProcessor::saveUserPreset(const juce::File& file)
{
    auto state = apvts.copyState();
    if (auto ui = state.getChildWithName("UI"); ui.isValid())
        state.removeChild(ui, nullptr);
    if (auto xml = state.createXml())
        if (xml->writeTo(file))
        {
            currentPresetName = file.getFileNameWithoutExtension();
            return true;
        }
    return false;
}

bool DecimulCincelProcessor::loadPresetFile(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return false;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (auto ui = tree.getChildWithName("UI"); ui.isValid())
        tree.removeChild(ui, nullptr);
    tree.addChild(uiState().createCopy(), -1, nullptr);
    apvts.replaceState(tree);
    currentPresetName = file.getFileNameWithoutExtension();
    return true;
}

void DecimulCincelProcessor::switchAB()
{
    abStates[abSlot] = apvts.copyState();
    abSlot ^= 1;
    if (abStates[abSlot].isValid())
    {
        auto tree = abStates[abSlot].createCopy();
        if (auto ui = tree.getChildWithName("UI"); ui.isValid())
            tree.removeChild(ui, nullptr);
        tree.addChild(uiState().createCopy(), -1, nullptr);
        apvts.replaceState(tree);
    }
}

void DecimulCincelProcessor::copyCurrentToOther()
{
    abStates[abSlot ^ 1] = apvts.copyState();
}

//==============================================================================
juce::AudioProcessorEditor* DecimulCincelProcessor::createEditor()
{
    return new DecimulCincelEditor(*this);
}

void DecimulCincelProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("preset", currentPresetName, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DecimulCincelProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            currentPresetName = tree.getProperty("preset", "Init").toString();
            apvts.replaceState(tree);
            reserveSpectral.store((bool) uiState().getProperty("specReserve", false));
        }
}

void DecimulCincelProcessor::updateTrackProperties(const TrackProperties& properties)
{
    if (properties.name.isNotEmpty())
        trackName = properties.name;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DecimulCincelProcessor();
}
