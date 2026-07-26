#include "neotlk/Search.hpp"
#include "neotlk/TlkFile.hpp"
#include "neotlk/TlkXml.hpp"
#include "NeoWxUi.hpp"
#include "NeoGameDirectoryMenu.hpp"
#include "NeoDocumentTabs.hpp"
#include "NeoSettings.hpp"
#include "NeoViewState.hpp"
#include "neotlk_icon.xpm"
#include "TabularData.hpp"
#include "neotlk/TlkJson.hpp"
#include "neotlk/TlkPatcher.hpp"
#include "neotlk/TlkColumns.hpp"
#include "neotlk/TlkTable.hpp"

#include <wx/aui/auibook.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/gauge.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/wx.h>
#include <wx/version.h>

#include <algorithm>
#include <cstddef>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kAppName = "NeoTLK";
constexpr const char* kTlkWildcard = "TLK files (*.tlk;*.TLK)|*.tlk;*.TLK|All files (*.*)|*.*";
constexpr const char* kCsvTableWildcard = "CSV files (*.csv)|*.csv";
constexpr const char* kTsvTableWildcard = "TSV files (*.tsv)|*.tsv";
constexpr const char* kXmlTableWildcard = "XML files (*.xml)|*.xml|All files (*.*)|*.*";
constexpr const char* kJsonTableWildcard = "JSON files (*.json)|*.json|All files (*.*)|*.*";

const char* tableWildcardForFormat(neotabular::Format format) {
    switch (format) {
    case neotabular::Format::Csv: return kCsvTableWildcard;
    case neotabular::Format::Tsv: return kTsvTableWildcard;
    case neotabular::Format::Xml: return kXmlTableWildcard;
    case neotabular::Format::Json: return kJsonTableWildcard;
    }
    return kCsvTableWildcard;
}
std::string exportExtensionForFormat(neotabular::Format format) {
    switch (format) {
    case neotabular::Format::Csv: return "csv";
    case neotabular::Format::Tsv: return "tsv";
    case neotabular::Format::Xml: return "xml";
    case neotabular::Format::Json: return "json";
    }
    return "txt";
}

std::string exportDefaultFilename(const std::filesystem::path& source,
                                  neotabular::Format format,
                                  const std::string& fallbackStem) {
    std::string stem = source.empty() ? fallbackStem : source.stem().string();
    if (stem.empty()) stem = fallbackStem.empty() ? std::string("export") : fallbackStem;
    return stem + "." + exportExtensionForFormat(format);
}

constexpr int kSoundColumnWidth = 128;

std::string tlkDisplayColumnLabel(std::size_t column) {
    switch (column) {
        case 0: return "StrRef";
        case 1: return "Entry text";
        case 2: return "Sound";
        default: return "Column " + std::to_string(column);
    }
}

std::size_t utf8SafePrefixLength(const std::string& text, std::size_t maximumBytes) {
    if (text.size() <= maximumBytes) return text.size();
    std::size_t length = maximumBytes;
    while (length > 0u &&
           (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
        --length;
    }
    return length;
}

std::string previewText(std::string text) {
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
    }
    if (text.size() > 180u) {
        text = text.substr(0, utf8SafePrefixLength(text, 180u)) + "...";
    }
    return text;
}

std::string displayTextForEntry(const neotlk::TalkString& entry) {
    std::string sound = entry.soundString();
    if (!sound.empty()) {
        sound = " (" + sound + ")";
    }
    return std::to_string(entry.strRef) + sound + ":\n\n" + entry.text;
}

neotlk::UInt32 parseUInt32(const std::string& text, const char* fieldName) {
    neotlk::UInt32 value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != last) {
        throw std::runtime_error(std::string("Invalid ") + fieldName + ": " + text);
    }
    return value;
}

float parseFloat(const std::string& text, const char* fieldName) {
    std::size_t consumed = 0;
    float value = 0.0f;
    try {
        value = std::stof(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid ") + fieldName + ": " + text);
    }
    if (text.empty() || consumed != text.size() || !std::isfinite(value)) {
        throw std::runtime_error(std::string("Invalid ") + fieldName + ": " + text);
    }
    return value;
}

std::string floatText(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

std::vector<std::string> tlkColumns() {
    return neotlk::tlkTableColumnNames();
}

std::vector<std::string> tlkRow(const neotlk::TalkString& entry,
                                const neotlk::TalkTable& table) {
    std::string sound;
    std::string flags;
    std::string soundId;
    std::string volume;
    std::string pitch;
    std::string soundLength;
    if (table.storageFormat() == neotlk::TlkStorageFormat::ClassicV30) {
        sound = entry.soundResref.soundString();
        flags = std::to_string(entry.flags);
        volume = std::to_string(entry.volumeVariance);
        pitch = std::to_string(entry.pitchVariance);
        soundLength = floatText(entry.soundLength);
    } else if (table.storageFormat() == neotlk::TlkStorageFormat::JadeV40 &&
               entry.soundId != 0xffffffffu) {
        soundId = std::to_string(entry.soundId);
    }
    return {
        std::to_string(entry.strRef), entry.text, std::move(sound), std::move(flags),
        std::move(soundId), std::move(volume), std::move(pitch), std::move(soundLength),
        neotlk::storageFormatToken(table.storageFormat()),
        table.supportsLanguageId() ? std::to_string(table.language()) : std::string(),
        neotlk::textEncodingName(entry.textEncoding),
    };
}

class EntryDialog final : public wxDialog {
public:
    EntryDialog(wxWindow* parent, const wxString& title, neotlk::UInt32 strRef,
                const neotlk::TalkString* source, bool numericSoundIdMode,
                bool soundMetadataEnabled)
        : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          numericSoundIdMode_(numericSoundIdMode),
          soundMetadataEnabled_(soundMetadataEnabled) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* mainRow = new wxBoxSizer(wxHORIZONTAL);

        auto* leftColumn = new wxBoxSizer(wxVERTICAL);
        auto* textBox = new wxStaticBoxSizer(wxVERTICAL, this, wxString::Format("Entry Text (%u)", strRef));
        text_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_DONTWRAP);
        textBox->Add(text_, 1, wxEXPAND | wxALL, FromDIP(6));
        leftColumn->Add(textBox, 1, wxEXPAND | wxBOTTOM, FromDIP(8));

        auto* resRefBox = new wxStaticBoxSizer(
            wxVERTICAL, this,
            soundMetadataEnabled_ ? (numericSoundIdMode_ ? "Sound ID" : "Sound") : "Sound metadata (not stored by this TLK format)");
        resRef_ = new wxTextCtrl(this, wxID_ANY);
        resRef_->SetMaxLength(numericSoundIdMode_ ? 10 : 16);
        resRefBox->Add(resRef_, 0, wxEXPAND | wxALL, FromDIP(6));
        leftColumn->Add(resRefBox, 0, wxEXPAND);
        mainRow->Add(leftColumn, 1, wxEXPAND | wxRIGHT, FromDIP(8));

        auto* rightColumn = new wxBoxSizer(wxVERTICAL);
        auto* flagsBox = new wxStaticBoxSizer(
            wxVERTICAL, this,
            soundMetadataEnabled_ ? "Flags" : "Flags (not stored by this TLK format)");
        textFlag_ = new wxCheckBox(this, wxID_ANY, "Text present");
        soundFlag_ = new wxCheckBox(this, wxID_ANY, "Sound present");
        lengthFlag_ = new wxCheckBox(this, wxID_ANY, "Sound length present");
        flagsBox->Add(textFlag_, 0, wxEXPAND | wxALL, FromDIP(4));
        flagsBox->Add(soundFlag_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
        flagsBox->Add(lengthFlag_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
        rightColumn->Add(flagsBox, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        auto* soundBox = new wxStaticBoxSizer(wxVERTICAL, this, "Sound settings");
        auto* soundGrid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
        soundGrid->AddGrowableCol(1, 1);
        soundGrid->Add(new wxStaticText(this, wxID_ANY, "Volume"), 0, wxALIGN_CENTER_VERTICAL);
        volume_ = new wxTextCtrl(this, wxID_ANY, "0");
        soundGrid->Add(volume_, 1, wxEXPAND);
        soundGrid->Add(new wxStaticText(this, wxID_ANY, "Pitch"), 0, wxALIGN_CENTER_VERTICAL);
        pitch_ = new wxTextCtrl(this, wxID_ANY, "0");
        soundGrid->Add(pitch_, 1, wxEXPAND);
        soundGrid->Add(new wxStaticText(this, wxID_ANY, "Length"), 0, wxALIGN_CENTER_VERTICAL);
        length_ = new wxTextCtrl(this, wxID_ANY, "0");
        soundGrid->Add(length_, 1, wxEXPAND);
        soundBox->Add(soundGrid, 0, wxEXPAND | wxALL, FromDIP(6));
        rightColumn->Add(soundBox, 0, wxEXPAND);
        mainRow->Add(rightColumn, 0, wxEXPAND);
        if (!soundMetadataEnabled_) {
            textFlag_->Enable(false);
            resRef_->Enable(false);
            soundFlag_->Enable(false);
            lengthFlag_->Enable(false);
            volume_->Enable(false);
            pitch_->Enable(false);
            length_->Enable(false);
        }
        root->Add(mainRow, 1, wxEXPAND | wxALL, FromDIP(10));

        auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
        if (buttons) {
            root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
        }
        SetSizer(root);
        SetMinSize(FromDIP(wxSize(560, 360)));
        SetInitialSize(FromDIP(wxSize(680, 440)));

        if (source) {
            text_->SetValue(wxui::toWx(source->text));
            textFlag_->SetValue((source->flags & neotlk::TEXT_PRESENT) != 0);
            soundFlag_->SetValue(soundMetadataEnabled_ && (source->flags & neotlk::SND_PRESENT) != 0);
            lengthFlag_->SetValue(soundMetadataEnabled_ && (source->flags & neotlk::SNDLENGTH_PRESENT) != 0);
            if (soundMetadataEnabled_) {
                resRef_->SetValue(wxui::toWx(source->soundString()));
                volume_->SetValue(wxui::toWx(std::to_string(source->volumeVariance)));
                pitch_->SetValue(wxui::toWx(std::to_string(source->pitchVariance)));
                length_->SetValue(wxui::toWx(std::to_string(source->soundLength)));
            }
        } else {
            textFlag_->SetValue(true);
            soundFlag_->SetValue(soundMetadataEnabled_);
            lengthFlag_->SetValue(soundMetadataEnabled_);
        }
        CentreOnParent();
        text_->SetFocus();
    }

    neotlk::TalkString entry() const {
        neotlk::TalkString result;
        result.flags = 0;
        if (textFlag_->GetValue()) result.flags |= neotlk::TEXT_PRESENT;
        if (soundMetadataEnabled_ && soundFlag_->GetValue()) result.flags |= neotlk::SND_PRESENT;
        if (soundMetadataEnabled_ && lengthFlag_->GetValue()) result.flags |= neotlk::SNDLENGTH_PRESENT;
        result.text = wxui::toStd(text_->GetValue());
        if (soundMetadataEnabled_) {
            const std::string soundValue = wxui::toStd(resRef_->GetValue());
            if (numericSoundIdMode_) {
                result.soundId = soundValue.empty() ? 0xffffffffu : parseUInt32(soundValue, "sound id");
            } else {
                result.soundResref = neotlk::ResRef::fromString(soundValue);
            }
            result.volumeVariance = parseUInt32(wxui::toStd(volume_->GetValue()), "volume");
            result.pitchVariance = parseUInt32(wxui::toStd(pitch_->GetValue()), "pitch");
            result.soundLength = parseFloat(wxui::toStd(length_->GetValue()), "sound length");
        }
        return result;
    }

private:
    wxTextCtrl* text_ = nullptr;
    wxCheckBox* textFlag_ = nullptr;
    wxCheckBox* soundFlag_ = nullptr;
    wxCheckBox* lengthFlag_ = nullptr;
    wxTextCtrl* resRef_ = nullptr;
    bool numericSoundIdMode_ = false;
    bool soundMetadataEnabled_ = true;
    wxTextCtrl* volume_ = nullptr;
    wxTextCtrl* pitch_ = nullptr;
    wxTextCtrl* length_ = nullptr;
};

class SearchDialog final : public wxDialog {
public:
    SearchDialog(wxWindow* parent, neotlk::UInt32 minimumStrRef,
                 neotlk::UInt32 maximumStrRef, bool hasEntries,
                 bool soundMetadataEnabled)
        : wxDialog(parent, wxID_ANY, "Search", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          minimumStrRef_(minimumStrRef), maximumStrRef_(maximumStrRef),
          hasEntries_(hasEntries), soundMetadataEnabled_(soundMetadataEnabled) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* body = new wxBoxSizer(wxHORIZONTAL);

        auto* textBox = new wxStaticBoxSizer(wxVERTICAL, this, "Text");
        auto* form = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
        form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, "Text string:"), 0, wxALIGN_CENTER_VERTICAL);
        text_ = new wxTextCtrl(this, wxID_ANY);
        form->Add(text_, 1, wxEXPAND);
        form->Add(new wxStaticText(this, wxID_ANY, "Sound:"), 0, wxALIGN_CENTER_VERTICAL);
        auto* soundRow = new wxBoxSizer(wxHORIZONTAL);
        resRef_ = new wxTextCtrl(this, wxID_ANY);
        resRef_->SetMaxLength(16);
        soundRow->Add(resRef_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
        anySound_ = new wxCheckBox(this, wxID_ANY, "Match entries with any sound");
        soundRow->Add(anySound_, 0, wxALIGN_CENTER_VERTICAL);
        form->Add(soundRow, 1, wxEXPAND);
        form->Add(new wxStaticText(this, wxID_ANY, "StrRef interval:"), 0, wxALIGN_CENTER_VERTICAL);
        auto* interval = new wxBoxSizer(wxHORIZONTAL);
        start_ = new wxTextCtrl(this, wxID_ANY, wxString::Format("%u", minimumStrRef_));
        stop_ = new wxTextCtrl(this, wxID_ANY, wxString::Format("%u", maximumStrRef_));
        all_ = new wxCheckBox(this, wxID_ANY, "Search all entries");
        interval->Add(start_, 0, wxRIGHT, FromDIP(4));
        interval->Add(new wxStaticText(this, wxID_ANY, "to"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        interval->Add(stop_, 0, wxRIGHT, FromDIP(10));
        interval->Add(all_, 0, wxALIGN_CENTER_VERTICAL);
        form->Add(interval, 1, wxEXPAND);
        textBox->Add(form, 1, wxEXPAND | wxALL, FromDIP(8));
        body->Add(textBox, 1, wxEXPAND | wxRIGHT, FromDIP(8));

        auto* filters = new wxStaticBoxSizer(wxVERTICAL, this, "Filters");
        matchCase_ = new wxCheckBox(this, wxID_ANY, "Text is case sensitive");
        negate_ = new wxCheckBox(this, wxID_ANY, "Negate text match");
        exact_ = new wxCheckBox(this, wxID_ANY, "Exact text match");
        newOnly_ = new wxCheckBox(this, wxID_ANY, "New entries only");
        noBlank_ = new wxCheckBox(this, wxID_ANY, "Filter blank strings");
        word_ = new wxCheckBox(this, wxID_ANY, "Match individual words");
        for (auto* check : {matchCase_, negate_, exact_, newOnly_, noBlank_, word_}) {
            filters->Add(check, 0, wxEXPAND | wxALL, FromDIP(3));
        }
        body->Add(filters, 0, wxEXPAND);
        root->Add(body, 1, wxEXPAND | wxALL, FromDIP(10));

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* search = new wxButton(this, wxID_OK, "&Search");
        auto* resetButton = new wxButton(this, wxID_CLEAR, "&Reset");
        buttons->Add(search, 0, wxRIGHT, FromDIP(6));
        buttons->Add(resetButton, 0, wxRIGHT, FromDIP(6));
        buttons->Add(new wxButton(this, wxID_CANCEL, "&Cancel"), 0);
        root->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
        search->SetDefault();
        SetSizer(root);
        SetMinSize(FromDIP(wxSize(560, 220)));
        SetInitialSize(FromDIP(wxSize(720, 260)));

        all_->SetValue(true);
        all_->Enable(hasEntries_);
        start_->Enable(false);
        stop_->Enable(false);
        all_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            start_->Enable(!all_->GetValue());
            stop_->Enable(!all_->GetValue());
        });
        word_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (word_->GetValue()) exact_->SetValue(false);
            exact_->Enable(!word_->GetValue());
        });
        anySound_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (anySound_->GetValue()) resRef_->Clear();
            resRef_->Enable(soundMetadataEnabled_ && !anySound_->GetValue());
        });
        if (!soundMetadataEnabled_) {
            resRef_->Enable(false);
            anySound_->Enable(false);
        }
        resetButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { reset(); });
        CentreOnParent();
    }

    neotlk::SearchOptions options() const {
        neotlk::SearchOptions opts;
        opts.text = wxui::toStd(text_->GetValue());
        opts.soundResRef = wxui::toStd(resRef_->GetValue());
        opts.start = all_->GetValue() ? minimumStrRef_ : parseUInt32(wxui::toStd(start_->GetValue()), "search start");
        opts.stop = all_->GetValue() ? maximumStrRef_ : parseUInt32(wxui::toStd(stop_->GetValue()), "search stop");
        opts.matchCase = matchCase_->GetValue();
        opts.negateTextMatch = negate_->GetValue();
        opts.exactTextMatch = exact_->GetValue();
        opts.wordSearch = word_->GetValue();
        opts.newEntriesOnly = newOnly_->GetValue();
        opts.filterBlankStrings = noBlank_->GetValue();
        opts.matchEntriesWithAnySound = anySound_->GetValue();
        return opts;
    }

