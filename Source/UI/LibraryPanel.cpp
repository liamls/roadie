#include "LibraryPanel.h"
#include "../I18n.h"

using namespace roadie;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
constexpr int rowHeight = 42;
} // namespace

LibraryPanel::LibraryPanel (Session& s)
    : session (s),
      tabs ({ { tr ("Captures"), icons::Icon::amp, tr ("The NAM captures in the library") },
              { tr ("Songs"), icons::Icon::song, tr ("The songs to play along to") },
              { tr ("Takes"), icons::Icon::record, tr ("The project's takes") } }),
      openFileButton (tr ("Open a File…"), ui::PillButton::Variant::neutral, theme::ampAccent, icons::Icon::folder),
      youtubeButton (tr ("From YouTube…"), ui::PillButton::Variant::tinted, theme::songAccent, icons::Icon::song)
{
    youtubeButton.setFontSize (12.0f);
    youtubeButton.setTooltip (tr ("Paste a YouTube link: the audio is downloaded and the tempo detected automatically"));
    youtubeButton.onClick = [this] { if (onImportYouTube) onImportYouTube(); };
    addChildComponent (youtubeButton);

    tabs.onChange = [this] (int index) { setTab ((Tab) index); };
    addAndMakeVisible (tabs);

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
        // Return takes the first match: search → enter is the fastest way to swap amps.
        if (! visible.empty() && visible.front().action)
            visible.front().action();
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
    list.addMouseListener (this, true);
    addAndMakeVisible (list);

    addFolderButton.setTooltip (tr ("Add a captures folder to the library"));
    addFolderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (addFolderButton);

    openFileButton.setFontSize (12.0f);
    openFileButton.onClick = [this]
    {
        if (tab == Tab::songs)
        {
            if (onImportSong)
                onImportSong();
        }
        else
            chooseFile();
    };
    addAndMakeVisible (openFileButton);

    setTab (Tab::captures);
}

void LibraryPanel::setTab (Tab newTab)
{
    tab = newTab;
    tabs.setSelected ((int) tab);
    search.clear();
    search.setTextToShowWhenEmpty (placeholder(), theme::textFaint);
    addFolderButton.setVisible (tab == Tab::captures);
    openFileButton.setVisible (tab != Tab::takes);
    youtubeButton.setVisible (tab == Tab::songs);
    openFileButton.setButtonText (tab == Tab::songs ? tr ("Import Song…") : tr ("Open a File…"));
    openFileButton.setIcon (tab == Tab::songs ? icons::Icon::song : icons::Icon::folder);
    openFileButton.setAccent (tab == Tab::songs ? theme::songAccent : theme::ampAccent);
    openFileButton.setTooltip (tab == Tab::songs ? tr ("MP3, M4A, WAV, AIFF or FLAC — or drop it onto the window")
                                                 : tr ("Load a .nam capture that isn't in the library"));
    visibleSignature.clear();
    resized();
    applyFilter();
    repaint();
}

juce::String LibraryPanel::placeholder() const
{
    switch (tab)
    {
        case Tab::songs:    return tr ("Search songs");
        case Tab::takes:    return tr ("Search takes");
        case Tab::captures:
        default:            return tr ("Search captures");
    }
}

void LibraryPanel::refresh()
{
    applyFilter();
}

