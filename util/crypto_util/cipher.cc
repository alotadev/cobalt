// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "util/crypto_util/cipher.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/bio.h"
#include "third_party/boringssl/src/include/openssl/bn.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/ecdh.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"
#include "third_party/boringssl/src/include/openssl/pem.h"
#include "util/crypto_util/errors.h"
#include "util/crypto_util/random.h"

namespace cobalt {

namespace crypto {

namespace {
// Note(pseudorandom) Curve constants are defined in
// third_party/boringssl/src/include/openssl/nid.h. NID_X9_62_prime256v1
// refers to the NIST p256 curve (secp256r1 or P-256 defined in FIPS 186-4
// here http://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.186-4.pdf)
#define EC_CURVE_CONSTANT NID_X9_62_prime256v1

const EVP_AEAD* GetAEAD() {
  // Note(rudominer) The constants KEY_SIZE and NONCE_SIZE are set based
  // on the algorithm chosen. If this algorithm changes you must also
  // change those constants accordingly.
  //
  // NOTE(pseudorandom) By using a 256-bit curve in EC_CURVE_CONSTANT for
  // public-key cryptography when SymmetricCipher is used in HybridCipher,
  // the effective security level is AES-128 and not AES-256.
  return EVP_aead_aes_256_gcm();
}
static const size_t GROUP_ELEMENT_SIZE = 256 / 8;  // (g^xy) object length

// For hybrid mode, we can fix the nonce to all zeroes without losing
// security. See: https://goto.google.com/aes-gcm-zero-nonce-security
const byte kAllZeroNonce[SymmetricCipher::NONCE_SIZE] = {0x00};

// Generates a cryptographically secure public/private key pair (g^x, x),
// appropriate for use by HybridCipher. Returns the pair in two forms:
// First the pair is represented by the returned EC_KEY. Second, if public_key
// is not NULL then the public key will be serialized into that buffer and
// similarly for private_key. Returns NULL to indicate that any of the steps
// failed. In that case the contents of |public_key| and |private_key| are
// unspecified and they should not be relied upon.
std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> GenerateHybridCipherKeyPair(
    byte public_key[HybridCipher::PUBLIC_KEY_SIZE],
    byte private_key[HybridCipher::PRIVATE_KEY_SIZE]) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!EC_KEY_generate_key(eckey.get())) {
    return eckey;
  }

  // Serialize public_key if requested.
  if (public_key) {
    if (EC_POINT_point2oct(EC_KEY_get0_group(eckey.get()),
                           EC_KEY_get0_public_key(eckey.get()),
                           POINT_CONVERSION_COMPRESSED, public_key,
                           HybridCipher::PUBLIC_KEY_SIZE,
                           nullptr) != HybridCipher::PUBLIC_KEY_SIZE) {
      eckey.reset();
      return eckey;
    }
  }

  // Serialize private_key if requested.
  if (private_key) {
    if (!BN_bn2bin_padded(private_key, HybridCipher::PRIVATE_KEY_SIZE,
                          EC_KEY_get0_private_key(eckey.get()))) {
      eckey.reset();
      return eckey;
    }
  }
  return eckey;
}

// Builds an ECKEY containing only a public-key using the given byte
// buffer. Return NULL to indicate failure.
std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> BuildECKeyPublic(
    const byte public_key[HybridCipher::PUBLIC_KEY_SIZE]) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!eckey) {
    return eckey;
  }

  std::unique_ptr<EC_POINT, decltype(&::EC_POINT_free)> ecpoint(
      EC_POINT_new(EC_KEY_get0_group(eckey.get())), EC_POINT_free);
  if (!ecpoint) {
    eckey.reset();
    return eckey;
  }

  // Read bytes from public_key into ecpoint
  if (!EC_POINT_oct2point(EC_KEY_get0_group(eckey.get()), ecpoint.get(),
                          public_key, HybridCipher::PUBLIC_KEY_SIZE, nullptr)) {
    eckey.reset();
    return eckey;
  }

  // Setup eckey with public key from ecpoint
  if (!EC_KEY_set_public_key(eckey.get(), ecpoint.get())) {
    eckey.reset();
    return eckey;
  }

  return eckey;
}

}  //  namespace

class CipherContext {
 public:
  ~CipherContext() { EVP_AEAD_CTX_cleanup(&impl_); }

