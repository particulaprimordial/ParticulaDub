#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
struct DubSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
// Voz: 2 sierras detuneadas (PolyBLEP) -> filtro LP resonante con envolvente -> ADSR
class DubVoice : public juce::SynthesiserVoice
{
public:
    explicit DubVoice (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<DubSound*> (s) != nullptr; }
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    static float polyBlep (float t, float dt);

    juce::AudioProcessorValueTreeState& apvts;
    juce::ADSR amp, fenv;
    juce::dsp::StateVariableTPTFilter<float> filter;
    double preparedSr = 0.0;
    float phase1 = 0.f, phase2 = 0.f, baseInc = 0.f, level = 0.f;
};

//==============================================================================
class ParticulaDubAudioProcessor : public juce::AudioProcessor
{
public:
    ParticulaDubAudioProcessor();
    ~ParticulaDubAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Particula Dub"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::Synthesiser synth;

    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    Delay delay[2];
    float fbState[2] { 0.f, 0.f };
    juce::SmoothedValue<float> smoothTime;

    juce::Reverb reverb;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParticulaDubAudioProcessor)
};
