#include "CaptureLibrary.h"

#include <juce_events/juce_events.h>

namespace
{
constexpr int maxScanDepth = 4;

juce::String sizeLabel (const juce::var& model)
{
    const auto architecture = model["architecture"].toString();

    if (architecture == "WaveNet")
    {
        // The standard NAM trainer presets differ only by channel count.
        const int channels = (int) model["config"]["layers"][0]["channels"];
        if (channels >= 16) return "Standard";
        if (channels >= 12) return "Lite";
        if (channels >= 8) return "Feather";
        if (channels > 0) return "Nano";
        return "WaveNet";
    }
    if (architecture == "SlimmableContainer")
        return "Adaptatif";
    return architecture.isNotEmpty() ? architecture : juce::String ("NAM");
}

juce::String toneLabel (const juce::String& tone)
{
    if (tone == "clean") return "Clean";
    if (tone == "crunch") return "Crunch";
    if (tone == "overdrive") return "Overdrive";
    if (tone == "hi_gain") return "High-gain";
    if (tone == "fuzz") return "Fuzz";
    return {};
}

juce::String gearLabel (const juce::String& gear)
{
    if (gear == "amp") return "Ampli";
    if (gear == "pedal") return juce::String::fromUTF8 ("P\xc3\xa9" "dale");
    if (gear == "pedal_amp") return juce::String::fromUTF8 ("P\xc3\xa9" "dale + ampli");
    if (gear == "amp_cab") return "Ampli + cab";
    if (gear == "amp_pedal_cab" || gear == "full-rig") return "Rig complet";
    if (gear == "preamp") return juce::String::fromUTF8 ("Pr\xc3\xa9" "ampli");
    if (gear == "studio") return "Studio";
    return {};
}

bool isPackage (const juce::File& dir)
{
    // Bundles look like folders but are opaque documents: never descend.
    for (auto* ext : { ".app", ".roadie", ".logicx", ".band", ".photoslibrary", ".musiclibrary" })
        if (dir.hasFileExtension (ext))
            return true;
    return dir.getFileName().startsWithChar ('.');
}

void collect (const juce::File& dir, int depth, juce::Array<juce::File>& out)
{
    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto& f = entry.getFile();
        if (entry.isDirectory())
        {
            if (depth < maxScanDepth && ! isPackage (f))
                collect (f, depth + 1, out);
        }
        else if (f.hasFileExtension (".nam"))
        {
            out.addIfNotAlreadyThere (f);
        }
    }
}
} // namespace

struct CaptureLibrary::Scanner
{
    CaptureLibrary* owner = nullptr; // message thread only; cleared when the library goes away
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Roadie library scan") };
    std::atomic<int> pending { 0 };
};

CaptureLibrary::CaptureLibrary() : scanner (std::make_shared<Scanner>())
{
    scanner->owner = this;
}

CaptureLibrary::~CaptureLibrary()
{
    scanner->owner = nullptr;
    scanner->pool.removeAllJobs (true, 2000);
}

juce::Array<juce::File> CaptureLibrary::getDefaultFolders()
{
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto own = music.getChildFile ("Roadie").getChildFile ("Captures");
    own.createDirectory();
    return { own, music };
}

void CaptureLibrary::setFolders (const juce::Array<juce::File>& newFolders)
{
    folders = newFolders;
    rescan();
}

void CaptureLibrary::addFolder (const juce::File& folder)
{
    if (folder.isDirectory() && ! folders.contains (folder))
    {
        folders.add (folder);
        rescan();
    }
}

bool CaptureLibrary::isScanning() const
{
    return scanner->pending.load() > 0;
}

void CaptureLibrary::rescan()
{
    auto state = scanner;
    const auto foldersToScan = folders;
    state->pending.fetch_add (1);

    state->pool.addJob ([state, foldersToScan]
    {
        juce::Array<juce::File> files;
        for (const auto& folder : foldersToScan)
            if (folder.isDirectory())
                collect (folder, 0, files);

        std::vector<CaptureInfo> found;
        for (const auto& f : files)
            found.push_back (describe (f));

        std::sort (found.begin(), found.end(), [] (const CaptureInfo& a, const CaptureInfo& b)
        {
            return a.name.compareNatural (b.name) < 0;
        });

        juce::MessageManager::callAsync ([state, result = std::move (found)]() mutable
        {
            state->pending.fetch_sub (1);
            if (auto* lib = state->owner)
            {
                lib->captures = std::move (result);
                if (lib->onChanged)
                    lib->onChanged();
            }
        });
    });
}

CaptureInfo CaptureLibrary::describe (const juce::File& namFile)
{
    CaptureInfo info;
    info.file = namFile;
    info.name = namFile.getFileNameWithoutExtension();

    const auto json = juce::JSON::parse (namFile);
    if (! json.isObject())
    {
        info.tag = "NAM";
        return info;
    }

    // Containers (slimmable captures) keep the descriptive metadata on the
    // outer object and the real architecture in their sub-models.
    auto metadata = json["metadata"];
    if (! metadata.isObject())
        metadata = json["config"]["submodels"][0]["model"]["metadata"];

    juce::StringArray parts;
    parts.add (sizeLabel (json));

    auto detail = toneLabel (metadata["tone_type"].toString());
    if (detail.isEmpty())
        detail = gearLabel (metadata["gear_type"].toString());
    if (detail.isNotEmpty())
        parts.add (detail);

    info.tag = parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
    return info;
}
