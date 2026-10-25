// sound_audit_host <path to .component or .vst3>: hosts the installed plug-in like a DAW and answers commands on
// stdin (one per line), for audit.py:
//   prepare <sr> <block>              setPlayConfigDetails + prepareToPlay; prints "latency <n>"
//   params                            "<index>|<name>|<normalised>|<text>|<steps>" per parameter, then "end"
//   set <name>|<text>                 value from the plug-in's own text conversion; prints the text read back
//   setn <name>|<normalised>          normalised value; prints the text read back
//   save / load                       keep / restore the plug-in state (getStateInformation)
//   reset                             AudioProcessor::reset()
//   render <frames> <block> <seed>    then event lines until "end":
//        on <sample> <note> <vel>  |  off <sample> <note>  |  bend <sample> <0..16383>  |  pn <sample> <index> <norm>
//        cc <sample> <controller> <value>
//      block 0 = random sizes 1..1024 (seed). Events split blocks so they land on their sample.
//      prints "data <frames> <max block us> <mean block us> <blocks>" then float32 L,R interleaved on stdout.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <iostream>
#include <random>

using namespace juce;

namespace
{
struct Ev { int pos; int kind; int a; int b; float v; }; // kind 0 on, 1 off, 2 bend, 3 param, 4 cc

AudioProcessorParameter* findParam (AudioPluginInstance& p, const String& name)
{
    for (auto* q : p.getParameters())
        if (q->getName (100) == name) return q;
    return nullptr;
}

void out (const String& s) { std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: sound_audit_host <plug-in>\n"); return 2; }
    ScopedJuceInitialiser_GUI gui;
    AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<AudioUnitPluginFormat>());
    fm.addFormat (std::make_unique<VST3PluginFormat>());
    const String path = String::fromUTF8 (argv[1]);
    OwnedArray<PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (path))
            f->findAllTypesForFile (types, path);
    if (types.isEmpty()) { out ("error no plug-in in " + path); return 1; }
    String err;
    auto p = fm.createPluginInstance (*types[0], 48000.0, 512, err);
    if (p == nullptr) { out ("error " + err); return 1; }
    out ("ok " + types[0]->pluginFormatName + " " + types[0]->name + " " + String (p->getParameters().size()));
    double sr = 48000.0; int maxBlock = 512;
    MemoryBlock saved;

    std::string line;
    while (std::getline (std::cin, line))
    {
        const String l (line);
        const auto cmd = l.upToFirstOccurrenceOf (" ", false, false);
        const auto rest = l.fromFirstOccurrenceOf (" ", false, false);
        if (cmd == "quit") break;
        if (cmd == "prepare")
        {
            auto t = StringArray::fromTokens (rest, " ", "");
            sr = t[0].getDoubleValue(); maxBlock = jmax (1024, t[1].getIntValue());
            p->releaseResources();
            p->setPlayConfigDetails (0, 2, sr, maxBlock);
            p->prepareToPlay (sr, maxBlock);
            out ("latency " + String (p->getLatencySamples()));
        }
        else if (cmd == "params")
        {
            auto& ps = p->getParameters();
            for (int i = 0; i < ps.size(); ++i)
                if (! ps[i]->getName (100).startsWith ("MIDI CC"))
                    out (String (i) + "|" + ps[i]->getName (100) + "|" + String (ps[i]->getValue(), 6) + "|" + ps[i]->getCurrentValueAsText() + "|" + String (ps[i]->getNumSteps()));
            out ("end");
        }
        else if (cmd == "set" || cmd == "setn")
        {
            auto* q = findParam (*p, rest.upToFirstOccurrenceOf ("|", false, false));
            if (q == nullptr) { out ("error no parameter " + rest); continue; }
            const auto text = rest.fromFirstOccurrenceOf ("|", false, false);
            q->setValue (cmd == "set" ? q->getValueForText (text) : text.getFloatValue());
            out ("value " + q->getCurrentValueAsText() + "|" + String (q->getValue(), 6));
        }
        else if (cmd == "save") { saved.reset(); p->getStateInformation (saved); out ("saved " + String ((int) saved.getSize())); }
        else if (cmd == "load") { p->setStateInformation (saved.getData(), (int) saved.getSize()); out ("loaded"); }
        else if (cmd == "reset") { p->reset(); out ("reset"); }
        else if (cmd == "latency") out ("latency " + String (p->getLatencySamples()));
        else if (cmd == "render")
        {
            auto t = StringArray::fromTokens (rest, " ", "");
            const int frames = t[0].getIntValue(), block = t[1].getIntValue();
            std::mt19937 rng ((unsigned) t[2].getIntValue());
            std::vector<Ev> evs;
            while (std::getline (std::cin, line) && line != "end")
            {
                auto e = StringArray::fromTokens (String (line), " ", "");
                if (e[0] == "on") evs.push_back ({ e[1].getIntValue(), 0, e[2].getIntValue(), e[3].getIntValue(), 0 });
                else if (e[0] == "off") evs.push_back ({ e[1].getIntValue(), 1, e[2].getIntValue(), 0, 0 });
                else if (e[0] == "bend") evs.push_back ({ e[1].getIntValue(), 2, e[2].getIntValue(), 0, 0 });
                else if (e[0] == "pn") evs.push_back ({ e[1].getIntValue(), 3, e[2].getIntValue(), 0, e[3].getFloatValue() });
                else if (e[0] == "cc") evs.push_back ({ e[1].getIntValue(), 4, e[2].getIntValue(), e[3].getIntValue(), 0 });
            }
            std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.pos < b.pos; });
            AudioBuffer<float> buf (jmax (2, p->getTotalNumOutputChannels()), maxBlock);
            std::vector<float> result ((size_t) frames * 2, 0.0f);
            size_t ei = 0; int pos = 0, blocks = 0; double maxUs = 0, sumUs = 0;
            std::uniform_int_distribution<int> rnd (1, 1024);
            while (pos < frames)
            {
                int n = block > 0 ? block : rnd (rng);
                n = jmin (n, frames - pos, maxBlock);
                // A parameter change splits the block so it lands on its sample (as sample-accurate automation would).
                for (size_t k = ei; k < evs.size(); ++k)
                    if (evs[k].kind == 3 && evs[k].pos > pos && evs[k].pos < pos + n) { n = evs[k].pos - pos; break; }
                MidiBuffer midi;
                while (ei < evs.size() && evs[ei].pos < pos + n)
                {
                    const auto& e = evs[ei++];
                    const int off = jmax (0, e.pos - pos);
                    if (e.kind == 0) midi.addEvent (MidiMessage::noteOn (1, e.a, (uint8) e.b), off);
                    else if (e.kind == 1) midi.addEvent (MidiMessage::noteOff (1, e.a), off);
                    else if (e.kind == 2) midi.addEvent (MidiMessage::pitchWheel (1, e.a), off);
                    else if (e.kind == 3) p->getParameters()[e.a]->setValue (e.v);
                    else if (e.kind == 4) midi.addEvent (MidiMessage::controllerEvent (1, e.a, e.b), off);
                }
                buf.setSize (buf.getNumChannels(), n, false, false, true);
                buf.clear();
                const auto t0 = Time::getHighResolutionTicks();
                p->processBlock (buf, midi);
                const double us = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0) * 1.0e6;
                maxUs = jmax (maxUs, us); sumUs += us; ++blocks;
                for (int i = 0; i < n; ++i)
                {
                    result[(size_t) (pos + i) * 2] = buf.getSample (0, i);
                    result[(size_t) (pos + i) * 2 + 1] = buf.getSample (jmin (1, buf.getNumChannels() - 1), i);
                }
                pos += n;
            }
            out ("data " + String (frames) + " " + String (maxUs, 1) + " " + String (sumUs / jmax (1, blocks), 2) + " " + String (blocks));
            std::fwrite (result.data(), sizeof (float), result.size(), stdout);
            std::fflush (stdout);
        }
        else out ("error unknown command " + cmd);
    }
    p.reset();
    return 0;
}
