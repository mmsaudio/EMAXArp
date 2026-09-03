#include "ArpEngine.h"

namespace
{
    constexpr int arpChannel = 1;       // all arp notes go out on MIDI channel 1
    constexpr int maxPhraseLength = 256;

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
    random.setSeedRandomly();
    reset();
}

void ArpEngine::reset()
{
    phrase.clear();
    keyCounts.fill (0);
    heldKeyCount = 0;
    sustaining = false;
    sequence.clear();
    step = 0;
    activeNote = -1;
    samplesToNextTick = -1;
    gateCountdown = -1;
    holdCountdown = -1;
}

void ArpEngine::setParameters (const Params& newParams)
{
    params = newParams;

    stepSamples = juce::jmax (1, (int) (sampleRate / (double) juce::jmax (0.01f, params.rateHz)));
    gateSamples = juce::jlimit (1, stepSamples,
                                (int) ((double) stepSamples * juce::jlimit (0.0f, 1.0f, params.gate)));
    holdSamples = juce::jmax (0, (int) (params.holdSeconds * sampleRate));
    holdLatch   = params.holdLatch;

    // Re-deriving the sequence from the (unchanged) phrase is harmless while
    // playing or sustaining, and lets mode/octave changes apply live.
    rebuildSequence();
}

//==============================================================================
void ArpEngine::process (const juce::MidiBuffer& midiIn, juce::MidiBuffer& midiOut, int numSamples)
{
    // ---- pass 1: note intake ------------------------------------------------
    for (const auto metadata : midiIn)
    {
        const auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (keyCounts[(size_t) note] > 0)
                continue;   // duplicate note-on, key already down

            if (heldKeyCount == 0)
            {
                // New phrase: the first note played while nothing is held
                // replaces whatever was looping (hold) or silent, even though
                // the held sequence may still be sounding.
                stopNote (midiOut, pos);
                phrase.clear();
                step = 0;
                sustaining = false;
                holdCountdown = -1;
                samplesToNextTick = -1;
                gateCountdown = -1;
            }

            // every press is recorded, repeats included: A4, G4, then A4
            // pressed again while G4 is still down stays one phrase A4 G4 A4
            if (phrase.size() >= maxPhraseLength)
                phrase.erase (phrase.begin());   // keep the most recent playing
            phrase.push_back (note);
            keyCounts[(size_t) note]++;
            heldKeyCount++;
            lastVelocity = msg.getVelocity();
            rebuildSequence();

            if (samplesToNextTick < 0 && ! sequence.empty())
            {
                // phrase start: the first note sounds immediately, the clock
                // then cycles the sequence from its next entry
                step = 0;
                tick (midiOut, pos);
                samplesToNextTick = pos + stepSamples;
                if (gateCountdown >= 0)
                    gateCountdown += pos;
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            auto& count = keyCounts[(size_t) note];
            if (count > 0)
            {
                count--;
                if (count == 0)
                    heldKeyCount--;
            }

            if (heldKeyCount > 0 || sustaining || sequence.empty() || samplesToNextTick < 0)
                continue;

            // last key released: keep the phrase looping (hold) or stop (hold=0)
            if (holdLatch)
            {
                sustaining = true;                  // loop forever until replaced
            }
            else if (holdSamples > 0)
            {
                sustaining = true;
                holdCountdown = holdSamples + pos;
            }
            else
            {
                stopNote (midiOut, pos);
                sequence.clear();
                phrase.clear();
                samplesToNextTick = -1;
                gateCountdown = -1;
                step = 0;
            }
        }

        // other messages (CC, pitch bend...) are consumed, not forwarded
    }

    // ---- pass 2: tick / gate / hold-expiry timeline --------------------------
    constexpr int never = std::numeric_limits<int>::max();
    int pos = 0;

    while (pos < numSamples)
    {
        const bool haveTick = samplesToNextTick >= 0 && ! sequence.empty();
        const bool haveGate = gateCountdown >= 0;
        const bool haveHold = holdCountdown >= 0;
        if (! haveTick && ! haveGate && ! haveHold)
            break;

        const int toTick = haveTick ? samplesToNextTick : never;
        const int toGate = haveGate ? gateCountdown : never;
        const int toHold = haveHold ? holdCountdown : never;
        const int next = std::min ({ toTick, toGate, toHold });
        const int remaining = numSamples - pos;

        if (next >= remaining)
        {
            // nothing fires within this block: carry the countdowns over
            if (haveTick) samplesToNextTick -= remaining;
            if (haveGate) gateCountdown  -= remaining;
            if (haveHold) holdCountdown  -= remaining;
            break;
        }

        pos += next;
        if (haveTick) samplesToNextTick -= next;
        if (haveGate) gateCountdown  -= next;
        if (haveHold) holdCountdown  -= next;

        if (toHold == next)
        {
            // hold ran out: silence and forget the phrase
            stopNote (midiOut, pos);
            sequence.clear();
            phrase.clear();
            step = 0;
            sustaining = false;
            samplesToNextTick = -1;
            gateCountdown = -1;
            holdCountdown = -1;
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
        return;

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
        step = index + 1;   // no modulo here: if the phrase keeps growing the
                            // cycle continues from the next entry; rebuildSequence()
                            // wraps it into range

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
