#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include "FactoryPresets.h"

namespace
{
    // Orden de los operadores en los registros del chip: +0 = op1, +8 = op2, +4 = op3, +12 = op4.
    constexpr int regOffset[4] = { 0, 8, 4, 12 };
    constexpr double chipRate = 53267.0;

    juce::String opId(int op, const char* name) { return "op" + juce::String(op + 1) + "_" + name; }

    using factory::presets;
    constexpr int numPresets = factory::count;
}

DecimulFMProcessor::DecimulFMProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    written.fill(-1);
    ym2612_reset(&chip, 48000.0);
    setCurrentProgram(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout DecimulFMProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    auto intP = [&](const String& id, const String& name, int lo, int hi, int def)
    { ps.push_back(std::make_unique<AudioParameterInt>(ParameterID { id, 1 }, name, lo, hi, def)); };

    ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "alg", 1 }, "Algorithm",
        StringArray { "1", "2", "3", "4", "5", "6", "7", "8" }, 4));
    intP("fb", "Feedback", 0, 7, 4);
    ps.push_back(std::make_unique<AudioParameterBool>(ParameterID { "lfoOn", 1 }, "LFO on", false));
    ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { "lfoFreq", 1 }, "LFO rate",
        StringArray { "3.98 Hz", "5.56 Hz", "6.02 Hz", "6.37 Hz", "6.88 Hz", "9.63 Hz", "48.1 Hz", "72.2 Hz" }, 0));
    intP("ams", "Tremolo depth (AMS)", 0, 3, 0);
    intP("pms", "Vibrato depth (PMS)", 0, 7, 0);
    ps.push_back(std::make_unique<AudioParameterBool>(ParameterID { "ladder", 1 }, "DAC distortion (MD1)", true));
    ps.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "gain", 1 }, "Output",
        NormalisableRange<float>(-24.0f, 18.0f, 0.1f), 9.0f, AudioParameterFloatAttributes().withLabel("dB")));
    intP("bendRange", "Pitch bend range", 0, 12, 2);

    const StringArray dtNames { "0", "+1", "+2", "+3", "-0", "-1", "-2", "-3" };
    const StringArray ssgNames { "off", "\\\\\\\\", "\\___", "\\/\\/", "\\---", "////", "/---", "/\\/\\", "/___" };
    for (int o = 0; o < 4; ++o)
    {
        const String n = "Op" + String(o + 1) + " ";
        intP(opId(o, "ar"), n + "Attack", 0, 31, 31);
        intP(opId(o, "d1r"), n + "Decay 1", 0, 31, 0);
        intP(opId(o, "sl"), n + "Sustain level", 0, 15, 0);
        intP(opId(o, "d2r"), n + "Decay 2", 0, 31, 0);
        intP(opId(o, "rr"), n + "Release", 0, 15, 8);
        intP(opId(o, "tl"), n + "Level (0 = loud)", 0, 127, o == 3 ? 0 : 127);
        intP(opId(o, "ks"), n + "Key scale", 0, 3, 0);
        intP(opId(o, "mul"), n + "Multiple", 0, 15, 1);
        ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { opId(o, "dt"), 1 }, n + "Detune", dtNames, 0));
        ps.push_back(std::make_unique<AudioParameterBool>(ParameterID { opId(o, "am"), 1 }, n + "AM on", false));
        ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { opId(o, "ssg"), 1 }, n + "SSG-EG", ssgNames, 0));
    }
    return { ps.begin(), ps.end() };
}

int DecimulFMProcessor::getNumPrograms() { return numPresets; }
const juce::String DecimulFMProcessor::getProgramName(int i) { return juce::isPositiveAndBelow(i, numPresets) ? juce::String::fromUTF8(presets[i].name) : juce::String(); }

void DecimulFMProcessor::setCurrentProgram(int index)
{
    if (! juce::isPositiveAndBelow(index, numPresets)) return;
    currentProgram = index;
    const auto& pr = presets[index];
    auto set = [&](const juce::String& id, float plain)
    {
        if (auto* prm = apvts.getParameter(id))
            prm->setValueNotifyingHost(prm->convertTo0to1(plain));
    };
    set("alg", (float) pr.alg); set("fb", (float) pr.fb);
    set("lfoOn", (float) pr.lfoOn); set("lfoFreq", (float) pr.lfoFreq); set("ams", (float) pr.ams); set("pms", (float) pr.pms);
    static const char* keys[9] = { "ar", "d1r", "d2r", "rr", "sl", "tl", "ks", "mul", "dt" };
    for (int o = 0; o < 4; ++o)
    {
        for (int k = 0; k < 9; ++k) set(opId(o, keys[k]), (float) pr.ops[o][k]);
        set(opId(o, "am"), (float) pr.ops[o][9]);
        set(opId(o, "ssg"), (float) (pr.ops[o][10] ? pr.ops[o][10] - 7 : 0));
    }
}

void DecimulFMProcessor::prepareToPlay(double sampleRate, int)
{
    ym2612_reset(&chip, sampleRate);
    written.fill(-1);
    for (auto& v : voices) v = {};
    smGain.reset(sampleRate, 0.03);
    smGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(p("gain")));
    syncRegisters(true);
}

bool DecimulFMProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DecimulFMProcessor::writeReg(int port, int addr, int value)
{
    const int k = (port << 8) | addr;
    if (written[(size_t) k] == value) return;
    written[(size_t) k] = value;
    ym2612_write(&chip, port, addr, value);
    if (port == 0) regView[(size_t) addr].store((uint8_t) value);
}

