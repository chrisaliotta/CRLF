#pragma once

#include "Types.h"

// Persists the workspace session (open tabs including unsaved buffer
// content) as a JSON file in the application data directory, so edits
// survive an application exit or crash without being saved to their files.
class SessionStore
{
public:
    SessionStore();

    void save(const Session &session) const;
    Session load() const;
    void clear() const;

private:
    QString m_filePath;
};
