#include "LibraryPanel.h"

using namespace roadie;

namespace
{
juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }
constexpr int rowHeight = 42;
} // namespace

LibraryPanel::LibraryPanel (Session& s)
    : session (s),
      tabs ({ { "Captures", icons::Icon::amp, utf8 ("Les captures NAM de la biblioth\xc3\xa8que") },
              { "Morceaux", icons::Icon::song, utf8 ("Les morceaux sur lesquels jouer") },
              { "Prises", icons::Icon::record, utf8 ("Les prises du projet") } }),
      openFileButton (utf8 ("Ouvrir un fichier\xe2\x80\xa6"), ui::PillButton::Variant::neutral, theme::ampAccent, icons::Icon::folder),
      youtubeButton (utf8 ("Depuis YouTube\xe2\x80\xa6"), ui::PillButton::Variant::tinted, theme::songAccent, icons::Icon::song),
      hiddenButton ("Masqu\xc3\xa9s", ui::PillButton::Variant::neutral, theme::textMuted)
{
    youtubeButton.setFontSize (12.0f);
    youtubeButton.setTooltip (utf8 ("Colle un lien YouTube : l'audio est t\xc3\xa9l\xc3\xa9" "charg\xc3\xa9 et le tempo d\xc3\xa9tect\xc3\xa9 tout seul"));
    youtubeButton.onClick = [this] { if (onImportYouTube) onImportYouTube(); };
    addChildComponent (youtubeButton);
    hiddenButton.setFontSize (11.5f);
    hiddenButton.onClick = [this] { showHidden = ! showHidden; visibleSignature.clear(); applyFilter(); };
    addChildComponent (hiddenButton);

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

    addFolderButton.setTooltip (utf8 ("Ajouter un dossier de captures \xc3\xa0 la biblioth\xc3\xa8que"));
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
    showHidden = false;
    openFileButton.setButtonText (tab == Tab::songs ? utf8 ("Importer un morceau\xe2\x80\xa6") : utf8 ("Ouvrir un fichier\xe2\x80\xa6"));
    openFileButton.setIcon (tab == Tab::songs ? icons::Icon::song : icons::Icon::folder);
    openFileButton.setAccent (tab == Tab::songs ? theme::songAccent : theme::ampAccent);
    openFileButton.setTooltip (tab == Tab::songs ? utf8 ("MP3, M4A, WAV, AIFF ou FLAC \xe2\x80\x94 ou glisse-le sur la fen\xc3\xaatre")
                                                 : utf8 ("Charger une capture .nam qui n'est pas dans la biblioth\xc3\xa8que"));
    visibleSignature.clear();
    resized();
    applyFilter();
    repaint();
}

juce::String LibraryPanel::placeholder() const
{
    switch (tab)
    {
        case Tab::songs:    return "Chercher un morceau";
        case Tab::takes:    return "Chercher une prise";
        case Tab::captures:
        default:            return "Chercher une capture";
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
            const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Roadie").getChildFile ("Morceaux");
            songFolderFiles = folder.findChildFiles (juce::File::findFiles, true, "*.mp3;*.m4a;*.aac;*.wav;*.aif;*.aiff;*.flac;*.caf");
        }
        for (const auto& f : songFolderFiles)
            files.addIfNotAlreadyThere (f);

        const auto current = session.getSong().title;
        for (const auto& f : files)
        {
            const bool isCurrent = session.hasSong() && f.getFileNameWithoutExtension() == current;
            const auto subtitle = isCurrent ? juce::String (session.getSong().bpm, 1).replaceCharacter ('.', ',') + utf8 (" BPM \xc2\xb7 dans le projet")
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
                            + (take.hasVideo() ? utf8 (" \xc2\xb7 vid\xc3\xa9o") : utf8 (" \xc2\xb7 audio seul"))
                            + (clipIndex < 0 ? utf8 (" \xc2\xb7 hors timeline") : juce::String());
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

    // Entries put aside stay out of the list, unless asked for.
    for (auto& row : rows)
        row.hidden = row.takeIndex >= 0 ? session.getTakes()[(size_t) row.takeIndex].hidden
                                        : session.isHiddenInLibrary (kindOfTab(), row.key);
    if (! showHidden)
        rows.erase (std::remove_if (rows.begin(), rows.end(), [] (const Row& r) { return r.hidden; }), rows.end());
    return rows;
}

void LibraryPanel::updateHiddenButton()
{
    const int count = session.countHiddenInLibrary (kindOfTab());
    const bool shouldShow = count > 0 || showHidden;
    hiddenButton.setButtonText (showHidden ? utf8 ("Cacher les masqu\xc3\xa9s") : "Masqu\xc3\xa9s (" + juce::String (count) + ")");
    hiddenButton.setVariant (showHidden ? ui::PillButton::Variant::tinted : ui::PillButton::Variant::neutral);
    if (hiddenButton.isVisible() != shouldShow)
    {
        hiddenButton.setVisible (shouldShow);
        resized();
    }
}

void LibraryPanel::showRowMenu (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;
    const auto r = visible[(size_t) row];
    auto* s = &session;
    const auto kind = kindOfTab();
    juce::PopupMenu menu;
    const auto label = r.hidden ? utf8 ("R\xc3\xa9tablir dans la liste")
                       : tab == Tab::takes ? utf8 ("Masquer cette prise de la liste") : utf8 ("Masquer de la liste");
    menu.addItem (label, [s, r, kind]
                  {
                      if (r.takeIndex >= 0)
                          s->setTakeHidden (r.takeIndex, ! r.hidden);
                      else
                          s->setHiddenInLibrary (kind, r.key, ! r.hidden);
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void LibraryPanel::applyFilter()
{
    updateHiddenButton();
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
        signature << row.title << '|' << row.subtitle << '|' << (int) row.active << '|' << (int) row.hidden << '\n';
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

    if (r.hidden)
        g.setOpacity (0.45f);
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
    g.drawText (r.hidden ? utf8 ("Masqu\xc3\xa9 \xc2\xb7 clic droit pour r\xc3\xa9tablir") : r.subtitle, content.withTrimmedBottom (3.0f), juce::Justification::topLeft, true);
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
            message = utf8 ("Rien ne correspond.");
        else if (tab == Tab::songs)
            message = utf8 ("Aucun morceau pour l'instant.\nGlisse un MP3, M4A, WAV, AIFF ou FLAC sur la fen\xc3\xaatre, ou range-les dans Musique/Roadie/Morceaux.\n\nLes titres d'Apple Music ou de Spotify sont prot\xc3\xa9g\xc3\xa9s et ne peuvent pas \xc3\xaatre lus.");
        else if (tab == Tab::takes)
            message = utf8 ("Aucune prise dans ce projet.\nT ou \xe2\x97\x8f pour enregistrer guitare + cam\xc3\xa9ra.");
        else
            message = session.getLibrary().isScanning() ? utf8 ("Recherche des captures\xe2\x80\xa6")
                                                        : utf8 ("Aucune capture .nam trouv\xc3\xa9" "e.\nAjoute un dossier avec +, ou d\xc3\xa9pose tes captures dans Musique/Roadie/Captures.");
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
    if (hiddenButton.isVisible())
    {
        hiddenButton.setBounds (area.removeFromBottom (26));
        area.removeFromBottom (6);
    }
    else
        area.removeFromBottom (2);

    list.setBounds (area.withTrimmedLeft (-4).withTrimmedRight (-4));
}
