#pragma once

#include <IPAddress.h>

// Joins the network named in secrets.h. Returns false rather than blocking
// forever, so the caller decides what a failed join means. On failure it logs
// every network the radio could see, which distinguishes "SSID not found" from
// "password rejected" - they look identical otherwise.
//
// The ESP32-S3 radio is 2.4 GHz only, so a 5 GHz network is simply absent from
// that scan. That absence is usually the answer.
bool networkConnect();

bool networkIsUp();
IPAddress networkIp();
int networkRssi();