  bool SetKey(const byte key[SymmetricCipher::KEY_SIZE]) {
    EVP_AEAD_CTX_cleanup(&impl_);
    return EVP_AEAD_CTX_init(&impl_, GetAEAD(), key, SymmetricCipher::KEY_SIZE,
                             EVP_AEAD_DEFAULT_TAG_LENGTH, NULL);
  }

  EVP_AEAD_CTX* get() { return &impl_; }

 private:
  EVP_AEAD_CTX impl_;
};

class HybridCipherContext {
 public:
  HybridCipherContext() : key_(nullptr, ::EVP_PKEY_free) {}

  bool ResetKey() {
    key_.reset(EVP_PKEY_new());
    return (nullptr != key_);
  }

  EVP_PKEY* GetKey() { return key_.get(); }

 private:
  std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> key_;
};

// SymmetricCipher methods.

SymmetricCipher::SymmetricCipher() : context_(new CipherContext()) {}

SymmetricCipher::~SymmetricCipher() {}

bool SymmetricCipher::set_key(const byte key[KEY_SIZE]) {
  return context_->SetKey(key);
}

bool SymmetricCipher::Encrypt(const byte nonce[NONCE_SIZE], const byte* ptext,
                              int ptext_len, std::vector<byte>* ctext) {
  int max_out_len = EVP_AEAD_max_overhead(GetAEAD()) + ptext_len;
  ctext->resize(max_out_len);
  size_t out_len;
  int rc =
      EVP_AEAD_CTX_seal(context_->get(), ctext->data(), &out_len, max_out_len,
                        nonce, NONCE_SIZE, ptext, ptext_len, NULL, 0);
  ctext->resize(out_len);
  return rc;
}

bool SymmetricCipher::Decrypt(const byte nonce[NONCE_SIZE], const byte* ctext,
                              int ctext_len, std::vector<byte>* ptext) {
  ptext->resize(ctext_len);
  size_t out_len;
  int rc =
      EVP_AEAD_CTX_open(context_->get(), ptext->data(), &out_len, ptext->size(),
                        nonce, NONCE_SIZE, ctext, ctext_len, NULL, 0);
  ptext->resize(out_len);
  return rc;
}

// HybridCipher methods.

// static
bool HybridCipher::GenerateKeyPair(
    byte public_key[HybridCipher::PUBLIC_KEY_SIZE],
    byte private_key[HybridCipher::PRIVATE_KEY_SIZE]) {
  auto key = GenerateHybridCipherKeyPair(public_key, private_key);
  return (key != nullptr);
}

// static
bool HybridCipher::GenerateKeyPairPEM(std::string* public_key_pem_out,
                                      std::string* private_key_pem_out) {
  auto eckey = GenerateHybridCipherKeyPair(nullptr, nullptr);
  if (!eckey) {
    return false;
  }
  std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> evpkey(EVP_PKEY_new(),
                                                               ::EVP_PKEY_free);
  if (!evpkey) {
    return false;
  }
  if (!EVP_PKEY_set1_EC_KEY(evpkey.get(), eckey.get())) {
    return false;
  }
  std::unique_ptr<BIO, decltype(&::BIO_free)> mem_bio(BIO_new(BIO_s_mem()),
                                                      ::BIO_free);

  // Write the public key as a pem
  if (!PEM_write_bio_PUBKEY(mem_bio.get(), evpkey.get())) {
    return false;
  }
  const unsigned char* contents;
  size_t content_length;
  if (!BIO_mem_contents(mem_bio.get(), &contents, &content_length)) {
    return false;
  }
  public_key_pem_out->resize(content_length);
  std::memcpy(&(*public_key_pem_out)[0], contents, content_length);

  // Write the private key as a pem
  mem_bio.reset(BIO_new(BIO_s_mem()));
  if (!PEM_write_bio_PrivateKey(mem_bio.get(), evpkey.get(), nullptr, nullptr,
                                0, nullptr, nullptr)) {
    return false;
  }
  if (!BIO_mem_contents(mem_bio.get(), &contents, &content_length)) {
    return false;
  }
  private_key_pem_out->resize(content_length);
  std::memcpy(&(*private_key_pem_out)[0], contents, content_length);

  return true;
}

HybridCipher::HybridCipher()
    : context_(new HybridCipherContext()), symm_cipher_(new SymmetricCipher) {}

HybridCipher::~HybridCipher() {}

