#pragma once
#include "NeoModulePanel.hpp"
#include "neotlk/TlkFile.hpp"
#include <neoshared/ResourceDocument.hpp>
namespace neotlk::ui {
inline constexpr unsigned kEditorApiVersion=1;
class TLKEditorPanel : public neomodules::Panel {
public:
    using Panel::Panel;
    virtual bool openFile(const std::filesystem::path& path)=0;
    virtual bool openResource(neoshared::ResourceDocument resource)=0;
    virtual bool saveActiveAs(const std::filesystem::path& path)=0;
    virtual bool activateResource(const std::string& identity)=0;
    virtual std::size_t documentCount() const=0;
    virtual neotlk::TalkTable* activeTable()=0;
    virtual void refreshActiveTable()=0;
};
TLKEditorPanel* createEditorPanel(wxWindow* parent, neomodules::Context context={});
}
