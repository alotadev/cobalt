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

#include <limits.h>
#include <string>
#include <vector>

#include "third_party/boringssl/src/include/openssl/bn.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "util/crypto_util/random.h"
#include "util/crypto_util/cipher.h"
#include "util/crypto_util/errors.h"
#include "util/crypto_util/types.h"

namespace cobalt {
namespace crypto {

const char* kLine1 = "The woods are lovely, dark and deep,\n";
const char* kLine2 = "But I have promises to keep,\n";
const char* kLine3 = "And miles to go before I sleep,\n";
const char* kLine4 = "And miles to go before I sleep.";
const char* kLines[] = {kLine1, kLine2, kLine3, kLine4};
const int kNumLines = 4;

// Tests SymmetricCipher().

// This function is invoked by the SymmetricCipherTest.
// It encrypts and then decrypts |plain_text| and checks that the
// recovered text is equal to the plain text. It generates a random
// key and nonce.
void doSymmetricCipherTest(SymmetricCipher* cipher,
    const byte *plain_text, int ptext_len) {
  // Initialize
  byte key[SymmetricCipher::KEY_SIZE];
  byte nonce[SymmetricCipher::NONCE_SIZE];
  Random rand;
  rand.RandomBytes(key, SymmetricCipher::KEY_SIZE);
  rand.RandomBytes(nonce, SymmetricCipher::NONCE_SIZE);
  EXPECT_TRUE(cipher->setKey(key)) << GetLastErrorMessage();

  // Encrypt
  std::vector<byte> cipher_text;
  EXPECT_TRUE(cipher->encrypt(nonce, plain_text, ptext_len, &cipher_text))
      << GetLastErrorMessage();

  // Decrypt
  std::vector<byte> recovered_text;
  EXPECT_TRUE(cipher->decrypt(nonce, cipher_text.data(), cipher_text.size(),
      &recovered_text)) << GetLastErrorMessage();

  // Compare
  EXPECT_EQ(std::string((const char*)recovered_text.data(),
                        recovered_text.size()),
            std::string((const char*)plain_text));
}

TEST(SymmetricCipherTest, TestManyStrings) {
  SymmetricCipher cipher;

  // Test once with each line separately.
  for (int i = 0; i < kNumLines; i++) {
    doSymmetricCipherTest(&cipher, (const byte*) kLines[i],
                             strlen(kLines[i]));
  }

  // Test once with all lines together.
  std::string all_lines;
  for (int i = 0; i < kNumLines; i++) {
    all_lines += kLines[i];
  }
  doSymmetricCipherTest(&cipher, (const byte*) all_lines.data(),
                           all_lines.size());

  // Test once with a longer string: Repeat string 32 times.
  for (int i = 0; i < 5; i++) {
    all_lines += all_lines;
  }
  doSymmetricCipherTest(&cipher, (const byte*) all_lines.data(),
                           all_lines.size());
}

// This function is invoked by the HybridCipherTest
// It encrypts and then decrypts |plain_text| and checks that the
// recovered text is equal to the plain text.
void doHybridCipherTest(HybridCipher* hybrid_cipher,
    const byte *plain_text, int ptext_len,
    const byte public_key[HybridCipher::PUBLIC_KEY_SIZE],
    const byte private_key[HybridCipher::PRIVATE_KEY_SIZE]) {

  // Encrypt
  byte public_key_part[HybridCipher::PUBLIC_KEY_SIZE];
  byte salt[HybridCipher::SALT_SIZE];
  byte nonce[HybridCipher::NONCE_SIZE];
  std::vector<byte> cipher_text;
  ASSERT_TRUE(hybrid_cipher->SetPublicKey(public_key)) << GetLastErrorMessage();
  EXPECT_TRUE(hybrid_cipher->Encrypt(plain_text, ptext_len, public_key_part,
                                     salt, nonce, &cipher_text))
      << GetLastErrorMessage();

  // Decrypt
  std::vector<byte> recovered_text;
  ASSERT_TRUE(hybrid_cipher->SetPrivateKey(private_key))
      << GetLastErrorMessage();
  ASSERT_TRUE(hybrid_cipher->Decrypt(public_key_part, salt, nonce,
                                     cipher_text.data(), cipher_text.size(),
                                     &recovered_text))
      << GetLastErrorMessage();

  // Compare
  EXPECT_EQ(std::string((const char*)recovered_text.data(),
                        recovered_text.size()),
            std::string((const char*)plain_text));

  // Decrypt with flipped salt
  salt[0] ^= 0x1;  // flip a bit in the salt
  EXPECT_FALSE(hybrid_cipher->Decrypt(public_key_part, salt, nonce,
                                      cipher_text.data(), cipher_text.size(),
                                      &recovered_text))
      << GetLastErrorMessage();

  // Decrypt with modified public_key_part
  salt[0] ^= 0x1;  // flip salt bit back
  public_key_part[2] ^= 0x1;  // flip any bit except in first byte (due to
                              // X9.62 serialization)
  EXPECT_FALSE(hybrid_cipher->Decrypt(public_key_part, salt, nonce,
                                      cipher_text.data(), cipher_text.size(),
                                      &recovered_text))
      << GetLastErrorMessage();
}

void doGenerateKeys(byte public_key[HybridCipher::PUBLIC_KEY_SIZE],
                    byte private_key[HybridCipher::PRIVATE_KEY_SIZE]) {
  std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> eckey(
      EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free);
  ASSERT_TRUE(EC_KEY_generate_key(eckey.get())) << GetLastErrorMessage();

  // Set public_key
  std::unique_ptr<EC_POINT, decltype(&::EC_POINT_free)> ecpoint(
      EC_POINT_dup(EC_KEY_get0_public_key(eckey.get()),
                   EC_KEY_get0_group(eckey.get())), EC_POINT_free);
  ASSERT_TRUE(EC_POINT_point2oct(EC_KEY_get0_group(eckey.get()),
                                 ecpoint.get(),
                                 POINT_CONVERSION_COMPRESSED,
                                 public_key,
                                 HybridCipher::PUBLIC_KEY_SIZE,
                                 nullptr)) << GetLastErrorMessage();

  // Set private_key
  std::unique_ptr<BIGNUM, decltype(&::BN_free)> bn_private_key(
      BN_dup(EC_KEY_get0_private_key(eckey.get())), BN_free);
  const size_t PKEY_SIZE = HybridCipher::PRIVATE_KEY_SIZE;
  ASSERT_TRUE(BN_bn2bin(bn_private_key.get(), private_key) <= PKEY_SIZE);
}

TEST(HybridCipherTest, Test) {
  HybridCipher hybrid_cipher;
  byte public_key[HybridCipher::PUBLIC_KEY_SIZE];
  byte private_key[HybridCipher::PRIVATE_KEY_SIZE];

  // Test with five different key pairs
  // TODO(pseudorandom): Test is flaky. Succeeds with 500 key pairs, fails
  // with 5000. Investigate.
  for (int times = 0; times < 5; ++times) {
    doGenerateKeys(public_key, private_key);

    // Test once with each line separately.
    for (int i = 0; i < kNumLines; i++) {
      doHybridCipherTest(&hybrid_cipher, (const byte*) kLines[i],
                         strlen(kLines[i]), public_key, private_key);
    }

    // Test once with all lines together.
    std::string all_lines;
    for (int i = 0; i < kNumLines; i++) {
      all_lines += kLines[i];
    }
    doHybridCipherTest(&hybrid_cipher, (const byte*) all_lines.data(),
                       all_lines.size(), public_key, private_key);

    // Test once with a longer string: Repeat string 32 times.
    for (int i = 0; i < 5; i++) {
      all_lines += all_lines;
    }
    doHybridCipherTest(&hybrid_cipher, (const byte*) all_lines.data(),
                       all_lines.size(), public_key, private_key);
  }
}

}  // namespace crypto

}  // namespace cobalt

