// src/serialization/NoteSerializer.cpp
#include "NoteSerializer.h"
#include "../document/Document.h"
bool NoteSerializer::serialize(const Document&, const std::filesystem::path&) { return true; }
