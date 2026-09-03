#include "ArpEngine.h"

namespace
{
    constexpr int arpChannel = 1;   // all arp notes go out on MIDI channel 1

    void addNoteOn (juce::MidiBuffer& out, int pos, int note, int velocity)
    {
        out.addEvent (juce::MidiMessage::noteOn (arpChannel, note,
                      (juce::uint8) juce::jlimit (1, 127, velocity)), pos);
    }

    void addNoteOff (juce::MidiBuffer& out, int pos, int note)
    {
        out.addEvent (juce::MidiMessage::noteOff (arpChannel, note), pos);
    }

    int clampNote (int note) { return juce::jlimit (0, 127, note); }
}

//==============================================================================
void ArpEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    chordWindowSamples = juce::jmax (1, (int) (0.030 * newSampleRate + 0.5));
    random.setSeedRandomly();
    reset();
}

void ArpEngine::reset()
{
    phrase.clear();
    keyCounts.fill (0);
    sequence.clear();
    step = 0;
    activeNote = -1;
    samplesToNextTick = -1;
    gateCountdown = -1;
    holdCountdown = -1;
    pendingFire = false;
    blockStartAbs = 0;
    lastNoteOnAbs = -(1 << 30);
}

void ArpEngine::setParameters (const Params& newParams)
{
    params = newParams;

    stepSamples = juce::jmax (1, (int) (sampleRate / (double) juce::jmax (0.01f, params.rateHz)));
    gateSamples = juce::jlimit (1, stepSamples,
                                (int) ((double) stepSamples * juce::jlimit (0.0f, 1.0f, params.gate)));
    holdSamples = juce::jmax (0, (int) (params.holdSeconds * sampleRate));
    holdLatch   = params.holdLatch;

    // While sustaining, phrase is empty and sequence is a frozen snapshot:
    // rebuildSequence() leaves it alone, so rate/gate stay live and the
    // pattern only changes when a new phrase starts.
    rebuildSequence();
}

//==============================================================================
void ArpEngine::process (const juce::MidiBuffer& midiIn, juce::MidiBuffer& midiOut, int numSamples)
{
    const juce::int64 absStart = blockStartAbs;

    // ---- pass 1: note intake ------------------------------------------------
    // A note-on inside the chord window joins the current phrase; past the
    // window it starts a new one and the previous sequence is dropped
    // completely. The phrase only starts playing once the window has closed,
    // so a chord played by hand begins as a complete sequence.
    for (const auto metadata : midiIn)
    {
        const auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (std::find (phrase.begin(), phrase.end(), note) != phrase.end())
                continue;   // duplicate note-on, ignore

            const juce::int64 absEvent = absStart + pos;
            const bool chordMate = ! phrase.empty()
                                && (absEvent - lastNoteOnAbs) <= chordWindowSamples;

            if (! chordMate)
            {
                // New phrase: the previous sequence is dropped completely,
                // even though its keys may still be down -- their note-offs
                // are tracked by keyCounts and will simply fall through.
                stopNote (midiOut, pos);
                phrase.clear();
                step = 0;
                holdCountdown = -1;
                samplesToNextTick = -1;
                gateCountdown = -1;
            }

            phrase.push_back (note);
            keyCounts[(size_t) note]++;
            lastVelocity = msg.getVelocity();
            lastNoteOnAbs = absEvent;

            // (re)arm the start: it fires once the window has closed, and a
            // chord mate arriving later rolls that start forward
            pendingFire = true;
            fireAtAbs = absEvent + chordWindowSamples;

            rebuildSequence();
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            auto& count = keyCounts[(size_t) note];
            if (count > 0)
                count--;

            if (count > 0)
                continue;

            // this was the last press of that pitch: drop it from the phrase
            const auto it = std::find (phrase.begin(), phrase.end(), note);
            if (it == phrase.end())
                continue;   // stale note-off (pitch already replaced)
            phrase.erase (it);

            if (! phrase.empty())
            {
                rebuildSequence();   // phrase follows the keys still held
                continue;
            }

            // last key released: either hold the sequence...
            if (holdLatch || holdSamples > 0)
                holdCountdown = holdLatch ? -1 : holdSamples + pos;
            // ...or stop right away (hold at zero)
            else
            {
                stopNote (midiOut, pos);
                sequence.clear();
                samplesToNextTick = -1;
                gateCountdown = -1;
                step = 0;
                pendingFire = false;
            }
        }

        // other messages (CC, pitch bend...) are consumed, not forwarded
    }

    blockStartAbs += numSamples;

    // ---- pass 2: fire / tick / gate / hold-expiry timeline -------------------
    constexpr int never = std::numeric_limits<int>::max();

    auto fireStart = [&] (int pos)
    {
        step = 0;
        tick (midiOut, pos);            // plays the pattern start, arms the gate
        samplesToNextTick = stepSamples;
    };

    int pos = 0;

    while (pos < numSamples)
    {
        const bool haveFire = pendingFire && ! sequence.empty();
        const bool haveTick = samplesToNextTick >= 0 && ! sequence.empty();
        const bool haveGate = gateCountdown >= 0;
        const bool haveHold = holdCountdown >= 0;
        if (! haveFire && ! haveTick && ! haveGate && ! haveHold)
            break;

        const int toFire = haveFire
            ? juce::jlimit (0, never, (int) (fireAtAbs - absStart - pos)) : never;
        const int toTick = haveTick ? samplesToNextTick : never;
        const int toGate = haveGate ? gateCountdown : never;
        const int toHold = haveHold ? holdCountdown : never;
        const int next = std::min ({ toFire, toTick, toGate, toHold });
        const int remaining = numSamples - pos;

        if (next >= remaining)
        {
            // nothing fires within this block; toFire is absolute, the rest carry
            if (haveTick) samplesToNextTick -= remaining;
            if (haveGate) gateCountdown  -= remaining;
            if (haveHold) holdCountdown  -= remaining;
            break;
        }

        pos += next;
        if (haveTick) samplesToNextTick -= next;
        if (haveGate) gateCountdown  -= next;
        if (haveHold) holdCountdown  -= next;

        if (toFire == next)
        {
            pendingFire = false;
            fireStart (pos);
        }
        else if (toHold == next)
        {
            // hold ran out: silence and forget the sequence
            stopNote (midiOut, pos);
            sequence.clear();
            step = 0;
            samplesToNextTick = -1;
            gateCountdown = -1;
            holdCountdown = -1;
            pendingFire = false;
        }
        else if (toTick <= toGate)
        {
            // tick first: it closes the previous note and re-arms the gate,
            // which makes a gate landing on the same sample redundant
            tick (midiOut, pos);
            samplesToNextTick = stepSamples;
        }
        else
        {
            stopNote (midiOut, pos);
            gateCountdown = -1;
        }
    }
}

