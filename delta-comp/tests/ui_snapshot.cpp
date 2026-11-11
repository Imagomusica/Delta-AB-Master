// Renders the editor to a PNG without a window, after feeding a music-like signal (phrases, transients, tails).
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("delta-comp.png"));
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    auto& proc = static_cast<DeltaCompProcessor&> (*p);
    for (auto* id : { "ambOn", "revOn", "llOn" }) proc.apvts.getParameter (id)->setValueNotifyingHost (1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* ded = static_cast<DeltaCompEditor*> (ed.get());
    ed->setSize (DeltaCompEditor::kW, DeltaCompEditor::kH);
    p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
    p->prepareToPlay (48000, 400);
    juce::AudioBuffer<double> b (2, 400); juce::MidiBuffer midi; juce::Random rnd (3);
    double tail = 0; long n = 0;
    for (int col = 0; col < ui::Sand::kCols; ++col)
    {
        const double phrase = 0.03 + 0.45 * std::pow (0.5 + 0.5 * std::sin (col * 0.035), 3.0);
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < 400; ++i, ++n)
            {
                if (n % 9600 == 0) tail = phrase * 2.5;          // a note every 200 ms
                tail *= 0.99985;
                const double x = tail * (rnd.nextDouble() * 2 - 1);
                b.setSample (0, i, x); b.setSample (1, i, 0.8 * x + 0.2 * tail * (rnd.nextDouble() * 2 - 1));
            }
            static_cast<juce::AudioProcessor&> (proc).processBlock (b, midi);
        }
        ded->timerCallback();
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