private:
    void reset() {
        text_->Clear();
        resRef_->Clear();
        matchCase_->SetValue(false);
        negate_->SetValue(false);
        exact_->SetValue(false);
        exact_->Enable(true);
        word_->SetValue(false);
        newOnly_->SetValue(false);
        noBlank_->SetValue(false);
        anySound_->SetValue(false);
        resRef_->Enable(soundMetadataEnabled_);
        all_->SetValue(true);
        start_->SetValue(wxString::Format("%u", minimumStrRef_));
        stop_->SetValue(wxString::Format("%u", maximumStrRef_));
        start_->Enable(false);
        stop_->Enable(false);
        text_->SetFocus();
    }

    neotlk::UInt32 minimumStrRef_ = 0;
    neotlk::UInt32 maximumStrRef_ = 0;
    bool hasEntries_ = false;
    bool soundMetadataEnabled_ = true;
    wxTextCtrl* text_ = nullptr;
    wxTextCtrl* resRef_ = nullptr;
    wxTextCtrl* start_ = nullptr;
    wxTextCtrl* stop_ = nullptr;
    wxCheckBox* all_ = nullptr;
    wxCheckBox* matchCase_ = nullptr;
    wxCheckBox* negate_ = nullptr;
    wxCheckBox* exact_ = nullptr;
    wxCheckBox* word_ = nullptr;
    wxCheckBox* newOnly_ = nullptr;
    wxCheckBox* noBlank_ = nullptr;
    wxCheckBox* anySound_ = nullptr;
};

class LanguageDialog final : public wxDialog {
public:
    explicit LanguageDialog(wxWindow* parent, neotlk::UInt32 language)
        : wxDialog(parent, wxID_ANY, "Set TLK file language ID", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(new wxStaticText(this, wxID_ANY, "Please specify what language id this TLK file uses:"),
                  0, wxEXPAND | wxALL, FromDIP(10));
        wxArrayString languages;
        languages.Add("English (0)");
        languages.Add(wxString::FromUTF8("Français (1)"));
        languages.Add("Deutsch (2)");
        languages.Add("Italiano (3)");
        languages.Add(wxString::FromUTF8("Español (4)"));
        languages.Add("Polski (5)");
        choice_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, languages);
        choice_->SetSelection(language <= 5 ? static_cast<int>(language) : 0);
        root->Add(choice_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
        auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
        if (buttons) {
            root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
        }
        SetSizerAndFit(root);
        SetMinSize(FromDIP(wxSize(360, 140)));
        CentreOnParent();
    }

    neotlk::UInt32 language() const { return static_cast<neotlk::UInt32>(choice_->GetSelection()); }

private:
    wxChoice* choice_ = nullptr;
};

class ConfirmDeleteDialog final : public wxDialog {
public:
    ConfirmDeleteDialog(wxWindow* parent, neotlk::UInt32 ref, bool reindexesFollowingEntries)
        : wxDialog(parent, wxID_ANY, "Confirm Delete Entry", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* header = new wxStaticText(this, wxID_ANY, "Delete entry");
        wxFont hfont = header->GetFont();
        hfont.SetWeight(wxFONTWEIGHT_BOLD);
        header->SetFont(hfont);
        root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
        auto* body = new wxBoxSizer(wxHORIZONTAL);
        auto* warning = new wxStaticText(this, wxID_ANY, "Warning:");
        wxFont wfont = warning->GetFont();
        wfont.SetWeight(wxFONTWEIGHT_BOLD);
        warning->SetFont(wfont);
        body->Add(warning, 0, wxRIGHT, FromDIP(10));
        const wxString warningText = reindexesFollowingEntries
            ? wxString::Format("Deleting StrRef %u will automatically adjust the StrRef values of all entries that come after it in this sequential TLK table.\n\nOnly click OK if you are very sure of what you are doing. Deleting a standard entry can cause game text to reference the wrong strings.", ref)
            : wxString::Format("Deleting string ID %u will remove only that sparse Dragon Age TLK record. Other string IDs will remain unchanged.\n\nOnly click OK if you are sure this string is no longer referenced.", ref);
        auto* message = new wxStaticText(this, wxID_ANY, warningText);
        message->Wrap(FromDIP(520));
        body->Add(message, 1, wxEXPAND);
        root->Add(body, 1, wxEXPAND | wxALL, FromDIP(12));
        auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
        if (buttons) {
            root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        }
        SetSizer(root);
        SetMinSize(FromDIP(wxSize(460, 230)));
        SetInitialSize(FromDIP(wxSize(620, 260)));
        CentreOnParent();
    }
};

