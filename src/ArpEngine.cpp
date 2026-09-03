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
    random.setSeedRandomly();
    reset();
}

void ArpEngine::reset()
{
    heldNotes.clear();
    sequence.clear();
    step = 0;
    activeNote = -1;
    samplesToNextTick = -1;
    gateCountdown = -1;
}

void ArpEngine::setParameters (const Params& newParams)
{
    params = newParams;

    stepSamples = juce::jmax (1, (int) (sampleRate / (double) juce::jmax (0.01f, params.rateHz)));
    gateSamples = juce::jlimit (1, stepSamples,
                                (int) ((double) stepSamples * juce::jlimit (0.0f, 1.0f, params.gate)));

    rebuildSequence();
}

//==============================================================================
void ArpEngine::process (const juce::MidiBuffer& midiIn, juce::MidiBuffer& midiOut, int numSamples)
{
    // ---- pass 1: consume input notes, maintain the held-note set ------------
    // The arp starts on the first held note, but only after every event at the
    // same position has been consumed, so a chord played as one event cluster
    // starts as a complete sequence.
    bool pendingStart = false;
    int pendingStartPos = 0;

    auto fireStart = [&] (int pos)
    {
        step = 0;
        tick (midiOut, pos);
        // countdowns are relative to the block start, so offset by pos
        samplesToNextTick = pos + stepSamples;
        if (gateCountdown >= 0)
            gateCountdown += pos;
    };

    for (const auto metadata : midiIn)
    {
        const auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;

        if (pendingStart && pos > pendingStartPos)
        {
            fireStart (pendingStartPos);
            pendingStart = false;
        }

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (std::find (heldNotes.begin(), heldNotes.end(), note) != heldNotes.end())
                continue;   // duplicate note-on, ignore

            const bool wasEmpty = heldNotes.empty();
            heldNotes.push_back (note);
            lastVelocity = msg.getVelocity();
            rebuildSequence();

            if (wasEmpty && ! sequence.empty())
                pendingStart = true, pendingStartPos = pos;
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            rebuildSequence();

            if (heldNotes.empty())
            {
                stopNote (midiOut, pos);    // everything released: silence the arp
                samplesToNextTick = -1;
                gateCountdown = -1;
                step = 0;
                pendingStart = false;
            }
        }

        // other messages (CC, pitch bend...) are consumed, not forwarded
    }

    if (pendingStart && ! sequence.empty())
        fireStart (pendingStartPos);

    // ---- pass 2: clock & gate timeline over the rest of the block -----------
    constexpr int never = std::numeric_limits<int>::max();
    int pos = 0;

    while (pos < numSamples)
    {
        const bool haveTick = samplesToNextTick >= 0 && ! sequence.empty();
        const bool haveGate = gateCountdown >= 0;
        if (! haveTick && ! haveGate)
            break;

        const int toTick = haveTick ? samplesToNextTick : never;
        const int toGate = haveGate ? gateCountdown : never;
        const int remaining = numSamples - pos;

        if (toTick <= toGate && toTick < remaining)
        {
            pos += toTick;
            tick (midiOut, pos);
            samplesToNextTick = stepSamples;
        }
        else if (toGate < remaining)
        {
            pos += toGate;
            stopNote (midiOut, pos);
            gateCountdown = -1;
            samplesToNextTick -= toGate;
        }
        else
        {
            // nothing fires within this block: carry the countdowns over
            if (haveTick) samplesToNextTick -= remaining;
            if (haveGate) gateCountdown  -= remaining;
            break;
        }
    }
}

//==============================================================================
void ArpEngine::rebuildSequence()
{
    sequence.clear();
    if (heldNotes.empty())
        return;

    const auto mode = (Mode) params.mode;
    std::vector<int> notes (heldNotes);

    if (mode == Mode::up || mode == Mode::upDown)
        std::sort (notes.begin(), notes.end());
    else if (mode == Mode::down)
        std::sort (notes.begin(), notes.end(), std::greater<int>());

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
