#pragma once

struct lua_State;

namespace saintnova {

// Installs the crypt library: base64 encode/decode, SHA-384 hashing, CSPRNG
// bytes, key derivation, and AES-256-CBC encrypt/decrypt.
//
// Format notes, because "encrypt" without a format is not an interface:
//
//   crypt.hash(data)          lowercase hex SHA-384 (96 characters)
//   crypt.encrypt(data, key)  AES-256-CBC, key = SHA-384(key)[0..32),
//                             output = 16-byte random IV || ciphertext, PKCS#7
//   crypt.decrypt(data, key)  inverse of the above; errors on malformed input
//   crypt.derive(value, len)  deterministic SHA-384 counter-mode expansion
//   crypt.random(n)           n characters from [A-Za-z0-9]
//
// The ciphertext layout is SaintNova's own. It is real AES and it round-trips, but
// it is not byte-compatible with any other executor's crypt library, because
// those do not document their key derivation or IV handling.
void install_crypt_api(lua_State* L);

}  // namespace saintnova