class PadDialog final : public wxDialog {
public:
    PadDialog(wxWindow* parent, neotlk::UInt32 current)
        : wxDialog(parent, wxID_ANY, "Pad to StrRef", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY, "Pad to StrRef");
        wxFont font = title->GetFont();
        font.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(font);
        root->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
        auto* info = new wxStaticText(this, wxID_ANY,
            "This function will let you insert blank entries into the TLK file up until the StrRef you enter below.\n\nThis can be used to reserve your own range of entries that does not follow directly after the standard entries, making it easier to re-append your own strings after the developers have released a new version of the game.");
        info->Wrap(FromDIP(620));
        root->Add(info, 1, wxEXPAND | wxALL, FromDIP(12));
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "StrRef number to pad up to:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        value_ = new wxTextCtrl(this, wxID_ANY, wxString::Format("%u", current + 1));
        row->Add(value_, 0, wxRIGHT, FromDIP(12));
        row->Add(new wxStaticText(this, wxID_ANY, wxString::Format("Current range: 0 to %u", current == 0 ? 0 : current - 1)), 1, wxALIGN_CENTER_VERTICAL);
        root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
        if (buttons) {
            root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        }
        SetSizer(root);
        SetMinSize(FromDIP(wxSize(520, 250)));
        SetInitialSize(FromDIP(wxSize(720, 300)));
        CentreOnParent();
    }

    neotlk::UInt32 target() const { return parseUInt32(wxui::toStd(value_->GetValue()), "target StrRef"); }

private:
    wxTextCtrl* value_ = nullptr;
};

class NeoTLKFrame final : public wxFrame {
public:
    NeoTLKFrame()
        : wxFrame(nullptr, wxID_ANY, "NeoTLK v1.0.0 (TLK file editor)", wxDefaultPosition, wxDefaultSize) {
        setApplicationIcon();
        buildMenus();
        buildLayout();
        wxui::createStatusBar(*this, 1);
        createDocumentTab(true);
        setStatus("Please use the File menu to open a TLK file to display.");
        updateLoadedState();
        layoutColumns();
        darkMode_ = wxui::readDarkMode(kAppName);
        fontScale_ = settings_.fontScale();
        fontScaleWheelFilter_.attach(this, [this](int steps) { changeFontScaleSteps(steps); });
        neoview::bindFontScaleDpiRefresh(this, [this]() { applyFontScale(); });
        applyDarkMode();
    }

private:

    struct DocumentTab {
        std::unique_ptr<neotlk::TalkTable> table = std::make_unique<neotlk::TalkTable>();
        std::vector<neotlk::UInt32> displayStrRefs;
        neoview::DocumentViewState viewState;
        std::string untitledName = "Untitled TLK";
        wxWindow* tabPage = nullptr;
    };

    bool hasActiveDocument() const {
        return activeDocumentIndex_ != neotabs::npos && activeDocumentIndex_ < documents_.size();
    }

    DocumentTab& activeDocument() { return documents_.at(activeDocumentIndex_); }
    const DocumentTab& activeDocument() const { return documents_.at(activeDocumentIndex_); }
    neotlk::TalkTable& table() { return *activeDocument().table; }
    const neotlk::TalkTable& table() const { return *activeDocument().table; }
    std::vector<neotlk::UInt32>& displayStrRefs() { return activeDocument().displayStrRefs; }
    const std::vector<neotlk::UInt32>& displayStrRefs() const { return activeDocument().displayStrRefs; }
    neoview::DocumentViewState& viewState() { return activeDocument().viewState; }
    const neoview::DocumentViewState& viewState() const { return activeDocument().viewState; }

    bool tabDirty(const DocumentTab& tab) const { return tab.table && tab.table->fileExists() && tab.table->modified(); }

    std::string tabDisplayName(const DocumentTab& tab) const {
        if (tab.table && tab.table->fileExists()) {
            const std::filesystem::path path(tab.table->hasSaveTarget() ? tab.table->saveTargetFilename() : tab.table->filename());
            return neotabs::displayNameForPath(path, tab.untitledName);
        }
        return tab.untitledName;
    }

    void updateActiveTabTitle() {
        if (!hasActiveDocument()) return;
        neotabs::setTabLabel(documentTabs_, activeDocument().tabPage, tabDisplayName(activeDocument()), tabDirty(activeDocument()));
    }

    void createDocumentTab(bool select = true) {
        DocumentTab tab;
        tab.table = std::make_unique<neotlk::TalkTable>();
        tab.viewState.resetForNewDocument();
        const std::size_t previousActiveIndex = activeDocumentIndex_;
        documents_.push_back(std::move(tab));
        const std::size_t index = documents_.size() - 1;

        tabSwitchInProgress_ = true;
        wxWindow* const page = neotabs::addTabPage(
            documentTabs_, tabDisplayName(documents_.back()), tabDirty(documents_.back()), select);
        if (page != nullptr) documents_.back().tabPage = page;
        tabSwitchInProgress_ = false;

        if (page == nullptr) {
            documents_.pop_back();
            activeDocumentIndex_ = previousActiveIndex;
            throw std::runtime_error("Unable to create a document tab.");
        }

        if (select) {
            activeDocumentIndex_ = index;
            tabSwitchInProgress_ = true;
            neotabs::changeSelectionToPage(documentTabs_, page);
            tabSwitchInProgress_ = false;
            updateLoadedState();
            if (table().fileExists()) refreshCurrentInterval();
        }
    }

    bool activeTabIsReusableForOpen() const {
        return hasActiveDocument() && documents_.size() == 1 && !tabDirty(activeDocument()) && !table().fileExists();
    }

    void ensureDocumentTabForOpen() {
        if (!hasActiveDocument()) { createDocumentTab(true); return; }
        if (!activeTabIsReusableForOpen()) createDocumentTab(true);
    }

    void selectDocumentTab(std::size_t index) {
        if (documentTabs_ == nullptr || index >= documents_.size()) return;
        tabSwitchInProgress_ = true;
        const bool selected = neotabs::changeSelectionToPage(documentTabs_, documents_[index].tabPage);
        tabSwitchInProgress_ = false;
        if (!selected) return;
        activeDocumentIndex_ = index;
        updateLoadedState();
        if (table().fileExists()) refreshCurrentInterval();
        updateActiveTabTitle();
    }

    bool confirmCloseDocumentTab(std::size_t index) {
        if (index >= documents_.size()) return true;
        if (!tabDirty(documents_[index])) return true;
        return wxui::confirm(this, "Close tab", neotabs::closePromptText(tabDisplayName(documents_[index])));
    }

    bool closeDocumentTab(std::size_t index) {
        if (index >= documents_.size() || !confirmCloseDocumentTab(index)) return false;

        wxWindow* const page = documents_[index].tabPage;
        tabSwitchInProgress_ = true;
        const bool deleted = neotabs::deleteTabPage(documentTabs_, page);
        tabSwitchInProgress_ = false;
        if (!deleted) return false;

        documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
        if (documents_.empty()) {
            activeDocumentIndex_ = neotabs::npos;
            createDocumentTab(true);
            return true;
        }

        std::size_t selectedIndex = neotabs::findDocumentIndexForPage(
            documents_, neotabs::currentPage(documentTabs_));
        if (selectedIndex == neotabs::npos) selectedIndex = std::min(index, documents_.size() - 1);
        selectDocumentTab(selectedIndex);
        return true;
    }

    bool confirmCloseAllTabs() {
        for (std::size_t i = 0; i < documents_.size(); ++i) {
            if (!confirmCloseDocumentTab(i)) return false;
        }
        return true;
    }