std::vector<LibraryPanel::Row> LibraryPanel::buildRows() const
{
    std::vector<Row> rows;
    auto* s = &session;

    if (tab == Tab::captures)
    {
        const auto loaded = session.getEngine().getAmp().getModelFile();
        for (const auto& c : session.getLibrary().getCaptures())
            rows.push_back ({ c.name, c.tag, c.file.getFullPathName(), theme::ampAccent, c.file == loaded,
                              [s, file = c.file] { s->loadCapture (file); }, c.file.getFullPathName() });
    }
    else if (tab == Tab::songs)
    {
        // Recent imports first, then the songs folder.
        juce::Array<juce::File> files;
        for (const auto& path : juce::StringArray::fromLines (session.getSettings().getValue ("recentSongs")))
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
                files.addIfNotAlreadyThere (juce::File (path));
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (now - songFolderScanTime > 5.0)
        {
            songFolderScanTime = now;
            const auto roadieFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Roadie");
            songFolderFiles.clear();
            // "Morceaux" is what earlier versions called the songs folder.
            for (const auto* name : { "Songs", "Morceaux" })
                songFolderFiles.addArray (roadieFolder.getChildFile (name).findChildFiles (juce::File::findFiles, true, "*.mp3;*.m4a;*.aac;*.wav;*.aif;*.aiff;*.flac;*.caf"));
        }
        for (const auto& f : songFolderFiles)
            files.addIfNotAlreadyThere (f);

        const auto current = session.getSong().title;
        for (const auto& f : files)
        {
            const bool isCurrent = session.hasSong() && f.getFileNameWithoutExtension() == current;
            const auto subtitle = isCurrent ? juce::String (session.getSong().bpm, 1).replaceCharacter ('.', i18n::decimalPoint()) + tr (" BPM · in the project")
                                            : f.getFileExtension().substring (1).toUpperCase() + utf8 (" \xc2\xb7 ") + f.getParentDirectory().getFileName();
            rows.push_back ({ f.getFileNameWithoutExtension(), subtitle, f.getFullPathName(), theme::songAccent, isCurrent,
                              [s, f] { if (! s->isImportingSong()) s->importSong (f); }, f.getFullPathName() });
        }
    }
    else
    {
        const auto& takes = session.getTakes();
        const auto& clips = session.getArrangement().clips;
        for (int i = (int) takes.size(); --i >= 0;) // newest first
        {
            const auto& take = takes[(size_t) i];
            int clipIndex = -1;
            for (int c = 0; c < (int) clips.size() && clipIndex < 0; ++c)
                if (clips[(size_t) c].take == i)
                    clipIndex = c;
            const int seconds = (int) take.durationSeconds;
            auto subtitle = juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2)
                            + (take.hasVideo() ? tr (" · video") : tr (" · audio only"))
                            + (clipIndex < 0 ? tr (" · off the timeline") : juce::String());
            auto* panel = this;
            rows.push_back ({ take.name, subtitle, take.createdAt.formatted ("%d/%m %H:%M"), theme::ampAccent,
                              clipIndex >= 0 && session.getSelectedClip() == clipIndex,
                              [s, panel, i, clipIndex]
                              {
                                  if (clipIndex < 0)
                                      s->insertTake (i, s->getPlayheadSeconds());
                                  else if (panel->onShowClip)
                                      panel->onShowClip (clipIndex);
                              }, {}, i });
        }
    }

    return rows;
}

void LibraryPanel::showRowMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;
    juce::PopupMenu menu;
    menu.addItem (tr ("Delete…"), [this, row] { confirmDelete (row); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

// Deleting is for good (the Trash, for files), so it asks first and says what goes.
void LibraryPanel::confirmDelete (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;
    const auto r = visible[(size_t) row];
    auto* s = &session;
    const auto tabNow = tab;
    juce::File file (r.key);

    // A song is the player's own file unless it sits in Roadie's songs folder:
    // elsewhere, only the entry in the list goes.
    const auto songsFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Roadie");
    const bool trashesFile = tabNow == Tab::captures || (tabNow == Tab::songs && file.isAChildOf (songsFolder));

    juce::String message = tabNow == Tab::takes ? tr ("The take, its regions on the timeline and its files (audio, video) will be deleted.")
                           : trashesFile       ? tr ("The file will be moved to the Trash.")
                                               : tr ("The song leaves this list. Your original file isn't touched.");
    juce::Component::SafePointer<LibraryPanel> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (
                                      juce::MessageBoxIconType::WarningIcon, tr ("Delete") + " \"" + r.title + "\"?", message, tr ("Delete"), tr ("Cancel"), this),
                                  [safe, s, r, tabNow, file, trashesFile] (int result)
                                  {
                                      if (result != 1 || safe == nullptr)
                                          return;
                                      auto* const self = safe.getComponent();
                                      if (tabNow == Tab::takes)
                                      {
                                          s->deleteTake (r.takeIndex);
                                      }
                                      else
                                      {
                                          if (trashesFile)
                                              file.moveToTrash();
                                          if (tabNow == Tab::songs)
                                          {
                                              auto recent = juce::StringArray::fromLines (s->getSettings().getValue ("recentSongs"));
                                              recent.removeString (file.getFullPathName());
                                              s->getSettings().setValue ("recentSongs", recent.joinIntoString ("\n"));
                                              self->songFolderScanTime = -100.0;
                                          }
                                          else
                                          {
                                              s->getLibrary().rescan();
                                          }
                                      }
                                      self->visibleSignature.clear();
                                      self->applyFilter();
                                  });
}

void LibraryPanel::applyFilter()
{
    const auto query = search.getText().trim();
    std::vector<Row> filtered;
    juce::String signature;

    for (auto& row : buildRows())
    {
        // Every word must match the title or the subtitle ("fender clean").
        const auto haystack = row.title + " " + row.subtitle + " " + row.tooltip;
        bool all = true;
        for (const auto& word : juce::StringArray::fromTokens (query, true))
            all = all && haystack.containsIgnoreCase (word);
        if (! all)
            continue;
        signature << row.title << '|' << row.subtitle << '|' << (int) row.active << '\n';
        filtered.push_back (std::move (row));
    }

    visible = std::move (filtered);
    if (signature != visibleSignature)
    {
        visibleSignature = signature;
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

    const auto& r = visible[(size_t) row];
    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.0f, 0.5f);

    const float hover = row == hoveredRow ? rowFadeIn.get (true) : (row == fadingRow ? rowFadeOut.get (false) : 0.0f);
    if (r.active || hover > 0.0f)
    {
        g.setColour (r.active ? r.colour.withAlpha (0.12f + 0.05f * hover) : theme::text.withAlpha (0.05f * hover));
        roadie::theme::fillSquircle (g, bounds, radiusSm);
    }

    auto content = bounds.reduced (8.0f, 0.0f);
    auto swatch = content.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (r.active ? r.colour : r.colour.withAlpha (0.55f));
    roadie::theme::fillSquircle (g, swatch, 3.0f);
    content.removeFromLeft (9.0f);

    auto nameArea = content.removeFromTop (content.getHeight() * 0.52f);
    g.setColour (text);
    const auto nameFont = fonts::semibold (13.0f);
    g.setFont (nameFont);
    g.drawText (ui::truncateMiddle (r.title, nameFont, nameArea.getWidth()), nameArea.withTrimmedTop (3.0f),
                juce::Justification::bottomLeft, false);

    g.setColour (textFaint);
    g.setFont (fonts::medium (11.0f));
    g.drawText (r.subtitle, content.withTrimmedBottom (3.0f), juce::Justification::topLeft, true);
}

int LibraryPanel::rowAt (const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo (&list).getPosition();
    return list.getLocalBounds().contains (p) ? list.getRowContainingPosition (p.x, p.y) : -1;
}

void LibraryPanel::setHoveredRow (int row)
{
    if (row == hoveredRow)
        return;
    fadingRow = hoveredRow;
    rowFadeOut.reset (hoveredRow >= 0);
    rowFadeIn.reset (false);
    hoveredRow = row;
    list.repaint();
}

void LibraryPanel::mouseMove (const juce::MouseEvent& e)
{
    setHoveredRow (rowAt (e));
}

void LibraryPanel::mouseExit (const juce::MouseEvent& e)
{
    setHoveredRow (rowAt (e));
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showRowMenu (row);
        return;
    }
    if (juce::isPositiveAndBelow (row, (int) visible.size()) && visible[(size_t) row].action)
        visible[(size_t) row].action();
}

