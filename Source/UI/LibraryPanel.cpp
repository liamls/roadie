#include "LibraryPanel.h"

using namespace roadie;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
constexpr int rowHeight = 42;
} // namespace

LibraryPanel::LibraryPanel (Session& s)
    : session (s),
      openFileButton (utf8 ("Ouvrir un fichier\xe2\x80\xa6"), ui::PillButton::Variant::neutral, theme::ampAccent, icons::Icon::folder)
{
    search.setTextToShowWhenEmpty ("Chercher une capture", theme::textFaint);
    search.setFont (fonts::medium (12.5f));
    search.setIndents (28, 7);
    // The box and its magnifier are painted by the panel, underneath the editor.
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.onTextChange = [this] { applyFilter(); };
    search.onEscapeKey = [this] { search.clear(); applyFilter(); if (auto* p = getTopLevelComponent()) p->grabKeyboardFocus(); };
    search.onReturnKey = [this]
    {
        // Return loads the first match: search → enter is the fastest way to swap amps.
        if (! visible.empty())
            session.loadCapture (visible.front().file);
        if (auto* p = getTopLevelComponent())
            p->grabKeyboardFocus();
    };
    addAndMakeVisible (search);

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    list.setWantsKeyboardFocus (false);
    list.setMouseClickGrabsKeyboardFocus (false);
    list.getViewport()->setScrollBarThickness (8);
    addAndMakeVisible (list);

    addFolderButton.setTooltip (utf8 ("Ajouter un dossier de captures \xc3\xa0 la biblioth\xc3\xa8que"));
    addFolderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (addFolderButton);

    openFileButton.setTooltip (utf8 ("Charger une capture .nam qui n'est pas dans la biblioth\xc3\xa8que"));
    openFileButton.setFontSize (12.0f);
    openFileButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (openFileButton);

    refresh();
}

void LibraryPanel::refresh()
{
    const auto loaded = session.getEngine().getAmp().getModelFile().getFullPathName();
    if (loaded != lastLoadedPath)
    {
        lastLoadedPath = loaded;
        list.repaint();
    }
    applyFilter();
}

void LibraryPanel::applyFilter()
{
    const auto query = search.getText().trim();
    std::vector<CaptureInfo> filtered;

    for (const auto& c : session.getLibrary().getCaptures())
    {
        if (query.isEmpty())
        {
            filtered.push_back (c);
            continue;
        }

        // Every word must match the name, the tag or the folder ("fender clean").
        const auto haystack = c.name + " " + c.tag + " " + c.file.getParentDirectory().getFileName();
        bool all = true;
        for (const auto& word : juce::StringArray::fromTokens (query, true))
            all = all && haystack.containsIgnoreCase (word);
        if (all)
            filtered.push_back (c);
    }

    const bool changed = filtered.size() != visible.size()
                         || ! std::equal (filtered.begin(), filtered.end(), visible.begin(),
                                          [] (const CaptureInfo& a, const CaptureInfo& b) { return a.file == b.file; });
    if (changed)
    {
        visible = std::move (filtered);
        list.updateContent();
        list.repaint();
        repaint();
    }
}

int LibraryPanel::getNumRows()
{
    return (int) visible.size();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    using namespace theme;
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;

    const auto& c = visible[(size_t) row];
    const bool isLoaded = c.file.getFullPathName() == lastLoadedPath;
    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.0f, 0.5f);

    if (isLoaded)
    {
        g.setColour (ampAccent.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds, radiusSm);
    }
    else if (list.getRowContainingPosition (list.getMouseXYRelative().x, list.getMouseXYRelative().y) == row && list.isMouseOver (true))
    {
        g.setColour (theme::text.withAlpha (0.03f));
        g.fillRoundedRectangle (bounds, radiusSm);
    }

    auto content = bounds.reduced (8.0f, 0.0f);
    auto swatch = content.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (isLoaded ? ampAccent : ampAccent.withAlpha (0.55f));
    g.fillRoundedRectangle (swatch, 3.0f);
    content.removeFromLeft (9.0f);

    auto nameArea = content.removeFromTop (content.getHeight() * 0.52f);
    g.setColour (text);
    const auto nameFont = fonts::semibold (13.0f);
    g.setFont (nameFont);
    g.drawText (ui::truncateMiddle (c.name, nameFont, nameArea.getWidth()), nameArea.withTrimmedTop (3.0f),
                juce::Justification::bottomLeft, false);

    g.setColour (textFaint);
    g.setFont (fonts::medium (11.0f));
    g.drawText (c.tag, content.withTrimmedBottom (3.0f), juce::Justification::topLeft, true);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) visible.size()))
        session.loadCapture (visible[(size_t) row].file);
}

juce::String LibraryPanel::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return {};
    return visible[(size_t) row].file.getFullPathName();
}

void LibraryPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (utf8 ("Ajouter un dossier de captures NAM"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().isDirectory())
                                  session.addCaptureFolder (fc.getResult());
                          });
}

void LibraryPanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choisis une capture NAM (.nam)", juce::File(), "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  session.loadCapture (fc.getResult());
                          });
}

void LibraryPanel::lookAndFeelChanged()
{
    search.setTextToShowWhenEmpty ("Chercher une capture", theme::textFaint);
    search.applyColourToAllText (theme::text);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    ui::drawSectionLabel (g, utf8 ("Biblioth\xc3\xa8que"), headerArea.toFloat());

    const auto box = search.getBounds().toFloat().reduced (0.5f);
    g.setColour (surfaceSunken);
    g.fillRoundedRectangle (box, radiusSm);
    g.setColour (search.hasKeyboardFocus (true) ? textMuted : border);
    g.drawRoundedRectangle (box, radiusSm, 1.0f);
    icons::draw (g, icons::Icon::search, searchIconArea.toFloat(), textFaint);

    if (visible.empty())
    {
        auto area = list.getBounds().toFloat().reduced (14.0f, 10.0f);
        g.setColour (textFaint);
        g.setFont (fonts::medium (12.0f));
        const auto message = session.getLibrary().isScanning() ? utf8 ("Recherche des captures\xe2\x80\xa6")
                             : search.isEmpty() ? utf8 ("Aucune capture .nam trouv\xc3\xa9" "e.\nAjoute un dossier avec +, ou d\xc3\xa9pose tes captures dans Musique/Roadie/Captures.")
                                                : utf8 ("Aucune capture ne correspond.");
        g.drawFittedText (message, area.toNearestInt(), juce::Justification::centredTop, 6);
    }
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    area.removeFromTop (12);

    auto header = area.removeFromTop (20);
    addFolderButton.setBounds (header.removeFromRight (22).withSizeKeepingCentre (22, 22));
    headerArea = header;
    area.removeFromTop (8);

    auto searchArea = area.removeFromTop (30);
    search.setBounds (searchArea);
    searchIconArea = searchArea.withWidth (30).withSizeKeepingCentre (13, 13).translated (2, 0);
    area.removeFromTop (8);

    openFileButton.setBounds (area.removeFromBottom (30).reduced (0, 0));
    area.removeFromBottom (10);

    list.setBounds (area.withTrimmedLeft (-4).withTrimmedRight (-4));
}
