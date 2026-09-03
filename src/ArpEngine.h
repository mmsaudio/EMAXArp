#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

//==============================================================================
/** Monophonic arpeggiator engine, EMAX-style input handling.

    Feed it the incoming MIDI stream, it returns the arpeggiated stream.

    Input model:
      - A phrase is every note played while at least one key is physically
        held: overlapping presses accumulate into the sequence, so a triad
        pressed with a little spread stays one phrase, and re-pressing a
        note records it again (A4, G4, then A4 pressed again while G4 is
        still down -> A4 G4 A4). The sequence cycles in the selected mode
        (e.g. "as played": the notes in playing order, then the same notes
        one octave up, and so on).
      - When the last key is released the phrase keeps looping for
        holdSeconds (holdLatch: forever). Note-offs never remove notes from
        the phrase -- releasing keys does not shrink the pattern.
      - A note played while nothing is held starts a NEW phrase and the
        previous sequence is dropped completely: that is how the held loop
        gets replaced. A duplicate note-on for a key still down is ignored.

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
        float rateHz  = 4.0f;     // steps per second
        float gate    = 0.5f;     // note length as fraction of a step
        int   octaves = 1;        // extra octaves stacked on top
        float holdSeconds = 0.0f; // how long the sequence loops after release
        bool  holdLatch   = false;// true: loop forever until replaced
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

    std::vector<int> phrase;            // notes played since the last full release
    std::array<int, 128> keyCounts {};  // physical key-down count per pitch
    int heldKeyCount = 0;               // total physical keys currently down
    bool sustaining = false;            // all keys up, phrase still looping
    std::vector<int> sequence;          // expanded arp notes for the current phrase
    int step = 0;                       // index into sequence
    int activeNote = -1;                // note currently sounding
    int lastVelocity = 100;

    int samplesToNextTick = -1;         // -1: clock stopped
    int gateCountdown = -1;             // -1: no note-off scheduled
    int holdCountdown = -1;             // -1: not on a timed hold
    int stepSamples = 11025;
    int gateSamples = 5512;
    int holdSamples = 0;
    bool holdLatch = false;

    juce::Random random;
};
