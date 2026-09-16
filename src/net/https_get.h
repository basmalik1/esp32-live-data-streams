#pragma once

#include <WString.h>

// One GET over TLS, body returned as a String. Every source uses this rather
// than owning a client of its own, so the TLS configuration - including the
// known gap below - lives in exactly one place.
//
// Returns false if the network is down, the connection fails, or the status is
// anything but 200. `tag` prefixes the log line so the failure names its
// source.
bool httpsGet(const char *url, const char *tag, String &body);
