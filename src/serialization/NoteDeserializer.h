// src/serialization/NoteDeserializer.h
#pragma once
#include <filesystem>
#include <memory>
class Document;
class NoteDeserializer {
public:
    std::shared_ptr<Document> deserialize(const std::filesystem::path& path);
};
