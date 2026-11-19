#include "PluginProcessor.h"

//==============================================================================
// VOZ
//==============================================================================
float DubVoice::polyBlep (float t, float dt)
{
    if (t < dt)         { t /= dt;           return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)  { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

void DubVoice::startNote (int midiNote, float velocity, juce::SynthesiserSound*, int)
{
    const double sr = getSampleRate();

    if (sr != preparedSr)
    {
        preparedSr = sr;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sr;
        spec.maximumBlockSize = 512;
        spec.numChannels = 1;
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        amp.setSampleRate (sr);
        fenv.setSampleRate (sr);
    }

    baseInc = (float) (juce::MidiMessage::getMidiNoteInHertz (midiNote) / sr);
    level   = velocity * 0.22f;

    auto& rng = juce::Random::getSystemRandom();
    phase1 = rng.nextFloat();
    phase2 = rng.nextFloat();

    filter.reset();
    amp.noteOn();
    fenv.noteOn();
}

void DubVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        amp.noteOff();
        fenv.noteOff();
    }
    else
    {
        amp.reset();
        fenv.reset();
        clearCurrentNote();
    }
}

void DubVoice::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    amp.setParameters  ({ get ("atk"), get ("dec"), get ("sus"), get ("rel") });
    fenv.setParameters ({ 0.003f, get ("fdec"), 0.0f, 0.2f });

    const float ratio   = std::pow (2.0f, get ("detune") / 1200.0f);
    const float cutoff  = get ("cutoff");
    const float envAmt  = get ("envamt");
    const float inc2    = baseInc * ratio;

    filter.setResonance (get ("res"));

    for (int i = 0; i < numSamples; ++i)
    {
        const float a = amp.getNextSample();

        if (! amp.isActive())
        {
            clearCurrentNote();
            break;
        }

        const float fe = fenv.getNextSample();

        phase1 += baseInc; if (phase1 >= 1.0f) phase1 -= 1.0f;
        phase2 += inc2;    if (phase2 >= 1.0f) phase2 -= 1.0f;

        const float saw1 = (2.0f * phase1 - 1.0f) - polyBlep (phase1, baseInc);
        const float saw2 = (2.0f * phase2 - 1.0f) - polyBlep (phase2, inc2);
        const float x = 0.5f * (saw1 + saw2);

        const float cut = juce::jlimit (20.0f, 18000.0f, cutoff * std::pow (2.0f, envAmt * fe));
        filter.setCutoffFrequency (cut);
        const float y = filter.processSample (0, x) * a * level;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, startSample + i, y);
    }
}

//==============================================================================
// PLUGIN
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ParticulaDubAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterChoice;
    using juce::NormalisableRange;
    using juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto f = [&l] (const char* id, const char* name, float lo, float hi, float def, float skew = 1.0f)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (lo, hi, 0.0f, skew), def));
    };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chord", 1 }, "Chord",
           juce::StringArray { "Off", "m7", "m9", "m11", "sus m7" }, 1));

    f ("detune", "Detune (cents)", 0.0f, 40.0f, 12.0f);
    f ("cutoff", "Cutoff",         60.0f, 12000.0f, 700.0f, 0.35f);
    f ("res",    "Resonance",      0.5f, 6.0f, 1.2f);
    f ("envamt", "Filter Env (oct)", 0.0f, 6.0f, 2.5f);
    f ("fdec",   "Filter Decay",   0.02f, 2.0f, 0.35f, 0.5f);

    f ("atk", "Attack",  0.001f, 2.0f, 0.005f, 0.4f);
    f ("dec", "Decay",   0.02f,  3.0f, 0.6f,   0.5f);
    f ("sus", "Sustain", 0.0f,   1.0f, 0.0f);
    f ("rel", "Release", 0.02f,  4.0f, 0.5f,   0.5f);

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dtime", 1 }, "Delay Time",
           juce::StringArray { "1/16", "1/8", "1/8.", "1/4", "1/4." }, 2));
    f ("dfb",   "Delay Feedback", 0.0f, 0.95f, 0.65f);
    f ("dtone", "Delay Tone",     400.0f, 9000.0f, 2200.0f, 0.4f);
    f ("dmix",  "Delay Mix",      0.0f, 1.0f, 0.45f);

    f ("rsize", "Reverb Size", 0.0f, 1.0f, 0.8f);
    f ("rmix",  "Reverb Mix",  0.0f, 1.0f, 0.3f);

    f ("gain", "Output (dB)", -24.0f, 6.0f, 0.0f);

    return l;
}

