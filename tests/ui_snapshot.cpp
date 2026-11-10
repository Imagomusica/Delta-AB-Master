// Renders the editor to PNG files (main console and each detail panel) without a window, to check the layout.
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto save = [&] (const juce::String& name, float scale)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        juce::File f = dir.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s\n", f.getFullPathName().toRawUTF8());
    };
    {   // feed some signal so the gain-match meters show: mic 1 X at -12 dB, Y at -15.5 dB (noise)
        juce::AudioBuffer<double> b (p->getTotalNumInputChannels() > p->getTotalNumOutputChannels() ? p->getTotalNumInputChannels() : p->getTotalNumOutputChannels(), 512);
        juce::MidiBuffer midi; juce::Random rnd (1);
        p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        p->prepareToPlay (48000, 512);
        for (int blk = 0; blk < 60; ++blk)
        {
            b.clear();
            for (int i = 0; i < 512; ++i)
            {
                b.setSample (0, i, 0.25 * (rnd.nextDouble() * 2 - 1) * 1.732);
                b.setSample (1, i, 0.168 * (rnd.nextDouble() * 2 - 1) * 1.732);
                b.setSample (9, i, 0.2 * (rnd.nextDouble() * 2 - 1));
                b.setSample (10, i, 0.3 * (rnd.nextDouble() * 2 - 1));
            }
            static_cast<juce::AudioProcessor&> (*p).processBlock (b, midi);
        }
    }
    if (auto* c = ed->findChildWithID ("console")) (void) c;
    std::function<void (juce::Component&)> tickAll = [&] (juce::Component& c) {
        if (auto* s = dynamic_cast<ui::Strip*> (&c)) s->tick();
        if (auto* m = dynamic_cast<ui::MasterPanel*> (&c)) { m->tick(); m->tick(); }
        for (auto* ch : c.getChildren()) tickAll (*ch);
    };
    tickAll (*ed);
    {   // show the fader readout on the first fader, as during a drag from 0 to -6.5 dB
        std::function<ui::Knob* (juce::Component&)> firstFader = [&] (juce::Component& c) -> ui::Knob* {
            if (auto* k = dynamic_cast<ui::Knob*> (&c)) if (k->isFader) return k;
            for (auto* ch : c.getChildren()) if (auto* k = firstFader (*ch)) return k;
            return nullptr;
        };
        if (auto* k = firstFader (*ed)) { k->dragStartValue = 0; k->slider.setValue (-6.5); k->move.setVisible (true); k->showMove(); }
    }
    save ("console.png", 1.5f);
    // open each kind of detail by clicking its DETAIL button
    auto clickDetail = [&] (const juce::String& stripTitle)
    {
        std::function<bool (juce::Component&)> find = [&] (juce::Component& c) -> bool {
            if (auto* s = dynamic_cast<ui::Strip*> (&c))
                if (s->title == stripTitle) { s->detail.onClick(); return true; }
            for (auto* ch : c.getChildren()) if (find (*ch)) return true;
            return false;
        };
        find (*ed);
    };
    clickDetail ("AB.01"); save ("detail_ab.png", 1.0f);
    clickDetail ("AER.02");   save ("detail_aer.png", 1.0f);
    clickDetail ("EN-MS.01");  save ("detail_spot.png", 1.0f);
    ed.reset();
    p.reset();
    return 0;
}
