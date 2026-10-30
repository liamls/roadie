#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

// The "Library" sidebar: every .nam capture found under the user's
// library folders, described from the file's own metadata.
struct CaptureInfo
{
    juce::File file;
    juce::String name; // file name: what the user organised their library by
    juce::String tag;  // "Standard · Clean", "Rig complet · High-gain"…
};

class CaptureLibrary
{
public:
    CaptureLibrary();
    ~CaptureLibrary();

    juce::Array<juce::File> getFolders() const { return folders; }
    void setFolders (const juce::Array<juce::File>& newFolders);
    void addFolder (const juce::File& folder);

    // Default places to look: ~/Music/Roadie/Captures (created) and any
    // usual NAM location that exists on this machine.
    static juce::Array<juce::File> getDefaultFolders();

    // Scans in the background; onChanged fires on the message thread.
    void rescan();
    bool isScanning() const;
    std::function<void()> onChanged;

    const std::vector<CaptureInfo>& getCaptures() const { return captures; }

    static CaptureInfo describe (const juce::File& namFile);

private:
    struct Scanner;

    juce::Array<juce::File> folders;
    std::vector<CaptureInfo> captures;
    std::shared_ptr<Scanner> scanner;
};
