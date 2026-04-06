// src/serialization/NoteDeserializer.cpp
#include "NoteDeserializer.h"
#include "../document/Document.h"
std::shared_ptr<Document> NoteDeserializer::deserialize(const std::filesystem::path&) {
    return std::make_shared<Document>();
}