    void onDocumentTabChanged(wxAuiNotebookEvent& event) {
        if (tabSwitchInProgress_) { event.Skip(); return; }
        const int selection = event.GetSelection();
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::pageForIndex(documentTabs_, selection));
        if (index != neotabs::npos) selectDocumentTab(index);
        event.Skip();
    }

    void onDocumentTabCloseRequested(wxAuiNotebookEvent& event) {
        event.Veto();
        const int selection = event.GetSelection();
        if (selection < 0) return;
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::pageForIndex(documentTabs_, selection));
        if (index != neotabs::npos) closeDocumentTab(index);
    }

    enum : int {
        ID_New = wxID_HIGHEST + 1,
        ID_Open,
        ID_SaveAs,
        ID_Save,
        ID_CloseTab,
        ID_CloseOtherTabs,
        ID_NextTab,
        ID_PreviousTab,
        ID_DocumentTabs,
        ID_Show,
        ID_Search,
        ID_NewEntry,
        ID_EditRow,
        ID_AppendFile,
        ID_SetLanguage,
        ID_DeleteSelected,
        ID_PadToStrRef,
        ID_Copy,
        ID_Cut,
        ID_Paste,
        ID_SelectAll,
        ID_Filter,
        ID_ClearFilter,
        ID_FilterColumn,
        ID_ClearColumnFilter,
        ID_ClearAllFilters,
        ID_ResetColumnOrder,
        ID_ResetRowOrder,
        ID_ImportCsv,
        ID_ImportTsv,
        ID_ImportXml,
        ID_ImportJson,
        ID_ExportCsv,
        ID_ExportTsv,
        ID_ExportXml,
        ID_ExportJson,
        ID_ExportTslPatcher,
        ID_ExportHoloPatcher,
        ID_DarkMode,
        ID_FontIncrease,
        ID_FontDecrease,
        ID_FontReset,
    };

    static constexpr int kRecentFileBaseId = wxID_HIGHEST + 1000;
    static constexpr int kClearRecentFilesId = kRecentFileBaseId + neosettings::kMaxRecentFiles;

    void rebuildRecentFilesMenu() {
        if (recentFilesMenu_ != nullptr) {
            neosettings::populateRecentFilesMenu(*recentFilesMenu_, settings_, kRecentFileBaseId, kClearRecentFilesId);
        }
    }

    void rememberRecentFile(const std::filesystem::path& path) {
        settings_.addRecentFile(path);
        rebuildRecentFilesMenu();
    }

    void onOpenRecent(wxCommandEvent& event) {
        const int index = event.GetId() - kRecentFileBaseId;
        const auto files = settings_.recentFiles();
        if (index < 0 || static_cast<std::size_t>(index) >= files.size()) return;
        try {
            if (!std::filesystem::exists(files[static_cast<std::size_t>(index)])) {
                settings_.removeRecentFile(files[static_cast<std::size_t>(index)]);
                rebuildRecentFilesMenu();
                throw std::runtime_error("Recent file no longer exists: " + files[static_cast<std::size_t>(index)].string());
            }
            loadFile(files[static_cast<std::size_t>(index)].string());
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onClearRecentFiles(wxCommandEvent&) {
        settings_.clearRecentFiles();
        rebuildRecentFilesMenu();
    }

    void setApplicationIcon() {
        wxIconBundle bundle;
#if defined(__WXMSW__)
        wxIcon windowsIcon("neotlk", wxBITMAP_TYPE_ICO_RESOURCE);
        if (windowsIcon.IsOk()) {
            bundle.AddIcon(windowsIcon);
        }
#endif
        wxIcon fallbackIcon(neotlk_icon_xpm);
        if (fallbackIcon.IsOk()) {
            bundle.AddIcon(fallbackIcon);
        }
        if (bundle.GetIconCount() > 0) {
            SetIcons(bundle);
        }
    }

    std::unique_ptr<neogames::OpenGameDirectoryMenu> gameDirectoryMenu_;

    void buildMenus() {
        auto* file = new wxMenu;
        file->Append(ID_New, "&New...");
        file->Append(ID_Open, "&Open...\tCtrl+O");
        recentFilesMenu_ = new wxMenu;
        rebuildRecentFilesMenu();
        file->AppendSubMenu(recentFilesMenu_, "Open &Recent");
        file->AppendSeparator();
        file->Append(ID_Save, "&Save\tCtrl+S");
        file->Append(ID_SaveAs, "S&ave as...\tCtrl+Shift+S");
        file->AppendSeparator();
        file->Append(ID_CloseTab, "&Close Tab\tCtrl-W");
        file->Append(ID_CloseOtherTabs, "Close &Other Tabs");
        file->Append(ID_NextTab, "Next Tab\tCtrl-Tab");
        file->Append(ID_PreviousTab, "Previous Tab\tCtrl-Shift-Tab");
        file->AppendSeparator();
        file->Append(ID_SetLanguage, "Set &language...\tCtrl+L");
        gameDirectoryMenu_ = neogames::appendOpenGameDirectoryMenu(
            *this, *file, [this](const std::filesystem::path& directory) {
                chooseAndOpenTlk(directory);
            });
        file->AppendSeparator();
        file->Append(wxID_EXIT, "&Quit\tCtrl+Q");

        auto* import = new wxMenu;
        import->Append(ID_ImportCsv, "Import &CSV...");
        import->Append(ID_ImportTsv, "Import &TSV...");
        import->Append(ID_ImportXml, "Import &XML...");
        import->Append(ID_ImportJson, "Import &JSON...");

        auto* exportMenu = new wxMenu;
        exportMenu->Append(ID_ExportCsv, "Export as &CSV...");
        exportMenu->Append(ID_ExportTsv, "Export as &TSV...");
        exportMenu->Append(ID_ExportXml, "Export as &XML...");
        exportMenu->Append(ID_ExportJson, "Export as &JSON...");
        exportMenu->AppendSeparator();
        exportMenu->Append(ID_ExportTslPatcher, "Export &TSLPatcher Package...");
        exportMenu->Append(ID_ExportHoloPatcher, "Export &HoloPatcher Package...");

        auto* edit = new wxMenu;
        edit->Append(ID_Cut, "Cu&t\tCtrl+X");
        edit->Append(ID_Copy, "&Copy\tCtrl+C");
        edit->Append(ID_Paste, "&Paste\tCtrl+V");
        edit->AppendSeparator();
        edit->Append(ID_SelectAll, "Select &all\tCtrl+A");

        auto* tools = new wxMenu;
        tools->Append(ID_Search, "Advanced S&earch...\tCtrl+F");
        tools->Append(ID_Filter, "&Filter/Search term...");
        tools->Append(ID_FilterColumn, "Filter Selected &Column...");
        tools->Append(ID_ClearColumnFilter, "Clear Filter on Selected Column");
        tools->Append(ID_ClearAllFilters, "Clear All Filters");
        tools->AppendSeparator();
        tools->Append(ID_NewEntry, "&New entry...\tCtrl+N");
        tools->Append(ID_EditRow, "&Edit selected entry...\tCtrl+E");
        tools->Append(ID_DeleteSelected, "Delete selected entry...");
        tools->AppendSeparator();
        tools->Append(ID_PadToStrRef, "Pad to StrRef...");
        tools->AppendSeparator();
        tools->Append(ID_AppendFile, "Append file...");

        auto* view = new wxMenu;
        darkModeItem_ = view->AppendCheckItem(ID_DarkMode, "&Dark Mode");
        view->AppendSeparator();
        view->Append(ID_FontIncrease, "Increase Font Size\tCtrl++");
        view->Append(ID_FontDecrease, "Decrease Font Size\tCtrl+-");
        view->Append(ID_FontReset, "Reset Font Size\tCtrl+0");
        view->AppendSeparator();
        view->Append(ID_ResetColumnOrder, "Reset Column Order");
        view->Append(ID_ResetRowOrder, "Reset Row Order");

        auto* help = new wxMenu;
        help->Append(wxID_ABOUT, "&About");

        auto* bar = new wxMenuBar;
        bar->Append(file, "&File");
        bar->Append(import, "&Import");
        bar->Append(exportMenu, "&Export");
        bar->Append(edit, "&Edit");
        bar->Append(tools, "&Tools");
        bar->Append(view, "&View");
        bar->Append(help, "&Help");
        SetMenuBar(bar);

        Bind(wxEVT_MENU, &NeoTLKFrame::onNew, this, ID_New);
        Bind(wxEVT_MENU, &NeoTLKFrame::onOpen, this, ID_Open);
        Bind(wxEVT_MENU, &NeoTLKFrame::onOpenRecent, this, kRecentFileBaseId, kRecentFileBaseId + neosettings::kMaxRecentFiles - 1);
        Bind(wxEVT_MENU, &NeoTLKFrame::onClearRecentFiles, this, kClearRecentFilesId);
        Bind(wxEVT_MENU, &NeoTLKFrame::onSave, this, ID_Save);
        Bind(wxEVT_MENU, &NeoTLKFrame::onSaveAs, this, ID_SaveAs);
        Bind(wxEVT_MENU, &NeoTLKFrame::onCloseTab, this, ID_CloseTab);
        Bind(wxEVT_MENU, &NeoTLKFrame::onCloseOtherTabs, this, ID_CloseOtherTabs);
        Bind(wxEVT_MENU, &NeoTLKFrame::onNextTab, this, ID_NextTab);
        Bind(wxEVT_MENU, &NeoTLKFrame::onPreviousTab, this, ID_PreviousTab);
        Bind(wxEVT_MENU, &NeoTLKFrame::onSearch, this, ID_Search);
        Bind(wxEVT_MENU, &NeoTLKFrame::onNewEntry, this, ID_NewEntry);
        Bind(wxEVT_MENU, &NeoTLKFrame::onEditRow, this, ID_EditRow);
        Bind(wxEVT_MENU, &NeoTLKFrame::onAppendFile, this, ID_AppendFile);
        Bind(wxEVT_MENU, &NeoTLKFrame::onSetLanguage, this, ID_SetLanguage);
        Bind(wxEVT_MENU, &NeoTLKFrame::onDeleteSelected, this, ID_DeleteSelected);
        Bind(wxEVT_MENU, &NeoTLKFrame::onPadToStrRef, this, ID_PadToStrRef);
        Bind(wxEVT_MENU, &NeoTLKFrame::onCopy, this, ID_Copy);
        Bind(wxEVT_MENU, &NeoTLKFrame::onCut, this, ID_Cut);
        Bind(wxEVT_MENU, &NeoTLKFrame::onPaste, this, ID_Paste);
        Bind(wxEVT_MENU, &NeoTLKFrame::onSelectAll, this, ID_SelectAll);
        Bind(wxEVT_MENU, &NeoTLKFrame::onFilterPrompt, this, ID_Filter);
        Bind(wxEVT_MENU, &NeoTLKFrame::onClearFilter, this, ID_ClearFilter);
        Bind(wxEVT_MENU, &NeoTLKFrame::onFilterSelectedColumn, this, ID_FilterColumn);
        Bind(wxEVT_MENU, &NeoTLKFrame::onClearSelectedColumnFilter, this, ID_ClearColumnFilter);
        Bind(wxEVT_MENU, &NeoTLKFrame::onClearAllFilters, this, ID_ClearAllFilters);
        Bind(wxEVT_MENU, &NeoTLKFrame::onResetColumnOrder, this, ID_ResetColumnOrder);
        Bind(wxEVT_MENU, &NeoTLKFrame::onResetRowOrder, this, ID_ResetRowOrder);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Csv); }, ID_ImportCsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Tsv); }, ID_ImportTsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Xml); }, ID_ImportXml);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Json); }, ID_ImportJson);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Csv); }, ID_ExportCsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Tsv); }, ID_ExportTsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Xml); }, ID_ExportXml);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Json); }, ID_ExportJson);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExportTlkPatcher(neotlk::TlkPatcherCompatibility::TslPatcher); }, ID_ExportTslPatcher);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExportTlkPatcher(neotlk::TlkPatcherCompatibility::HoloPatcher); }, ID_ExportHoloPatcher);
        Bind(wxEVT_MENU, &NeoTLKFrame::onToggleDarkMode, this, ID_DarkMode);
        Bind(wxEVT_MENU, &NeoTLKFrame::onIncreaseFontScale, this, ID_FontIncrease);
        Bind(wxEVT_MENU, &NeoTLKFrame::onDecreaseFontScale, this, ID_FontDecrease);
        Bind(wxEVT_MENU, &NeoTLKFrame::onResetFontScale, this, ID_FontReset);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            wxui::showMessage(this, "About NeoTLK", "NeoTLK v1.0.0\nNative wxWidgets TLK editor\n\nA special thanks to everyone in the KOTOR modding community that has contributed their work, knowledge, and creativity to making tools, mods, and guides over the last 20+ years");
        }, wxID_ABOUT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_BUTTON, &NeoTLKFrame::onNew, this, ID_New);
        Bind(wxEVT_BUTTON, &NeoTLKFrame::onOpen, this, ID_Open);
        Bind(wxEVT_BUTTON, &NeoTLKFrame::onSave, this, ID_Save);
        Bind(wxEVT_BUTTON, &NeoTLKFrame::onSaveAs, this, ID_SaveAs);
        Bind(wxEVT_BUTTON, &NeoTLKFrame::onClearFilter, this, ID_ClearFilter);
        Bind(wxEVT_CLOSE_WINDOW, &NeoTLKFrame::onClose, this);
    }

    void buildLayout() {
        auto* panel = new wxPanel(this);
        auto* root = new wxBoxSizer(wxVERTICAL);

        documentTabs_ = new wxAuiNotebook(panel, ID_DocumentTabs, wxDefaultPosition, wxDefaultSize,
                                          wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_SCROLL_BUTTONS);
        root->Add(documentTabs_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
        neotabs::configureDocumentTabStrip(documentTabs_);

        topPanel_ = new wxPanel(panel);
        auto* topRoot = new wxBoxSizer(wxVERTICAL);

        auto* fileRow = new wxBoxSizer(wxHORIZONTAL);
        fileRow->Add(new wxStaticText(topPanel_, wxID_ANY, "TLK file:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        filePath_ = new wxTextCtrl(topPanel_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        fileRow->Add(filePath_, 1, wxEXPAND | wxRIGHT, FromDIP(6));
        fileRow->Add(new wxButton(topPanel_, ID_New, "New"), 0, wxRIGHT, FromDIP(4));
        fileRow->Add(new wxButton(topPanel_, ID_Open, "Open..."), 0, wxRIGHT, FromDIP(4));
        fileRow->Add(new wxButton(topPanel_, ID_Save, "Save"), 0, wxRIGHT, FromDIP(4));
        fileRow->Add(new wxButton(topPanel_, ID_SaveAs, "Save As..."), 0);
        topRoot->Add(fileRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

        auto* intervalRow = new wxBoxSizer(wxHORIZONTAL);
        auto* show = new wxButton(topPanel_, ID_Show, "Show &interval");
        intervalRow->Add(show, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        intervalRow->Add(new wxStaticText(topPanel_, wxID_ANY, "StrRef:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        start_ = new wxTextCtrl(topPanel_, wxID_ANY, "0");
        stop_ = new wxTextCtrl(topPanel_, wxID_ANY, "0");
        start_->SetMinSize(FromDIP(wxSize(80, -1)));
        stop_->SetMinSize(FromDIP(wxSize(80, -1)));
        intervalRow->Add(start_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        intervalRow->Add(new wxStaticText(topPanel_, wxID_ANY, "to"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        intervalRow->Add(stop_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        maxLabel_ = new wxStaticText(topPanel_, wxID_ANY, "(info)");
        intervalRow->Add(maxLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        intervalRow->AddStretchSpacer(1);
        auto* logo = new wxStaticText(topPanel_, wxID_ANY, "NeoTLK");
        wxFont logoFont = logo->GetFont();
        logoFont.SetWeight(wxFONTWEIGHT_BOLD);
        logo->SetFont(logoFont);
        intervalRow->Add(logo, 0, wxALIGN_CENTER_VERTICAL);
        topRoot->Add(intervalRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

        auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
        filterRow->Add(new wxStaticText(topPanel_, wxID_ANY, "Filter:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        filterText_ = new wxTextCtrl(topPanel_, wxID_ANY);
        filterRow->Add(filterText_, 1, wxEXPAND | wxRIGHT, FromDIP(4));
        filterRow->Add(new wxButton(topPanel_, ID_ClearFilter, "Clear"), 0);
        topRoot->Add(filterRow, 0, wxEXPAND | wxALL, FromDIP(8));

        gauge_ = new wxGauge(topPanel_, wxID_ANY, 100);
        topRoot->Add(gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

        grid_ = new wxListCtrl(topPanel_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
        wxui::setColumns(*grid_, {{"StrRef", 72}, {"Entry text", 420}, {"Sound", kSoundColumnWidth}});
        topRoot->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
        topPanel_->SetSizer(topRoot);

        display_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
        root->Add(topPanel_, 2, wxEXPAND);
        root->Add(display_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
        panel->SetSizer(root);

        SetMinSize(FromDIP(wxSize(560, 420)));
        SetInitialSize(FromDIP(wxSize(860, 620)));
        settings_.restoreWindowPlacement(*this);

        show->Bind(wxEVT_BUTTON, &NeoTLKFrame::onShowInterval, this);
        documentTabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &NeoTLKFrame::onDocumentTabChanged, this);
        documentTabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &NeoTLKFrame::onDocumentTabCloseRequested, this);
        if (filterText_) filterText_->Bind(wxEVT_TEXT, &NeoTLKFrame::onFilterText, this);
        grid_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { updateDisplayFromSelection(); });
        grid_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { editSelectedEntry(); });
        grid_->Bind(wxEVT_LIST_COL_RIGHT_CLICK, &NeoTLKFrame::onListColumnRightClick, this);
        grid_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            layoutColumns();
            event.Skip();
        });
    }

    void layoutColumns() {
        if (!grid_) return;
        const int width = std::max(320, grid_->GetClientSize().GetWidth());
        const int strRefWidth = FromDIP(72);
        const int soundWidth = FromDIP(kSoundColumnWidth);
        grid_->SetColumnWidth(0, strRefWidth);
        grid_->SetColumnWidth(2, soundWidth);
        grid_->SetColumnWidth(1, std::max(120, width - strRefWidth - soundWidth - FromDIP(28)));
    }

    void setStatus(const std::string& status) {
        std::string text = status;
        const std::string summary = neoview::columnFilterSummary(viewState());
        if (!summary.empty()) {
            text += "; filters: " + summary;
        }
        wxui::setStatusText(*this, wxui::toWx(text));
    }

    const neotlk::TalkString& entryAt(neotlk::UInt32 strRef) const {
        return static_cast<const neotlk::TalkTable&>(table()).entryAtStrRef(strRef);
    }

    bool confirmDiscard(const std::string& action) {
        if (table().fileExists() && table().modified()) {
            return wxui::confirm(this, action, action + " will discard unsaved changes. Continue?");
        }
        return true;
    }

    void ensureLoaded() const {
        if (!table().fileExists()) {
            throw std::runtime_error("No TLK file is currently open.");
        }
    }

    bool isTlkV40() const noexcept { return table().isVersion40(); }

    neotlk::UInt32 minStrRef() const noexcept { return table().minStrRef(); }
    neotlk::UInt32 maxStrRef() const noexcept { return table().maxStrRef(); }

    std::optional<neotlk::UInt32> nearestExistingStrRef(neotlk::UInt32 target) const {
        if (table().entries().empty()) return std::nullopt;
        std::optional<neotlk::UInt32> atOrAfter;
        neotlk::UInt32 greatest = table().entries().front().strRef;
        for (const auto& entry : table().entries()) {
            greatest = std::max(greatest, entry.strRef);
            if (entry.strRef >= target && (!atOrAfter || entry.strRef < *atOrAfter)) {
                atOrAfter = entry.strRef;
            }
        }
        return atOrAfter ? atOrAfter : std::optional<neotlk::UInt32>(greatest);
    }

    void updateLoadedState() {
        const bool loaded = table().fileExists();
        const bool hasEntries = loaded && table().count() != 0u;
        const bool supportsLanguage = loaded && table().supportsLanguageId();
        const bool canPad = loaded && !table().hasSparseStrRefs();
        const bool supportsKotORPatcher = loaded && !table().isDragonAgeV02() && !table().isVersion40();

        if (auto* menuBar = GetMenuBar()) {
            menuBar->Enable(ID_Save, loaded);
            menuBar->Enable(ID_SaveAs, loaded);
            menuBar->Enable(ID_SetLanguage, supportsLanguage);
            menuBar->Enable(ID_Search, hasEntries);
            menuBar->Enable(ID_Filter, loaded);
            menuBar->Enable(ID_FilterColumn, loaded);
            menuBar->Enable(ID_ClearColumnFilter, loaded);
            menuBar->Enable(ID_ClearAllFilters, loaded);
            menuBar->Enable(ID_NewEntry, loaded);
            menuBar->Enable(ID_EditRow, hasEntries);
            menuBar->Enable(ID_DeleteSelected, hasEntries);
            menuBar->Enable(ID_PadToStrRef, canPad);
            menuBar->Enable(ID_AppendFile, loaded);
            menuBar->Enable(ID_ExportCsv, loaded);
            menuBar->Enable(ID_ExportTsv, loaded);
            menuBar->Enable(ID_ExportXml, loaded);
            menuBar->Enable(ID_ExportJson, loaded);
            menuBar->Enable(ID_ExportTslPatcher, supportsKotORPatcher);
            menuBar->Enable(ID_ExportHoloPatcher, supportsKotORPatcher);
        }

        if (filePath_ != nullptr) {
            const std::string path = loaded ? (table().hasSaveTarget() ? table().saveTargetFilename() : table().filename()) : std::string();
            if (wxui::toStd(filePath_->GetValue()) != path) filePath_->ChangeValue(wxui::toWx(path));
        }
        if (!loaded) {
            start_->Enable(false);
            stop_->Enable(false);
            if (filterText_) filterText_->Enable(false);
            maxLabel_->SetLabel("");
            grid_->DeleteAllItems();
            displayStrRefs().clear();
            neoview::setIdentityRows(viewState(), 0);
            wxui::appendRow(*grid_, {"", "Please use the File menu to open a TLK file to display.", ""});
            wxui::applyTheme(grid_, darkMode_);
            grid_->Enable(false);
            display_->Clear();
            return;
        }
        start_->Enable(hasEntries);
        stop_->Enable(hasEntries);
        if (filterText_) filterText_->Enable(true);
        grid_->Enable(true);
        if (!hasEntries) {
            maxLabel_->SetLabel("(0 entries)");
        } else if (table().hasSparseStrRefs()) {
            maxLabel_->SetLabel(wxString::Format("(%u entries; IDs %u-%u)", table().count(), minStrRef(), maxStrRef()));
        } else {
            maxLabel_->SetLabel(wxString::Format("(%u max.)", maxStrRef()));
        }
        updateListColumnLabels();
    }

    void resetGridMessage(const std::string& message) {
        grid_->DeleteAllItems();
        displayStrRefs().clear();
        neoview::setIdentityRows(viewState(), 0);
        display_->Clear();
        wxui::appendRow(*grid_, {"", message, ""});
        wxui::applyTheme(grid_, darkMode_);
    }

    std::string tlkDisplayCell(const neotlk::TalkString& entry, std::size_t logicalColumn) const {
        switch (logicalColumn) {
            case 0: return std::to_string(entry.strRef);
            case 1: return previewText(entry.text);
            case 2: return entry.soundString();
            default: return {};
        }
    }

    std::string tlkFilterCell(const neotlk::TalkString& entry, std::size_t logicalColumn) const {
        switch (logicalColumn) {
            case 0: return std::to_string(entry.strRef);
            case 1: return entry.text;
            case 2: return entry.soundString();
            default: return {};
        }
    }

    bool tlkEntryPassesCurrentFilters(const neotlk::TalkString& entry) const {
        if (!viewState().filterTerm.empty()) {
            neotabular::Table matchTable;
            matchTable.columns = tlkColumns();
            if (!neotabular::rowMatches(matchTable, tlkRow(entry, table()), viewState().filterTerm)) {
                return false;
            }
        }
        return neoview::rowPassesColumnFilters(viewState(), [&](std::size_t logicalColumn) {
            return tlkFilterCell(entry, logicalColumn);
        });
    }

    void updateListColumnLabels() {
        neoview::ensureIdentityColumns(viewState(), 3);
        for (std::size_t visualColumn = 0; visualColumn < 3; ++visualColumn) {
            const std::size_t logicalColumn = neoview::logicalColumnForVisual(viewState(), visualColumn);
            std::string label = tlkDisplayColumnLabel(logicalColumn);
            if (logicalColumn == 2u && table().fileExists() && !table().supportsSoundMetadata()) {
                label = "Sound (not stored)";
            }
            if (neoview::findColumnFilter(viewState(), logicalColumn) != nullptr) {
                label += " *";
            }
            wxListItem item;
            item.SetMask(wxLIST_MASK_TEXT);
            item.SetText(wxui::toWx(label));
            grid_->SetColumn(static_cast<int>(visualColumn), item);
        }
    }

    neotabular::Table filteredTlkTable() const {
        neotabular::Table exportTable;
        exportTable.columns = tlkColumns();
        for (const auto& entry : table().entries()) {
            if (tlkEntryPassesCurrentFilters(entry)) {
                exportTable.rows.push_back(tlkRow(entry, table()));
            }
        }
        neotlk::ensureTlkTabularMetadataRow(table(), exportTable);
        return exportTable;
    }

    void showInterval(neotlk::UInt32 start, neotlk::UInt32 stop) {
        ensureLoaded();
        if (start > stop) {
            throw std::runtime_error("Invalid start value of interval to display!");
        }
        if (table().count() == 0) {
            resetGridMessage("No entries present. Please use the Tools menu to add new entries.");
            return;
        }
        if (stop > maxStrRef()) {
            throw std::runtime_error("Invalid end value of interval to display!");
        }

        grid_->Freeze();
        grid_->DeleteAllItems();
        displayStrRefs().clear();
        neoview::removeColumnFiltersOutsideRange(viewState(), 3);
        neoview::ensureIdentityColumns(viewState(), 3);
        updateListColumnLabels();
        gauge_->SetRange(static_cast<int>(std::max<neotlk::UInt32>(1u, table().count())));
        int progress = 0;
        for (const auto& entry : table().entries()) {
            ++progress;
            if ((progress & 0xff) == 0 || progress == static_cast<int>(table().count())) {
                gauge_->SetValue(progress);
            }
            if (entry.strRef < start || entry.strRef > stop) continue;
            if (!tlkEntryPassesCurrentFilters(entry)) continue;
            displayStrRefs().push_back(entry.strRef);
            wxui::appendRow(*grid_, {tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 0)),
                                    tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 1)),
                                    tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 2))});
        }
        neoview::setIdentityRows(viewState(), displayStrRefs().size());
        grid_->Thaw();
        wxui::applyTheme(grid_, darkMode_);
        gauge_->SetValue(0);
        viewState().searchResultsActive = false;
        std::string status = "Displaying " + std::to_string(displayStrRefs().size()) + " entries between StrRef " + std::to_string(start) + " and " + std::to_string(stop) +
                             " out of a total of " + std::to_string(table().count()) + " entries";
        if (!viewState().filterTerm.empty()) {
            status += " matching filter: " + viewState().filterTerm;
        }
        status += ".";
        setStatus(status);
        if (!displayStrRefs().empty()) {
            wxui::selectRow(*grid_, 0);
            updateDisplayFromSelection();
        } else {
            display_->Clear();
        }
    }

    void showAllEntries() {
        ensureLoaded();
        if (table().count() == 0) {
            start_->SetValue("0");
            stop_->SetValue("0");
            resetGridMessage("No entries present. Please use the Tools menu to add new entries.");
            return;
        }
        start_->SetValue(wxString::Format("%u", minStrRef()));
        stop_->SetValue(wxString::Format("%u", maxStrRef()));
        showInterval(minStrRef(), maxStrRef());
    }

    neotlk::UInt32 selectedStrRef() const {
        const long row = wxui::selectedRow(*grid_);
        if (row < 0 || static_cast<std::size_t>(row) >= displayStrRefs().size()) {
            throw std::runtime_error("No TLK entry row has been selected in the table!");
        }
        return displayStrRefs().at(static_cast<std::size_t>(row));
    }

    void updateDisplayFromSelection() {
        if (!table().fileExists() || displayStrRefs().empty()) return;
        try {
            display_->SetValue(wxui::toWx(displayTextForEntry(entryAt(selectedStrRef()))));
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void refreshCurrentInterval() {
        if (table().count() == 0u) {
            showAllEntries();
            return;
        }
        neotlk::UInt32 start = parseUInt32(wxui::toStd(start_->GetValue()), "interval start");
        neotlk::UInt32 stop = parseUInt32(wxui::toStd(stop_->GetValue()), "interval stop");
        const neotlk::UInt32 minimum = minStrRef();
        const neotlk::UInt32 maximum = maxStrRef();
        if (start < minimum || start > maximum) {
            start = minimum;
            start_->SetValue(wxString::Format("%u", start));
        }
        if (stop > maximum || stop < minimum) {
            stop = maximum;
            stop_->SetValue(wxString::Format("%u", stop));
        }
        if (start > stop) {
            start = minimum;
            stop = maximum;
            start_->SetValue(wxString::Format("%u", start));
            stop_->SetValue(wxString::Format("%u", stop));
        }
        showInterval(start, stop);
    }

    void loadFile(const std::string& file) {
        ensureDocumentTabForOpen();
        table().load(file);
        viewState().resetForNewDocument();
        if (filterText_) filterText_->ChangeValue("");
        SetTitle(wxui::toWx("NeoTLK v1.0.0 (TLK file editor) - " + file));
        start_->SetValue(wxString::Format("%u", table().count() == 0u ? 0u : minStrRef()));
        stop_->SetValue(wxString::Format("%u", table().count() == 0u ? 0u : maxStrRef()));
        updateLoadedState();
        showAllEntries();
        rememberRecentFile(std::filesystem::path(file));
        neogames::resolver().inferFromOpenedPath(std::filesystem::path(file));
        setStatus("Opened file " + file + ". " + neotlk::storageFormatName(table().storageFormat()) +
                  "; text encoding: " + table().textEncodingSummary() + ". Showing all " +
                  std::to_string(table().count()) + " entries.");
    }

    void saveTo(const std::string& file) {
        ensureLoaded();
        table().save(file);
        updateLoadedState();
        rememberRecentFile(std::filesystem::path(file));
        neogames::resolver().inferFromOpenedPath(std::filesystem::path(file));
        setStatus("Saved file " + file + ". " + neotlk::storageFormatName(table().storageFormat()) +
                  "; text encoding: " + table().textEncodingSummary() + ". " +
                  std::to_string(table().count()) + " entries written.");
    }

    void onShowInterval(wxCommandEvent&) {
        try { refreshCurrentInterval(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void chooseAndOpenTlk(const std::filesystem::path& initialDirectory = {}) {
        try {
            const auto file = wxui::chooseOpenFile(this, "Open TLK", kTlkWildcard, initialDirectory);
            if (!file) {
                setStatus("No valid file opened!");
                return;
            }
            loadFile(file->string());
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onOpen(wxCommandEvent&) {
        chooseAndOpenTlk();
    }


    void setFilterTerm(std::string term) {
        viewState().filterTerm = std::move(term);
        if (filterText_ && wxui::toStd(filterText_->GetValue()) != viewState().filterTerm) {
            filterText_->ChangeValue(wxui::toWx(viewState().filterTerm));
        }
        if (table().fileExists()) refreshCurrentInterval();
    }

    void onFilterText(wxCommandEvent&) {
        viewState().filterTerm = filterText_ ? wxui::toStd(filterText_->GetValue()) : std::string();
        if (table().fileExists()) {
            try { refreshCurrentInterval(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
        }
    }

    void onFilterPrompt(wxCommandEvent&) {
        try {
            ensureLoaded();
            const auto term = wxui::promptText(this, "Filter/Search", "Search term:", viewState().filterTerm);
            if (term) setFilterTerm(*term);
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void clearAllFiltersAndRefresh() {
        neoview::clearAllFilters(viewState());
        if (filterText_) filterText_->ChangeValue("");
        if (table().fileExists()) refreshCurrentInterval();
    }

    int selectedVisualColumn() const {
        return contextVisualColumn_ >= 0 ? contextVisualColumn_ : 0;
    }

    void promptColumnFilterForVisualColumn(int visualColumn) {
        ensureLoaded();
        neoview::ensureIdentityColumns(viewState(), 3);
        const std::size_t logicalColumn = neoview::logicalColumnForVisual(viewState(), static_cast<std::size_t>(std::max(0, visualColumn)));
        const auto* existing = neoview::findColumnFilter(viewState(), logicalColumn);
        const std::string prior = existing != nullptr ? existing->term : std::string();
        const auto term = wxui::promptText(this, "Column Filter", "Show rows where " + tlkDisplayColumnLabel(logicalColumn) + " contains:", prior);
        if (!term) return;
        if (neoview::trimmedCopy(*term).empty()) {
            neoview::clearColumnFilter(viewState(), logicalColumn);
        } else {
            neoview::setColumnFilter(viewState(), neoview::ColumnFilter{logicalColumn, tlkDisplayColumnLabel(logicalColumn), *term, neoview::TextFilterMode::Contains, true});
        }
        refreshCurrentInterval();
    }

    void onClearFilter(wxCommandEvent&) {
        try { clearAllFiltersAndRefresh(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onFilterSelectedColumn(wxCommandEvent&) {
        try { promptColumnFilterForVisualColumn(selectedVisualColumn()); } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onClearSelectedColumnFilter(wxCommandEvent&) {
        try {
            neoview::ensureIdentityColumns(viewState(), 3);
            const std::size_t logicalColumn = neoview::logicalColumnForVisual(viewState(), static_cast<std::size_t>(std::max(0, selectedVisualColumn())));
            neoview::clearColumnFilter(viewState(), logicalColumn);
            refreshCurrentInterval();
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onClearAllFilters(wxCommandEvent&) {
        try { clearAllFiltersAndRefresh(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onResetColumnOrder(wxCommandEvent&) {
        neoview::setIdentityColumns(viewState(), 3);
        if (table().fileExists()) {
            try { refreshCurrentInterval(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
        } else {
            updateListColumnLabels();
        }
    }

    void onResetRowOrder(wxCommandEvent&) {
        if (table().fileExists()) {
            try { refreshCurrentInterval(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
        }
    }

    void onListColumnRightClick(wxListEvent& event) {
        contextVisualColumn_ = event.GetColumn();
        wxMenu menu;
        menu.Append(ID_FilterColumn, "Filter This Column...");
        menu.Append(ID_ClearColumnFilter, "Clear Filter on This Column");
        menu.AppendSeparator();
        menu.Append(ID_ClearAllFilters, "Clear All Filters");
        PopupMenu(&menu);
    }

    void onImport(neotabular::Format format) {
        try {
            const auto file = wxui::chooseOpenFile(this, "Import " + neotabular::formatName(format), tableWildcardForFormat(format));
            if (!file) return;
            const bool hadNativeDocument = table().fileExists();

            if (format == neotabular::Format::Xml) {
                const std::string xmlText = neotlk::readTextFile(*file);
                const auto declared = neotlk::storageFormatFromTlkXml(xmlText);
                if (!hadNativeDocument && declared && *declared == neotlk::TlkStorageFormat::DragonAgeV02) {
                    throw std::runtime_error(
                        "Dragon Age TLK XML must be imported into an already-open native Dragon Age TLK so its GFF backing structure is preserved.");
                }
                if (!hadNativeDocument) table().newFile();
                neotlk::applyXmlToTalkTable(table(), xmlText, !hadNativeDocument);
            } else {
                const std::string jsonText = format == neotabular::Format::Json
                    ? neotlk::readTextFile(*file)
                    : std::string();
                const auto imported = format == neotabular::Format::Json
                    ? neotlk::tlkTableFromJson(jsonText)
                    : neotabular::readTable(*file, format);
                const auto metadata = neotlk::inspectTlkTabularMetadata(imported);
                const auto jsonFormat = format == neotabular::Format::Json
                    ? neotlk::tlkStorageFormatFromJson(jsonText)
                    : std::optional<neotlk::TlkStorageFormat>{};
                const auto declared = jsonFormat ? jsonFormat : metadata.storageFormat;
                if (!hadNativeDocument && declared && *declared == neotlk::TlkStorageFormat::DragonAgeV02) {
                    throw std::runtime_error(
                        "Dragon Age TLK interchange data must be imported into an already-open native Dragon Age TLK so its GFF backing structure is preserved.");
                }
                if (!hadNativeDocument) table().newFile();
                if (declared) neotlk::applyDeclaredStorageFormat(table(), *declared, !hadNativeDocument);
                if (format == neotabular::Format::Json && table().supportsLanguageId()) {
                    table().setLanguage(neotlk::tlkLanguageFromJson(jsonText, table().language()));
                }
                neotlk::applyTabularToTalkTable(
                    table(),
                    imported,
                    !hadNativeDocument,
                    format == neotabular::Format::Json
                        ? neotlk::TlkTabularApplyMode::Replace
                        : neotlk::TlkTabularApplyMode::Merge);
            }
            viewState().resetForNewDocument();
            if (filterText_) filterText_->ChangeValue("");
            updateLoadedState();
            showAllEntries();
            setStatus("Imported TLK table from " + file->string() + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onExport(neotabular::Format format) {
        try {
            ensureLoaded();
            const bool structured = format == neotabular::Format::Xml || format == neotabular::Format::Json;
            if (structured && (!viewState().filterTerm.empty() || neoview::hasActiveColumnFilters(viewState()))) {
                throw std::runtime_error(
                    "NeoTLK XML/JSON exports are complete semantic documents and cannot be filtered. Use CSV or TSV to export the visible filtered rows.");
            }
            const std::filesystem::path sourceName = table().hasSaveTarget() ? std::filesystem::path(table().saveTargetFilename()) : std::filesystem::path(table().filename());
            const auto file = wxui::chooseSaveFile(this, "Export " + neotabular::formatName(format), tableWildcardForFormat(format),
                                                  exportDefaultFilename(sourceName, format, "dialog"));
            if (!file) return;
            if (format == neotabular::Format::Xml) {
                neotlk::writeTextFile(*file, neotlk::talkTableToXml(table()));
            } else if (format == neotabular::Format::Json) {
                neotlk::writeTextFile(*file, neotlk::talkTableToJson(table()));
            } else {
                neotabular::writeTable(filteredTlkTable(), *file, format);
            }
            setStatus("Exported TLK table to " + file->string() + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onExportTlkPatcher(neotlk::TlkPatcherCompatibility compatibility) {
        try {
            ensureLoaded();
            if (table().isDragonAgeV02()) {
                throw std::runtime_error("Dragon Age GFF TLK V0.2 uses sparse IDs and cannot be exported through the KotOR append.tlk patching model.");
            }
            if (table().isVersion40()) {
                throw std::runtime_error("Jade Empire TLK V4.0 is not supported by TSLPatcher/HoloPatcher's KotOR dialog.tlk workflow.");
            }

            const auto originalFile = wxui::chooseOpenFile(
                this,
                "Select the clean original TLK used as the comparison baseline",
                kTlkWildcard);
            if (!originalFile) return;

            const auto outputDirectory = wxui::chooseDirectory(
                this,
                "Choose the tslpatchdata output folder");
            if (!outputDirectory) return;

            neotlk::TalkTable original(originalFile->string());
            neotlk::TlkPatcherOptions options;
            options.compatibility = compatibility;
            auto result = neotlk::diffTlkForPatcher(original, table(), options);
            neotsl::throwIfUnsupported(result.project);

            if (!result.hasPatchableChanges()) {
                wxui::showMessage(this, "No TLK Patcher Changes",
                                  "No appended or replaceable TLK entries differ from the selected baseline.");
                return;
            }

            std::vector<std::filesystem::path> generatedFiles{
                *outputDirectory / "changes.ini"
            };
            if (result.hasAppendTable()) generatedFiles.push_back(*outputDirectory / options.appendFilename);
            if (result.hasReplacementTable()) generatedFiles.push_back(*outputDirectory / options.replacementFilename);

            std::vector<std::string> existingNames;
            for (const auto& generatedFile : generatedFiles) {
                std::error_code ec;
                if (std::filesystem::exists(generatedFile, ec) && !ec) {
                    existingNames.push_back(generatedFile.filename().string());
                }
            }
            if (!existingNames.empty()) {
                std::ostringstream message;
                message << "The selected folder already contains generated package files:\n\n";
                for (const auto& name : existingNames) message << "  " << name << "\n";
                message << "\nOverwrite these files?";
                if (!wxui::confirm(this, "Overwrite TLK Patcher Package", message.str())) return;
            }

            neotlk::writeTlkPatcherPackage(result, *outputDirectory);

            std::ostringstream summary;
            summary << "Wrote changes.ini to:\n" << outputDirectory->string() << "\n\n";
            summary << "Appended entries: " << result.appendedEntries << "\n";
            summary << "Replaced existing entries: " << result.replacedEntries;
            if (compatibility == neotlk::TlkPatcherCompatibility::TslPatcher) {
                summary << "\n\nThe package uses stock TSLPatcher-compatible append.tlk semantics.";
            } else if (result.hasReplacementTable()) {
                summary << "\n\nExisting-entry edits use HoloPatcher's replace.tlk extension and will not work with the original TSLPatcher executable.";
            } else {
                summary << "\n\nOnly append.tlk was needed, so this package is also compatible with stock TSLPatcher.";
            }
            wxui::showMessage(this, "TLK Patcher Package Generated", summary.str());
            setStatus("Generated " + std::string(neotlk::tlkPatcherCompatibilityName(compatibility)) +
                      " TLK package in " + outputDirectory->string() + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void applyDarkMode() {
        if (darkModeItem_ != nullptr) {
            darkModeItem_->Check(darkMode_);
        }
        wxui::applyTheme(this, darkMode_);
        if (grid_ != nullptr) {
            wxui::applyListTheme(*grid_, darkMode_);
        }
        applyFontScale();
    }

    void applyFontScale() {
        neoview::applyFontScale(this, fontScale_);
        layoutColumns();
    }

    void changeFontScaleSteps(int steps) {
        const double next = neoview::steppedFontScale(fontScale_, steps);
        if (neoview::fontScalePercent(next) == neoview::fontScalePercent(fontScale_)) return;
        fontScale_ = next;
        settings_.setFontScale(fontScale_);
        applyFontScale();
    }

    void onToggleDarkMode(wxCommandEvent& event) {
        darkMode_ = event.IsChecked();
        wxui::writeDarkMode(kAppName, darkMode_);
        applyDarkMode();
    }

    void onIncreaseFontScale(wxCommandEvent&) {
        fontScaleWheelFilter_.reset();
        changeFontScaleSteps(1);
    }
    void onDecreaseFontScale(wxCommandEvent&) {
        fontScaleWheelFilter_.reset();
        changeFontScaleSteps(-1);
    }
    void onResetFontScale(wxCommandEvent&) {
        fontScaleWheelFilter_.reset();
        fontScale_ = neoview::kDefaultFontScale;
        settings_.setFontScale(fontScale_);
        applyFontScale();
    }



    void onNew(wxCommandEvent&) {
        try {
            createDocumentTab(true);
            table().newFile();
            viewState().resetForNewDocument();
            if (filterText_) filterText_->ChangeValue("");
            SetTitle("NeoTLK v1.0.0 (TLK file editor)");
            start_->SetValue("0");
            stop_->SetValue("0");
            updateLoadedState();
            resetGridMessage("No entries present. Please use the Tools menu to add new entries.");
            setStatus("Created new blank TLK file. " + neotlk::fourCharToString(table().fileId()) + " " + neotlk::fourCharToString(table().version()));
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onSave(wxCommandEvent&) {
        try {
            ensureLoaded();
            if (!table().hasSaveTarget()) {
                wxCommandEvent dummy;
                onSaveAs(dummy);
                return;
            }
            saveTo(table().saveTargetFilename());
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onSaveAs(wxCommandEvent&) {
        try {
            ensureLoaded();
            const std::string defaultFile = table().hasSaveTarget() ? table().saveTargetFilename() : table().filename();
            const auto file = wxui::chooseSaveFile(this, "Save TLK", kTlkWildcard, defaultFile);
            if (!file) return;
            saveTo(file->string());
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onSearch(wxCommandEvent&) {
        try {
            ensureLoaded();
            SearchDialog dialog(this, minStrRef(), maxStrRef(), table().count() != 0u,
                                table().supportsSoundMetadata());
            wxui::applyTheme(&dialog, darkMode_);
            if (dialog.ShowModal() != wxID_OK) return;
            const neotlk::SearchOptions options = dialog.options();
            const std::vector<neotlk::UInt32> results = neotlk::searchStrRefs(table(), options);
            neoview::removeColumnFiltersOutsideRange(viewState(), 3);
            neoview::ensureIdentityColumns(viewState(), 3);
            grid_->DeleteAllItems();
            updateListColumnLabels();
            displayStrRefs() = results;
            gauge_->SetRange(static_cast<int>(std::max<std::size_t>(std::size_t{1}, results.size())));
            int progress = 0;
            for (neotlk::UInt32 ref : results) {
                const auto& entry = entryAt(ref);
                wxui::appendRow(*grid_, {tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 0)),
                                        tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 1)),
                                        tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 2))});
                gauge_->SetValue(++progress);
            }
            neoview::setIdentityRows(viewState(), displayStrRefs().size());
            wxui::applyTheme(grid_, darkMode_);
            gauge_->SetValue(0);
            viewState().searchResultsActive = true;
            setStatus("Found " + std::to_string(results.size()) + " matching entries.");
            if (!results.empty()) {
                wxui::selectRow(*grid_, 0);
                updateDisplayFromSelection();
            } else {
                display_->Clear();
            }
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onNewEntry(wxCommandEvent&) {
        try {
            ensureLoaded();
            neotlk::UInt32 newRef = table().nextAvailableStrRef();
            if (table().hasSparseStrRefs()) {
                const auto value = wxui::promptText(this, "New Dragon Age string ID",
                    "String ID:", std::to_string(newRef));
                if (!value) return;
                newRef = parseUInt32(*value, "string ID");
                if (table().containsStrRef(newRef)) {
                    throw std::runtime_error("That Dragon Age string ID already exists in this TLK file.");
                }
            }
            EntryDialog dialog(this, "New entry", newRef, nullptr, isTlkV40(),
                               table().supportsSoundMetadata());
            wxui::applyTheme(&dialog, darkMode_);
            if (dialog.ShowModal() != wxID_OK) return;
            neotlk::TalkString entry = dialog.entry();
            if (!table().supportsSoundMetadata()) {
                entry.flags = neotlk::TEXT_PRESENT;
                entry.soundResref = {};
                entry.soundId = 0xffffffffu;
                entry.volumeVariance = 0u;
                entry.pitchVariance = 0u;
                entry.soundLength = 0.0f;
            }
            if (table().hasSparseStrRefs()) table().addEntryAtStrRef(newRef, std::move(entry));
            else table().addEntry(std::move(entry));
            updateLoadedState();
            showAllEntries();
            selectStrRef(newRef);
            setStatus(std::string("New string entry added to TLK with ") +
                      (table().hasSparseStrRefs() ? "string ID " : "StrRef #") +
                      std::to_string(newRef) + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void editSelectedEntry() {
        ensureLoaded();
        const neotlk::UInt32 ref = selectedStrRef();
        const auto original = entryAt(ref);
        EntryDialog dialog(this, "Modify entry", ref, &original, isTlkV40(),
                           table().supportsSoundMetadata());
        wxui::applyTheme(&dialog, darkMode_);
        if (dialog.ShowModal() != wxID_OK) return;
        neotlk::TalkString replacement = dialog.entry();
        replacement.strRef = ref;
        if (!table().supportsSoundMetadata()) {
            replacement.flags = neotlk::TEXT_PRESENT;
            replacement.soundResref = {};
            replacement.soundId = 0xffffffffu;
            replacement.volumeVariance = 0u;
            replacement.pitchVariance = 0u;
            replacement.soundLength = 0.0f;
        }
        replacement.offsetToString = original.offsetToString;
        replacement.stringSize = original.stringSize;
        table().replaceEntry(ref, replacement);
        refreshCurrentIntervalOrSearchRow(ref);
        setStatus(std::string("Modified string entry ") +
                  (table().hasSparseStrRefs() ? "ID " : "#") +
                  std::to_string(ref) + ".");
    }

    void onEditRow(wxCommandEvent&) {
        try { editSelectedEntry(); } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onAppendFile(wxCommandEvent&) {
        try {
            ensureLoaded();
            const auto file = wxui::chooseOpenFile(this, "Append TLK", kTlkWildcard);
            if (!file) return;
            neotlk::TalkTable appendData(file->string());
            const neotlk::UInt32 start = table().nextAvailableStrRef();
            const neotlk::UInt32 added = table().appendFrom(appendData);
            updateLoadedState();
            if (table().count() > 0) {
                showAllEntries();
            }
            const neotlk::UInt32 end = added == 0u ? start : table().maxStrRef();
            setStatus("Appended " + file->string() + ". Added " + std::to_string(added) + " entries from " +
                      std::to_string(start) + " to " + std::to_string(end) + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onSetLanguage(wxCommandEvent&) {
        try {
            ensureLoaded();
            if (!table().supportsLanguageId()) {
                throw std::runtime_error("This Dragon Age TLK format does not store a classic language ID.");
            }
            LanguageDialog dialog(this, table().language());
            wxui::applyTheme(&dialog, darkMode_);
            if (dialog.ShowModal() != wxID_OK) return;
            table().setLanguage(dialog.language());
            static const char* names[] = {"English", "French", "German", "Italian", "Spanish", "Polish"};
            const neotlk::UInt32 id = table().language();
            wxui::showMessage(this, "Language", std::string("TLK file Language ID is now set to ") + names[id] + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onDeleteSelected(wxCommandEvent&) {
        try {
            ensureLoaded();
            const neotlk::UInt32 ref = selectedStrRef();
            const bool reindexes = table().deleteReindexesStrRefs();
            ConfirmDeleteDialog dialog(this, ref, reindexes);
            wxui::applyTheme(&dialog, darkMode_);
            if (dialog.ShowModal() != wxID_OK) return;
            table().deleteEntry(ref);
            if (table().count() == 0) {
                start_->SetValue("0");
                stop_->SetValue("0");
                updateLoadedState();
                resetGridMessage("No entries present. Please use the Tools menu to add new entries.");
            } else {
                refreshCurrentInterval();
                if (const auto nearest = nearestExistingStrRef(ref)) selectStrRef(*nearest);
            }
            const std::string effect = reindexes
                ? " All following entries have had their StrRefs changed."
                : " Other sparse Dragon Age string IDs were left unchanged.";
            wxui::showMessage(this, "Delete entry", "Entry " + std::to_string(ref) + " has been deleted." + effect);
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onPadToStrRef(wxCommandEvent&) {
        try {
            ensureLoaded();
            if (table().hasSparseStrRefs()) {
                throw std::runtime_error("Pad to StrRef is not applicable to sparse Dragon Age TLK files. Add the desired string ID directly.");
            }
            const neotlk::UInt32 current = table().count();
            PadDialog dialog(this, current);
            wxui::applyTheme(&dialog, darkMode_);
            if (dialog.ShowModal() != wxID_OK) return;
            const neotlk::UInt32 target = dialog.target();
            const neotlk::UInt32 added = table().padToStrRef(target);
            viewState().resetForNewDocument();
            if (filterText_) filterText_->ChangeValue("");
            updateLoadedState();
            showAllEntries();
            setStatus("Added " + std::to_string(added) + " blank entries, padding StrRef " + std::to_string(current) + " to " + std::to_string(target - 1) + ".");
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void refreshCurrentIntervalOrSearchRow(neotlk::UInt32 ref) {
        if (viewState().searchResultsActive) {
            const auto it = std::find(displayStrRefs().begin(), displayStrRefs().end(), ref);
            if (it != displayStrRefs().end()) {
                const long row = static_cast<long>(std::distance(displayStrRefs().begin(), it));
                const auto& entry = entryAt(ref);
                grid_->SetItem(row, 0, wxui::toWx(tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 0))));
                grid_->SetItem(row, 1, wxui::toWx(tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 1))));
                grid_->SetItem(row, 2, wxui::toWx(tlkDisplayCell(entry, neoview::logicalColumnForVisual(viewState(), 2))));
                selectStrRef(ref);
                return;
            }
        }
        refreshCurrentInterval();
        selectStrRef(ref);
    }

    void selectStrRef(neotlk::UInt32 ref) {
        const auto it = std::find(displayStrRefs().begin(), displayStrRefs().end(), ref);
        if (it != displayStrRefs().end()) {
            wxui::selectRow(*grid_, static_cast<long>(std::distance(displayStrRefs().begin(), it)));
            updateDisplayFromSelection();
        }
    }

    wxString itemText(long row, int column) const {
        wxListItem item;
        item.SetId(row);
        item.SetColumn(column);
        item.SetMask(wxLIST_MASK_TEXT);
        if (!grid_->GetItem(item)) {
            return wxString();
        }
        return item.GetText();
    }

    void onCopy(wxCommandEvent&) {
        if (auto* text = dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus())) {
            text->Copy();
            return;
        }
        const long row = wxui::selectedRow(*grid_);
        if (row >= 0 && wxTheClipboard && wxTheClipboard->Open()) {
            wxString value = itemText(row, 0) + "\t" + itemText(row, 1) + "\t" + itemText(row, 2);
            wxTheClipboard->SetData(new wxTextDataObject(value));
            wxTheClipboard->Close();
        }
    }

    void onCut(wxCommandEvent&) {
        if (auto* text = dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus())) {
            if (text->IsEditable()) text->Cut(); else text->Copy();
        }
    }

    void onPaste(wxCommandEvent&) {
        if (auto* text = dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus())) {
            if (text->IsEditable()) text->Paste();
            return;
        }
        try {
            ensureLoaded();
            if (!wxTheClipboard || !wxTheClipboard->Open()) return;
            if (!wxTheClipboard->IsSupported(wxDF_TEXT)) { wxTheClipboard->Close(); return; }
            wxTextDataObject data;
            wxTheClipboard->GetData(data);
            wxTheClipboard->Close();
            const auto pasted = neotabular::parseDelimited(wxui::toStd(data.GetText()), '\t');
            if (pasted.rows.empty()) return;
            const neotlk::UInt32 ref = selectedStrRef();
            neotlk::TalkString entry = entryAt(ref);
            const auto& row = pasted.rows.front();
            if (row.size() == 1) {
                entry.text = row[0];
            } else {
                if (row.size() >= 2) entry.text = row[1];
                if (row.size() >= 3 && table().supportsSoundMetadata()) {
                    if (isTlkV40()) entry.soundId = row[2].empty() ? 0xffffffffu : parseUInt32(row[2], "SoundId");
                    else entry.soundResref = neotlk::ResRef::fromString(row[2]);
                }
            }
            table().replaceEntry(ref, entry);
            refreshCurrentIntervalOrSearchRow(ref);
        } catch (const std::exception& ex) { wxui::showError(this, ex); }
    }

    void onCloseTab(wxCommandEvent&) { closeDocumentTab(activeDocumentIndex_); }

    void onCloseOtherTabs(wxCommandEvent&) {
        if (!hasActiveDocument()) return;
        for (std::size_t i = documents_.size(); i-- > 0;) {
            if (i != activeDocumentIndex_ && !closeDocumentTab(i)) return;
        }
    }

    void onNextTab(wxCommandEvent&) {
        if (documentTabs_ == nullptr || documentTabs_->GetPageCount() < 2) return;
        tabSwitchInProgress_ = true;
        documentTabs_->AdvanceSelection(true);
        tabSwitchInProgress_ = false;
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::currentPage(documentTabs_));
        if (index != neotabs::npos) selectDocumentTab(index);
    }

    void onPreviousTab(wxCommandEvent&) {
        if (documentTabs_ == nullptr || documentTabs_->GetPageCount() < 2) return;
        tabSwitchInProgress_ = true;
        documentTabs_->AdvanceSelection(false);
        tabSwitchInProgress_ = false;
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::currentPage(documentTabs_));
        if (index != neotabs::npos) selectDocumentTab(index);
    }

    void onSelectAll(wxCommandEvent&) {
        if (auto* text = dynamic_cast<wxTextCtrl*>(wxWindow::FindFocus())) {
            text->SelectAll();
        }
    }

    void onClose(wxCloseEvent& event) {
        if (event.CanVeto() && !confirmCloseAllTabs()) {
            event.Veto();
            return;
        }
        settings_.saveWindowPlacement(*this);
        Destroy();
    }

    neosettings::AppSettings settings_{kAppName};
    wxMenu* recentFilesMenu_ = nullptr;
    wxMenuItem* darkModeItem_ = nullptr;
    wxPanel* topPanel_ = nullptr;
    wxListCtrl* grid_ = nullptr;
    wxTextCtrl* display_ = nullptr;
    wxTextCtrl* start_ = nullptr;
    wxTextCtrl* stop_ = nullptr;
    wxTextCtrl* filePath_ = nullptr;
    wxTextCtrl* filterText_ = nullptr;
    wxStaticText* maxLabel_ = nullptr;
    wxGauge* gauge_ = nullptr;
    wxAuiNotebook* documentTabs_ = nullptr;
    std::vector<DocumentTab> documents_;
    std::size_t activeDocumentIndex_ = neotabs::npos;
    bool tabSwitchInProgress_ = false;
    int contextVisualColumn_ = 0;
    neoview::FontScaleWheelFilter fontScaleWheelFilter_;
    double fontScale_ = neoview::kDefaultFontScale;
    bool darkMode_ = false;
};

class NeoTLKApp final : public wxApp {
public:
    bool OnInit() override {
#if wxCHECK_VERSION(3, 3, 0)
        SetAppearance(Appearance::System);
#endif
        const bool smokeTest =
            argc > 1 && wxString(argv[1]) == wxString::FromUTF8("--smoke-test");

        auto* frame = new NeoTLKFrame;
        frame->Show(!smokeTest);

        if (smokeTest) {
            CallAfter([frame]() {
                frame->Destroy();
                if (wxTheApp != nullptr) wxTheApp->ExitMainLoop();
            });
        }
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(NeoTLKApp);