ParticulaDubAudioProcessor::ParticulaDubAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < 16; ++i)
        synth.addVoice (new DubVoice (apvts));

    synth.addSound (new DubSound());
}

bool ParticulaDubAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ParticulaDubAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 1;

    for (auto& d : delay)
    {
        d.setMaximumDelayInSamples ((int) (sampleRate * 3.0));
        d.prepare (spec);
        d.reset();
    }

    fbState[0] = fbState[1] = 0.0f;
    smoothTime.reset (sampleRate, 0.15);
    smoothTime.setCurrentAndTargetValue (0.4f * (float) sampleRate);

    reverb.setSampleRate (sampleRate);
    reverb.reset();
}

void ParticulaDubAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // ---- Chord: un nota -> acorde ----
    static const std::vector<std::vector<int>> chords {
        { 0 }, { 0, 3, 7, 10 }, { 0, 3, 10, 14 }, { 0, 3, 7, 10, 17 }, { 0, 5, 7, 10 } };

    const int chordIdx = (int) get ("chord");
    juce::MidiBuffer chordMidi;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (chordIdx > 0 && (msg.isNoteOn() || msg.isNoteOff()))
        {
            for (int iv : chords[(size_t) chordIdx])
            {
                const int note = msg.getNoteNumber() + iv;
                if (note > 127) continue;

                chordMidi.addEvent (msg.isNoteOn() ? juce::MidiMessage::noteOn (msg.getChannel(), note, msg.getVelocity())
                                                   : juce::MidiMessage::noteOff (msg.getChannel(), note, 0.0f),
                                    meta.samplePosition);
            }
        }
        else
        {
            chordMidi.addEvent (msg, meta.samplePosition);
        }
    }

    synth.renderNextBlock (buffer, chordMidi, 0, n);

    // ---- Dub delay (feedback oscuro + saturacion suave) ----
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    static const float beats[] { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f };
    const float beatSec   = 60.0f / (float) juce::jmax (30.0, bpm);
    const float maxSamp   = (float) (sr * 2.9);
    smoothTime.setTargetValue (juce::jmin (maxSamp, beats[(int) get ("dtime")] * beatSec * (float) sr));

    const float fb   = get ("dfb");
    const float mix  = get ("dmix");
    const float coef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * get ("dtone") / (float) sr);

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float tL = smoothTime.getNextValue();
        const float tR = tL * 0.75f;
        float* chans[2] { left, right };
        const float times[2] { tL, tR };

        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = chans[ch][i];
            const float wet = delay[ch].popSample (0, times[ch]);

            fbState[ch] += coef * (wet - fbState[ch]);
            delay[ch].pushSample (0, std::tanh (dry + fbState[ch] * fb));

            chans[ch][i] = dry + wet * mix;
        }
    }

    // ---- Reverb ----
    juce::Reverb::Parameters rp;
    rp.roomSize = get ("rsize");
    rp.damping  = 0.6f;
    rp.wetLevel = get ("rmix");
    rp.dryLevel = 1.0f;
    rp.width    = 1.0f;
    reverb.setParameters (rp);
    reverb.processStereo (left, right, n);

    // ---- Salida ----
    buffer.applyGain (juce::Decibels::decibelsToGain (get ("gain")));
}

void ParticulaDubAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ParticulaDubAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParticulaDubAudioProcessor();
}