//==============================================================================
void ArpEngine::rebuildSequence()
{
    if (phrase.empty())
        return;   // keep any frozen sequence (sustain) untouched

    const auto mode = (Mode) params.mode;
    std::vector<int> notes (phrase);

    if (mode == Mode::up || mode == Mode::upDown)
        std::sort (notes.begin(), notes.end());
    else if (mode == Mode::down)
        std::sort (notes.begin(), notes.end(), std::greater<int>());

    sequence.clear();

    const int octaves = juce::jlimit (0, 8, params.octaves);
    for (int i = 0; i <= octaves; ++i)
    {
        const int octave = (mode == Mode::down) ? (octaves - i) : i;
        for (auto note : notes)
            sequence.push_back (clampNote (note + 12 * octave));
    }

    // ping-pong: mirror the ascending run without repeating its endpoints
    if (mode == Mode::upDown && sequence.size() > 2)
        for (int i = (int) sequence.size() - 2; i >= 1; --i)
            sequence.push_back (sequence[(size_t) i]);

    if (! sequence.empty())
        step = step % (int) sequence.size();
}

void ArpEngine::tick (juce::MidiBuffer& out, int pos)
{
    if (sequence.empty())
        return;

    stopNote (out, pos);

    const auto mode = (Mode) params.mode;
    int index;
    if (mode == Mode::random && sequence.size() > 1)
        index = random.nextInt ((int) sequence.size());
    else
        index = step % (int) sequence.size();

    activeNote = sequence[(size_t) index];
    addNoteOn (out, pos, activeNote, lastVelocity);

    if (mode != Mode::random)
        step = (index + 1) % (int) sequence.size();

    // with a full-length gate the next tick's note-off closes the note instead
    gateCountdown = (gateSamples < stepSamples) ? gateSamples : -1;
}

void ArpEngine::stopNote (juce::MidiBuffer& out, int pos)
{
    if (activeNote >= 0)
    {
        addNoteOff (out, pos, activeNote);
        activeNote = -1;
    }
}