/** Lleva los parámetros a los registros de los 6 canales (solo escribe lo que cambió). */
void DecimulFMProcessor::syncRegisters(bool force)
{
    if (force) written.fill(-1);
    ym2612_ladder(&chip, p("ladder") > 0.5f ? 1 : 0);
    writeReg(0, 0x22, p("lfoOn") > 0.5f ? (8 | (int) p("lfoFreq")) : 0);

    const int alg = (int) p("alg"), fb = (int) p("fb"), ams = (int) p("ams"), pms = (int) p("pms");
    int opRegs[4][7];
    for (int o = 0; o < 4; ++o)
    {
        opRegs[o][0] = ((int) p(opId(o, "dt")) << 4) | (int) p(opId(o, "mul"));
        opRegs[o][1] = (int) p(opId(o, "tl"));
        opRegs[o][2] = ((int) p(opId(o, "ks")) << 6) | (int) p(opId(o, "ar"));
        opRegs[o][3] = ((p(opId(o, "am")) > 0.5f ? 1 : 0) << 7) | (int) p(opId(o, "d1r"));
        opRegs[o][4] = (int) p(opId(o, "d2r"));
        opRegs[o][5] = ((int) p(opId(o, "sl")) << 4) | (int) p(opId(o, "rr"));
        const int ssg = (int) p(opId(o, "ssg"));
        opRegs[o][6] = ssg ? 7 + ssg : 0;
    }
    for (int c = 0; c < 6; ++c)
    {
        const int port = c < 3 ? 0 : 1, cc = c % 3;
        writeReg(port, 0xB0 + cc, alg | (fb << 3));
        writeReg(port, 0xB4 + cc, 0xC0 | (ams << 4) | pms);
        for (int o = 0; o < 4; ++o)
            for (int r = 0; r < 7; ++r)
                writeReg(port, 0x30 + r * 0x10 + regOffset[o] + cc, opRegs[o][r]);
    }
}

void DecimulFMProcessor::setPitch(int c)
{
    const double f = 440.0 * std::pow(2.0, (voices[(size_t) c].note - 69 + bendSemis) / 12.0);
    int fnum = 2047, block = 7;
    for (int b = 0; b < 8; ++b)
    {
        const int fn = (int) std::lround(f * 1048576.0 / (chipRate * std::pow(2.0, b - 1)));
        if (fn < 2048) { fnum = fn; block = b; break; }
    }
    const int port = c < 3 ? 0 : 1, cc = c % 3;
    // A4 primero (se guarda) y después A0 (aplica), como en el chip.
    ym2612_write(&chip, port, 0xA4 + cc, (block << 3) | (fnum >> 8));
    ym2612_write(&chip, port, 0xA0 + cc, fnum & 0xFF);
}

static int keyByte(int c) { return c < 3 ? c : c + 1; }

void DecimulFMProcessor::noteOn(int note)
{
    int pick = -1;
    for (int c = 0; c < 6; ++c) if (voices[(size_t) c].note == note) pick = c;
    if (pick < 0)
    {
        uint32_t best = UINT32_MAX;
        for (int c = 0; c < 6; ++c) if (voices[(size_t) c].note < 0 && voices[(size_t) c].stamp < best) { best = voices[(size_t) c].stamp; pick = c; }
    }
    if (pick < 0)
    {
        uint32_t best = UINT32_MAX;
        for (int c = 0; c < 6; ++c) if (voices[(size_t) c].stamp < best) { best = voices[(size_t) c].stamp; pick = c; }
    }
    ym2612_write(&chip, 0, 0x28, keyByte(pick));                 // suelta, para que vuelva a atacar
    voices[(size_t) pick] = { note, ++stampCounter };
    setPitch(pick);
    ym2612_write(&chip, 0, 0x28, 0xF0 | keyByte(pick));
}

void DecimulFMProcessor::noteOff(int note)
{
    for (int c = 0; c < 6; ++c)
        if (voices[(size_t) c].note == note)
        {
            ym2612_write(&chip, 0, 0x28, keyByte(c));
            voices[(size_t) c] = { -1, ++stampCounter };
        }
}

void DecimulFMProcessor::allNotesOff()
{
    for (int c = 0; c < 6; ++c) if (voices[(size_t) c].note >= 0) noteOff(voices[(size_t) c].note);
}

void DecimulFMProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    syncRegisters(false);
    smGain.setTargetValue(juce::Decibels::decibelsToGain(p("gain")));

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    float monoR[4096];

    int pos = 0;
    auto renderTo = [&](int end)
    {
        while (pos < end)
        {
            const int k = juce::jmin(end - pos, 4096);
            ym2612_render(&chip, L + pos, R ? R + pos : monoR, k);
            if (! R) for (int i = 0; i < k; ++i) L[pos + i] = 0.5f * (L[pos + i] + monoR[i]);
            pos += k;
        }
    };

    for (const auto meta : midi)
    {
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn()) noteOn(m.getNoteNumber());
        else if (m.isNoteOff()) noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
        else if (m.isPitchWheel())
        {
            bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * p("bendRange");
            for (int c = 0; c < 6; ++c) if (voices[(size_t) c].note >= 0) setPitch(c);
        }
        else if (m.isProgramChange()) { setCurrentProgram(m.getProgramChangeNumber() % numPresets); syncRegisters(false); }
    }
    renderTo(n);

    for (int i = 0; i < n; ++i)
    {
        const float g = smGain.getNextValue();
        L[i] *= g;
        if (R) R[i] *= g;
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);
}

void DecimulFMProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void DecimulFMProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml(*xml);
            currentProgram = vt.getProperty("program", 0);
            apvts.replaceState(vt);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DecimulFMProcessor(); }
