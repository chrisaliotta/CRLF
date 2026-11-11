#pragma once

// Padding is a blank row the diff view inserts so both sides stay aligned.
enum class ChangeKind { Added, Modified, Removed, Padding };

// Rows `line` through `line + count - 1` (0-based) differ from a base text.
// A mark with count 0 sits on the boundary above `line`: rows were deleted
// there.
struct ChangeMark
{
    int line = 0;
    int count = 0;
    ChangeKind kind = ChangeKind::Added;
};
