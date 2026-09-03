#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <limits>
#include <vector>

//==============================================================================
/** Simple monophonic arpeggiator engine.

    Feed it the incoming MIDI stream, it returns the arpeggiated stream.
    Held input notes form the arp sequence; an internal clock (rate in Hz)
    steps through the sequence, retriggering notes with a gate length.

    Extension points for the EMAX behaviour:
      - new pattern modes: extend Mode + rebuildSequence()/tick()
      - new parameters:    extend Params + createParameterLayout() in the processor
*/
class ArpEngine
{
public:
    enum class Mode { asPlayed = 0, up, down, upDown, random };
    static constexpr int numModes = 5;

    struct Params
    {
        int   mode    = (int) Mode::up;
        float rateHz  = 4.0f;   // steps per second
        float gate    = 0.5f;   // note length as fraction of a step
        int   octaves = 1;      // extra octaves stacked on top
    };

    void prepare (double sampleRate);
    void reset();

    void setParameters (const Params& newParams);

    /** Consumes incoming note events and writes the arpeggiated result to midiOut. */
    void process (const juce::MidiBuffer& midiIn, juce::MidiBuffer& midiOut, int numSamples);

private:
    //==============================================================================
    void rebuildSequence();
    void tick (juce::MidiBuffer& out, int pos);
    void stopNote (juce::MidiBuffer& out, int pos);

    double sampleRate = 44100.0;
    Params params;

    std::vector<int> heldNotes;   // input notes currently down, in "as played" order
    std::vector<int> sequence;    // expanded arp notes for the current mode
    int step = 0;                 // index into sequence
    int activeNote = -1;          // note currently sounding
    int lastVelocity = 100;

    int samplesToNextTick = -1;   // -1: clock stopped
    int gateCountdown = -1;       // -1: no note-off scheduled
    int stepSamples = 11025;
    int gateSamples = 5512;

    juce::Random random;
};