juce::String LibraryPanel::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return {};
    return visible[(size_t) row].tooltip;
}

void LibraryPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Add a NAM captures folder"),
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
    chooser = std::make_unique<juce::FileChooser> (tr ("Choose a NAM capture (.nam)"), juce::File(), "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  session.loadCapture (fc.getResult());
                          });
}

void LibraryPanel::lookAndFeelChanged()
{
    search.setTextToShowWhenEmpty (placeholder(), theme::textFaint);
    search.applyColourToAllText (theme::text);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (surface);
    g.setColour (border);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());


    const auto box = search.getBounds().toFloat().reduced (0.5f);
    g.setColour (surfaceSunken);
    roadie::theme::fillSquircle (g, box, radiusSm);
    g.setColour (search.hasKeyboardFocus (true) ? textMuted : border);
    roadie::theme::drawSquircle (g, box, radiusSm, 1.0f);
    icons::draw (g, icons::Icon::search, searchIconArea.toFloat(), textFaint);

    if (visible.empty())
    {
        auto area = list.getBounds().toFloat().reduced (14.0f, 10.0f);
        g.setColour (textFaint);
        g.setFont (fonts::medium (12.0f));
        juce::String message;
        if (! search.isEmpty())
            message = tr ("Nothing matches.");
        else if (tab == Tab::songs)
            message = tr ("No songs yet.\nDrop an MP3, M4A, WAV, AIFF or FLAC onto the window, or put them in Music/Roadie/Songs.\n\nApple Music and Spotify tracks are protected and can't be played.");
        else if (tab == Tab::takes)
            message = tr ("No takes in this project.\nPress T or ● to record guitar + camera.");
        else
            message = session.getLibrary().isScanning() ? tr ("Looking for captures…")
                                                        : tr ("No .nam captures found.\nAdd a folder with +, or drop your captures into Music/Roadie/Captures.");
        g.drawFittedText (message, area.toNearestInt(), juce::Justification::centredTop, 6);
    }
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    area.removeFromTop (12);

    auto header = area.removeFromTop (30);
    tabs.setBounds (header);
    headerArea = header;
    area.removeFromTop (8);

    auto searchArea = area.removeFromTop (30);
    if (addFolderButton.isVisible())
    {
        addFolderButton.setBounds (searchArea.removeFromRight (26).withSizeKeepingCentre (24, 24));
        searchArea.removeFromRight (6);
    }
    search.setBounds (searchArea);
    searchIconArea = searchArea.withWidth (30).withSizeKeepingCentre (13, 13).translated (2, 0);
    area.removeFromTop (8);

    if (openFileButton.isVisible())
    {
        openFileButton.setBounds (area.removeFromBottom (30));
        area.removeFromBottom (8);
    }
    if (youtubeButton.isVisible())
    {
        youtubeButton.setBounds (area.removeFromBottom (30));
        area.removeFromBottom (8);
    }
    area.removeFromBottom (2);

    list.setBounds (area.withTrimmedLeft (-4).withTrimmedRight (-4));
}
