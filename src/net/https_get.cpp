#include "net/https_get.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "net/network_manager.h"

namespace {

constexpr uint32_t HTTP_TIMEOUT_MS = 8000;

} // namespace

bool httpsGet(const char *url, const char *tag, String &body) {
  if (!networkIsUp()) {
    return false;
  }

  WiFiClientSecure client;
  // Known gap: no certificate validation. Open-Meteo is HTTPS-only, and
  // pinning a root CA here means shipping a certificate that expires. For
  // public read-only data the exposure is low, but this is a real weakness and
  // belongs in the documented gaps rather than in a comment nobody reads.
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    return false;
  }

  int status = http.GET();
  if (status != 200) {
    Serial.printf("%s: HTTP %d\n", tag, status);
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  return true;
}
