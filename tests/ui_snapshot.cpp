// Renders the editor to PNG files (main console and each detail panel) without a window, to check the layout.
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    p->prepareToPlay (48000, 512);
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
    save ("console.png", 1.0f);
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
    clickDetail ("MIC 1 G"); save ("detail_ab.png", 1.0f);
    clickDetail ("AER C");   save ("detail_aer.png", 1.0f);
    clickDetail ("SPOT 1");  save ("detail_spot.png", 1.0f);
    ed.reset();
    p.reset();
    return 0;
}
