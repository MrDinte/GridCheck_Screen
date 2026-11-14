#include "ota_verify.h"

#include <cstdlib>
#include <cstring>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "signing_pub.h"

bool signatureOk(const uint8_t hash[32], const uint8_t sig[SIG_LEN]) {
  mbedtls_ecp_group grp;
  mbedtls_ecp_point q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  const bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_ecp_point_read_binary(&grp, &q, SIGNING_PUB, sizeof SIGNING_PUB) == 0 &&
                  mbedtls_mpi_read_binary(&r, sig, 32) == 0 && mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
                  mbedtls_ecdsa_verify(&grp, hash, 32, &q, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

static bool checkTrailer(const uint8_t* trailer, const uint8_t hash[32], std::string& err) {
  if (memcmp(trailer + SIG_LEN, SIG_MAGIC, 8) != 0) {
    err = "this firmware isn't signed by GridCheck, so it won't be installed";
    return false;
  }
  if (!signatureOk(hash, trailer)) {
    err = "this firmware's signature doesn't match, so it won't be installed";
    return false;
  }
  return true;
}

bool verifyBuffer(const uint8_t* data, size_t len, std::string& err) {
  if (len <= SIG_TRAILER) {
    err = "file too small";
    return false;
  }
  uint8_t hash[32];
  mbedtls_sha256(data, len - SIG_TRAILER, hash, 0);
  return checkTrailer(data + len - SIG_TRAILER, hash, err);
}

#ifdef ESP_PLATFORM
bool verifySigned(const esp_partition_t* part, size_t len, std::string& err) {
  if (len <= SIG_TRAILER) {
    err = "file too small";
    return false;
  }
  const size_t body = len - SIG_TRAILER;
  uint8_t* buf = (uint8_t*)malloc(4096);
  if (!buf) {
    err = "out of memory";
    return false;
  }
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  bool ok = true;
  for (size_t off = 0; ok && off < body; off += 4096) {
    const size_t n = body - off < 4096 ? body - off : 4096;
    ok = esp_partition_read(part, off, buf, n) == ESP_OK;
    if (ok) mbedtls_sha256_update(&ctx, buf, n);
  }
  uint8_t hash[32];
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  uint8_t trailer[SIG_TRAILER];
  if (ok) ok = esp_partition_read(part, body, trailer, SIG_TRAILER) == ESP_OK;
  free(buf);
  if (!ok) {
    err = "couldn't read the update back from flash";
    return false;
  }
  return checkTrailer(trailer, hash, err);
}
#endif
