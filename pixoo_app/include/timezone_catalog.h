#pragma once

#include <cstddef>

namespace pixoo {

// Canonical, closed set of timezone options. Each entry pairs a user-facing
// label with a POSIX TZ string (offset plus DST rule); the chip's C library
// accepts only POSIX TZ, not IANA names. This table is the single owner of the
// labels, their POSIX mappings, and the first-boot default: the select entity
// derives its options from it and applies the mapped POSIX string, so no option
// list or mapping is duplicated in YAML.
size_t TimezoneCount();

// Label/POSIX at index, or nullptr when out of range.
const char *TimezoneLabel(size_t index);
const char *TimezonePosix(size_t index);

// Index of the first-boot default option.
size_t DefaultTimezoneIndex();

// Resolves a label to its index. Returns false and leaves *index unchanged when
// the label is not in the table.
bool TimezoneIndexForLabel(const char *label, size_t *index);

}  // namespace pixoo
