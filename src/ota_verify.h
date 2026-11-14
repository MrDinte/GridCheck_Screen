// Signature check for firmware updates.
//
// A signed update ("-app.bin") is the normal firmware image followed by a
// 72-byte trailer: a 64-byte ECDSA P-256 signature (r||s) of the SHA-256 of
// everything before it, then the 8 bytes "GCSIG001". tools/sign.py makes it.
// The screen only switches to an update whose signature matches the public key
// built into the firmware it is running (src/signing_pub.h), so nobody without
// the private key can get their own firmware onto it over Wi-Fi - not someone
// on the network, and not someone who got into the GitHub account.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

static constexpr size_t SIG_LEN = 64;
static constexpr size_t SIG_TRAILER = SIG_LEN + 8;
static const char SIG_MAGIC[9] = "GCSIG001";

/** Is `sig` (r||s) a valid signature of `hash` (SHA-256) by the built-in key? */
bool signatureOk(const uint8_t hash[32], const uint8_t sig[SIG_LEN]);

/** Check a whole signed file held in memory (used by the PC test). */
bool verifyBuffer(const uint8_t* data, size_t len, std::string& err);

#ifdef ESP_PLATFORM
#include "esp_partition.h"
/** Check a signed file of `len` bytes that was just written to `part`. */
bool verifySigned(const esp_partition_t* part, size_t len, std::string& err);
#endif
