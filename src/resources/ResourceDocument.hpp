#pragma once
#include "neotlk/TlkFile.hpp"
#include <neoshared/ResourceDocument.hpp>
#include <algorithm>
#include <cctype>
namespace neotlk {
struct ResourceDocument {
    neoshared::ResourceDocument source;
    TalkTable model;
    static ResourceDocument load(neoshared::ResourceDocument input) {
        if (input.identity.empty() || input.type != 2018) throw std::runtime_error("This is not a valid NeoTLK resource handoff.");
        ResourceDocument result;
        result.model.loadBytes(input.bytes, input.fileName);
        result.source = std::move(input);
        result.source.bytes.clear(); // the parsed model owns any preservation bytes it needs
        return result;
    }
    void checkSaveDestination(const std::filesystem::path& path) const {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {return static_cast<char>(std::tolower(c));});
        if (extension != ".tlk") throw std::runtime_error("Choose a separate .tlk working file.");
        neoshared::checkResourceOutput(path, source.protectedInputs);
    }
};
}