bool HybridCipher::set_public_key(const byte public_key[PUBLIC_KEY_SIZE]) {
  auto eckey = BuildECKeyPublic(public_key);
  if (!eckey) {
    return false;
  }

  // Setup pkey with EC public key eckey
  context_->ResetKey();
  if (!EVP_PKEY_set1_EC_KEY(context_->GetKey(), eckey.get())) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::set_public_key_pem(const std::string& key_pem) {
  // Construct a memory BIO to wrap the string.
  std::unique_ptr<BIO, decltype(&::BIO_free)> mem_bio(
      BIO_new_mem_buf(key_pem.data(), key_pem.size()), ::BIO_free);
  if (!mem_bio.get()) {
    return false;
  }

  // Read a public key from the PEM in the memory BIO, construct an EVP_PKEY.
  std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> evpkey(
      PEM_read_bio_PUBKEY(mem_bio.get(), nullptr, nullptr, nullptr),
      ::EVP_PKEY_free);
  if (!evpkey.get()) {
    return false;
  }

  context_->ResetKey();

  if (!EVP_PKEY_set1_EC_KEY(context_->GetKey(),
                            EVP_PKEY_get0_EC_KEY(evpkey.get()))) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::set_private_key(const byte private_key[PRIVATE_KEY_SIZE]) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(EC_CURVE_CONSTANT), EC_KEY_free);
  if (!eckey) {
    return false;
  }

  std::unique_ptr<BIGNUM, decltype(&::BN_free)> bn_private_key(
      BN_bin2bn(private_key, PRIVATE_KEY_SIZE, nullptr), BN_free);
  if (!bn_private_key) {
    return false;
  }

  // Read bytes from private_key into BIGNUM object
  if (!EC_KEY_set_private_key(eckey.get(), bn_private_key.get())) {
    return false;
  }

  // Setup pkey with EC private key eckey
  context_->ResetKey();
  if (!EVP_PKEY_set1_EC_KEY(context_->GetKey(), eckey.get())) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::set_private_key_pem(const std::string& key_pem) {
  // Construct a memory BIO to wrap the string.
  std::unique_ptr<BIO, decltype(&::BIO_free)> mem_bio(
      BIO_new_mem_buf(key_pem.data(), key_pem.size()), ::BIO_free);
  if (!mem_bio.get()) {
    return false;
  }

  // Read a private key from the PEM in the memory BIO, construct an EVP_PKEY.
  std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> evpkey(
      PEM_read_bio_PrivateKey(mem_bio.get(), nullptr, nullptr, nullptr),
      ::EVP_PKEY_free);
  if (!evpkey.get()) {
    return false;
  }

  context_->ResetKey();
  if (!EVP_PKEY_set1_EC_KEY(context_->GetKey(),
                            EVP_PKEY_get0_EC_KEY(evpkey.get()))) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::Encrypt(const byte* ptext, int ptext_len,
                           std::vector<byte>* hybrid_ctext) {
  byte public_key_part[PUBLIC_KEY_SIZE];
  byte salt[SALT_SIZE];
  std::vector<byte> symmetric_ctext;
  if (!EncryptInternal(ptext, ptext_len, public_key_part, salt,
                       &symmetric_ctext)) {
    return false;
  }
  hybrid_ctext->resize(symmetric_ctext.size() + PUBLIC_KEY_SIZE + SALT_SIZE);
  std::memcpy(hybrid_ctext->data(), public_key_part, PUBLIC_KEY_SIZE);
  std::memcpy(hybrid_ctext->data() + PUBLIC_KEY_SIZE, salt, SALT_SIZE);
  std::memcpy(hybrid_ctext->data() + PUBLIC_KEY_SIZE + SALT_SIZE,
              symmetric_ctext.data(), symmetric_ctext.size());
  return true;
}

bool HybridCipher::EncryptInternal(const byte* ptext, int ptext_len,
                                   byte public_key_part_out[PUBLIC_KEY_SIZE],
                                   byte salt_out[SALT_SIZE],
                                   std::vector<byte>* symmetric_ctext_out) {
  // Generate fresh EC key (g^y, y). The public key part g^y is also serialized
  // into |public_key_part_out|.
  auto eckey = GenerateHybridCipherKeyPair(public_key_part_out, nullptr);
  if (!eckey) {
    return false;
  }

  byte shared_key[GROUP_ELEMENT_SIZE];  // To store g^(xy) after ECDH
  const EC_POINT* ec_pub_point =
      EC_KEY_get0_public_key(EVP_PKEY_get0_EC_KEY(context_->GetKey()));
  size_t shared_key_len = ECDH_compute_key(shared_key, sizeof(shared_key),
                                           ec_pub_point, eckey.get(), nullptr);
  if (shared_key_len != sizeof(shared_key)) {
    return false;
  }

  // Fill salt with random bytes
  Random rand;
  rand.RandomBytes(salt_out, SALT_SIZE);

  // Derive hkdf_derived_key by running HKDF with SHA512 and random salt
  byte hkdf_derived_key[SymmetricCipher::KEY_SIZE];
  std::vector<byte> hkdf_input(PUBLIC_KEY_SIZE + GROUP_ELEMENT_SIZE);
  std::memcpy(hkdf_input.data(), public_key_part_out, PUBLIC_KEY_SIZE);
  std::memcpy(hkdf_input.data() + PUBLIC_KEY_SIZE, shared_key,
              GROUP_ELEMENT_SIZE);
  if (!HKDF(hkdf_derived_key, SymmetricCipher::KEY_SIZE, EVP_sha512(),
            hkdf_input.data(), hkdf_input.size(), salt_out, SALT_SIZE, nullptr,
            0)) {
    return false;
  }

  // Do symmetric encryption with hkdf_derived_key
  if (!symm_cipher_->set_key(hkdf_derived_key)) {
    return false;
  }
  // For hybrid mode, we can fix the nonce to all zeroes without losing
  // security. See: https://goto.google.com/aes-gcm-zero-nonce-security
  if (!symm_cipher_->Encrypt(kAllZeroNonce, ptext, ptext_len,
                             symmetric_ctext_out)) {
    return false;
  }

  // Success
  return true;
}

bool HybridCipher::Decrypt(const byte* hybrid_ctext, int ctext_len,
                           std::vector<byte>* ptext) {
  if (!hybrid_ctext || ctext_len < PUBLIC_KEY_SIZE + SALT_SIZE + 1) {
    return false;
  }
  return DecryptInternal(hybrid_ctext, hybrid_ctext + PUBLIC_KEY_SIZE,
                         hybrid_ctext + PUBLIC_KEY_SIZE + SALT_SIZE,
                         ctext_len - (PUBLIC_KEY_SIZE + SALT_SIZE), ptext);
}

bool HybridCipher::DecryptInternal(const byte public_key_part[PUBLIC_KEY_SIZE],
                                   const byte salt[SALT_SIZE],
                                   const byte* symmetric_ctext,
                                   int symmetric_ctext_len,
                                   std::vector<byte>* ptext) {
  auto eckey = BuildECKeyPublic(public_key_part);
  if (!eckey) {
    return false;
  }

  byte shared_key[GROUP_ELEMENT_SIZE];  // To store g^(xy) after ECDH
  size_t shared_key_len = ECDH_compute_key(
      shared_key, sizeof(shared_key), EC_KEY_get0_public_key(eckey.get()),
      EVP_PKEY_get0_EC_KEY(context_->GetKey()), nullptr);
  if (shared_key_len != sizeof(shared_key)) {
    return false;
  }

  // Derive hkdf_derived_key by running HKDF with SHA512 and given salt
  byte hkdf_derived_key[SymmetricCipher::KEY_SIZE];
  std::vector<byte> hkdf_input(PUBLIC_KEY_SIZE + GROUP_ELEMENT_SIZE);
  std::memcpy(hkdf_input.data(), public_key_part, PUBLIC_KEY_SIZE);
  std::memcpy(hkdf_input.data() + PUBLIC_KEY_SIZE, shared_key,
              GROUP_ELEMENT_SIZE);
  if (!HKDF(hkdf_derived_key, SymmetricCipher::KEY_SIZE, EVP_sha512(),
            hkdf_input.data(), hkdf_input.size(), salt, SALT_SIZE, nullptr,
            0)) {
    return false;
  }

  // Decrypt using symm_cipher_ interface
  if (!symm_cipher_->set_key(hkdf_derived_key)) {
    return false;
  }

  // Our encryption always uses the all-zero nonce.
  if (!symm_cipher_->Decrypt(kAllZeroNonce, symmetric_ctext,
                             symmetric_ctext_len, ptext)) {
    return false;
  }

  // Success
  return true;
}

}  // namespace crypto

}  // namespace cobalt
