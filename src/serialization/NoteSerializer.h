// src/serialization/NoteSerializer.h
#pragma once
#include <filesystem>
class Document;
class NoteSerializer {
public:
    bool serialize(const Document& doc, const std::filesystem::path& path);
};
