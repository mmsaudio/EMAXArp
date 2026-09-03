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
      - Note-ons landing within a small "played together" window (~30 ms)
        join the current phrase, so chords played by hand form one sequence.
      - The phrase starts once the window has closed, so a chord begins as
        a complete, correctly ordered pattern (~30 ms after the last note-on).
      - A note-on after that window REPLACES the phrase completely: the
        previous sequence is dropped, even if its keys are still held down
        (their later note-offs simply fall through).
      - When the last key is released the last sequence keeps looping for
        holdSeconds; at holdSeconds the arp silences itself. With holdLatch
        it loops forever until a new note replaces it.

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

    std::vector<int> phrase;            // notes of the current phrase, as played
    std::array<int, 128> keyCounts {};  // physical key-down count per pitch
    std::vector<int> sequence;          // expanded arp notes; frozen while holding
    int step = 0;                       // index into sequence
    int activeNote = -1;                // note currently sounding
    int lastVelocity = 100;

    int samplesToNextTick = -1;         // -1: clock stopped
    int gateCountdown = -1;             // -1: no note-off scheduled
    int holdCountdown = -1;             // -1: not sustaining
    int stepSamples = 11025;
    int gateSamples = 5512;
    int holdSamples = 0;
    bool holdLatch = false;

    bool pendingFire = false;           // phrase armed, waiting for the window to close
    juce::int64 fireAtAbs = 0;          // absolute sample position of that start

    // note-ons within this span count as "played together"; past it, a new
    // note-on replaces the whole phrase. ~30 ms, recomputed in prepare().
    int chordWindowSamples = 1323;

    juce::int64 blockStartAbs = 0;      // absolute sample counter, for the window
    juce::int64 lastNoteOnAbs = -(1 << 30);

    juce::Random random;
};
