#pragma once

#include <QStringList>

// Registers this executable for Explorer's "Open with" chooser and adds an
// "Open with <app>" item to the file context menu. Per-user, so no install
// or elevation is required; a later launch updates the path.
void registerOpenWith(const QStringList &extensions);
