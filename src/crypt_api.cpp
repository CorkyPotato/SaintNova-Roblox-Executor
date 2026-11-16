#include "saintnova/crypt_api.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lua.h"
#include "lualib.h"

namespace saintnova {
namespace {

constexpr std::size_t kSha384Bytes = 48;
constexpr std::size_t kMaxCryptBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxRandomChars = 1024;
constexpr std::size_t kMaxDeriveBytes = 1024;

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kRandomAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

[[noreturn]] void fail(lua_State* L, const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    luaL_error(L, "%s", buffer);
    std::abort();
}

bool sha384(const void* data, std::size_t length, unsigned char* out) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA384_ALGORITHM, nullptr, 0) < 0) return false;

    bool ok = false;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        ok = BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(length), 0) >= 0 &&
             BCryptFinishHash(hash, out, static_cast<ULONG>(kSha384Bytes), 0) >= 0;
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool random_bytes(unsigned char* out, std::size_t length) {
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

std::string to_hex(const unsigned char* data, std::size_t length) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

std::string sha384_hex(const std::string& data) {
    unsigned char digest[kSha384Bytes];
    if (!sha384(data.data(), data.size(), digest)) return std::string();
    return to_hex(digest, sizeof(digest));
}

bool aes256_cbc(bool encrypt, const std::string& key_material, const std::string& input, std::string& output,
                std::string& error) {
    if (input.size() > kMaxCryptBytes) {
        error = "data exceeds the 8 MiB crypt limit";
        return false;
    }

    unsigned char digest[kSha384Bytes];
    if (!sha384(key_material.data(), key_material.size(), digest)) {
        error = "key derivation failed";
        return false;
    }
    const unsigned char* key = digest;  // first 32 bytes of SHA-384

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
        error = "AES is not available on this system";
        return false;
    }

    bool ok = false;
    BCRYPT_KEY_HANDLE symmetric = nullptr;
    do {
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                              sizeof(BCRYPT_CHAIN_MODE_CBC), 0) < 0) {
            error = "could not select CBC mode";
            break;
        }
        if (BCryptGenerateSymmetricKey(alg, &symmetric, nullptr, 0, const_cast<PUCHAR>(key), 32, 0) < 0) {
            error = "could not import the AES key";
            break;
        }

        unsigned char iv[16] = {};
        unsigned char iv_prefix[16] = {};
        std::string payload;

        if (encrypt) {
            if (!random_bytes(iv, sizeof(iv))) {
                error = "could not generate an IV";
                break;
            }
            // BCryptEncrypt writes the final chaining value back into pbIV, so
            // the value to store with the ciphertext has to be copied first.
            std::memcpy(iv_prefix, iv, sizeof(iv_prefix));
            payload = input;
        } else {
            if (input.size() < sizeof(iv)) {
                error = "ciphertext is too short to contain an IV";
                break;
            }
            std::memcpy(iv, input.data(), sizeof(iv));
            payload = input.substr(sizeof(iv));
            if (payload.empty() || payload.size() % 16 != 0) {
                error = "ciphertext is not a whole number of AES blocks";
                break;
            }
        }

        // Size the output from the input rather than a null-output probe:
        // BCryptDecrypt reports 0 for a null output buffer when PKCS#7 padding
        // is in play, which would hand the real call a zero-length buffer.
        const ULONG capacity = static_cast<ULONG>(payload.size()) + 16;
        std::string buffer(capacity, '\0');
        ULONG written = 0;
        const NTSTATUS status =
            encrypt ? BCryptEncrypt(symmetric, reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
                                    static_cast<ULONG>(payload.size()), nullptr, iv, sizeof(iv),
                                    reinterpret_cast<PUCHAR>(buffer.data()), capacity, &written, BCRYPT_BLOCK_PADDING)
                    : BCryptDecrypt(symmetric, reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
                                    static_cast<ULONG>(payload.size()), nullptr, iv, sizeof(iv),
                                    reinterpret_cast<PUCHAR>(buffer.data()), capacity, &written, BCRYPT_BLOCK_PADDING);
        if (status < 0) {
            error = encrypt ? "encryption failed" : "decryption failed (wrong key or damaged ciphertext)";
            char detail[64];
            std::snprintf(detail, sizeof(detail), " [ntstatus 0x%08lx]", static_cast<unsigned long>(status));
            error += detail;
            break;
        }
        buffer.resize(written);

        if (encrypt) {
            output.assign(reinterpret_cast<const char*>(iv_prefix), sizeof(iv_prefix));
            output += buffer;
        } else {
            output = std::move(buffer);
        }
        ok = true;
    } while (false);

    if (symmetric != nullptr) BCryptDestroyKey(symmetric);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// -- bindings ---------------------------------------------------------------

int l_base64_encode(lua_State* L) {
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    if (length > kMaxCryptBytes) fail(L, "base64_encode: data exceeds the 8 MiB limit");

    std::string out;
    out.reserve(((length + 2) / 3) * 4);
    for (std::size_t i = 0; i < length; i += 3) {
        const std::uint32_t b0 = static_cast<unsigned char>(data[i]);
        const std::uint32_t b1 = i + 1 < length ? static_cast<unsigned char>(data[i + 1]) : 0;
        const std::uint32_t b2 = i + 2 < length ? static_cast<unsigned char>(data[i + 2]) : 0;
        const std::uint32_t chunk = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back(i + 1 < length ? kBase64Alphabet[(chunk >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < length ? kBase64Alphabet[chunk & 0x3F] : '=');
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

int l_base64_decode(lua_State* L) {
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    if (length > kMaxCryptBytes) fail(L, "base64_decode: data exceeds the 8 MiB limit");

    std::string out;
    out.reserve((length / 4) * 3);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const char c = data[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const int value = base64_value(c);
        if (value < 0) fail(L, "base64_decode: invalid character at offset %zu", i);
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int l_crypt_hash(lua_State* L) {
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    if (length > kMaxCryptBytes) fail(L, "crypt.hash: data exceeds the 8 MiB limit");
    const std::string hex = sha384_hex(std::string(data, length));
    if (hex.empty()) fail(L, "crypt.hash: hashing failed");
    lua_pushlstring(L, hex.data(), hex.size());
    return 1;
}

int l_crypt_random(lua_State* L) {
    const int size = luaL_checkinteger(L, 1);
    if (size < 0 || static_cast<std::size_t>(size) > kMaxRandomChars) {
        fail(L, "crypt.random: size must be between 0 and %zu", kMaxRandomChars);
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (size > 0 && !random_bytes(bytes.data(), bytes.size())) fail(L, "crypt.random: the system RNG failed");

    std::string out(static_cast<std::size_t>(size), '\0');
    for (int i = 0; i < size; ++i) out[static_cast<std::size_t>(i)] = kRandomAlphabet[bytes[static_cast<std::size_t>(i)] % 62];
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int l_crypt_generatebytes(lua_State* L) {
    std::size_t length = 0;
    const char* value = luaL_checklstring(L, 1, &length);

    unsigned char salt[32];
    if (!random_bytes(salt, sizeof(salt))) fail(L, "crypt.generatebytes: the system RNG failed");

    std::string material(value, length);
    material.append(reinterpret_cast<const char*>(salt), sizeof(salt));
    unsigned char digest[kSha384Bytes];
    if (!sha384(material.data(), material.size(), digest)) fail(L, "crypt.generatebytes: hashing failed");
    lua_pushlstring(L, reinterpret_cast<const char*>(digest), sizeof(digest));
    return 1;
}

int l_crypt_generatekey(lua_State* L) {
    std::size_t length = 0;
    const char* value = luaL_checklstring(L, 1, &length);

    unsigned char salt[16];
    if (!random_bytes(salt, sizeof(salt))) fail(L, "crypt.generatekey: the system RNG failed");

    std::string material(value, length);
    material.append(reinterpret_cast<const char*>(salt), sizeof(salt));
    unsigned char digest[kSha384Bytes];
    if (!sha384(material.data(), material.size(), digest)) fail(L, "crypt.generatekey: hashing failed");

    const std::string hex = to_hex(digest, sizeof(digest));
    lua_pushlstring(L, hex.data(), hex.size());
    return 1;
}

int l_crypt_derive(lua_State* L) {
    std::size_t length = 0;
    const char* value = luaL_checklstring(L, 1, &length);
    const int wanted = luaL_checkinteger(L, 2);
    if (wanted <= 0 || static_cast<std::size_t>(wanted) > kMaxDeriveBytes) {
        fail(L, "crypt.derive: length must be between 1 and %zu", kMaxDeriveBytes);
    }

    // SHA-384 in counter mode: deterministic, so the same value and length
    // always produce the same key.
    std::string out;
    out.reserve(static_cast<std::size_t>(wanted));
    for (std::uint32_t counter = 0; out.size() < static_cast<std::size_t>(wanted); ++counter) {
        std::string material(value, length);
        for (int shift = 24; shift >= 0; shift -= 8) {
            material.push_back(static_cast<char>((counter >> shift) & 0xFF));
        }
        unsigned char digest[kSha384Bytes];
        if (!sha384(material.data(), material.size(), digest)) fail(L, "crypt.derive: hashing failed");
        out.append(reinterpret_cast<const char*>(digest), sizeof(digest));
    }
    out.resize(static_cast<std::size_t>(wanted));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int l_crypt_encrypt(lua_State* L) {
    std::size_t data_length = 0;
    std::size_t key_length = 0;
    const char* data = luaL_checklstring(L, 1, &data_length);
    const char* key = luaL_checklstring(L, 2, &key_length);

    std::string output;
    std::string error;
    if (!aes256_cbc(true, std::string(key, key_length), std::string(data, data_length), output, error)) {
        fail(L, "crypt.encrypt: %s", error.c_str());
    }
    lua_pushlstring(L, output.data(), output.size());
    return 1;
}

int l_crypt_decrypt(lua_State* L) {
    std::size_t data_length = 0;
    std::size_t key_length = 0;
    const char* data = luaL_checklstring(L, 1, &data_length);
    const char* key = luaL_checklstring(L, 2, &key_length);

    std::string output;
    std::string error;
    if (!aes256_cbc(false, std::string(key, key_length), std::string(data, data_length), output, error)) {
        fail(L, "crypt.decrypt: %s", error.c_str());
    }
    lua_pushlstring(L, output.data(), output.size());
    return 1;
}

const luaL_Reg kTopLevel[] = {
    {"base64encode", l_base64_encode},
    {"base64_encode", l_base64_encode},
    {"base64decode", l_base64_decode},
    {"base64_decode", l_base64_decode},
    {nullptr, nullptr},
};

const luaL_Reg kCryptTable[] = {
    {"hash", l_crypt_hash},
    {"random", l_crypt_random},
    {"generatebytes", l_crypt_generatebytes},
    {"generatekey", l_crypt_generatekey},
    {"derive", l_crypt_derive},
    {"encrypt", l_crypt_encrypt},
    {"decrypt", l_crypt_decrypt},
    {nullptr, nullptr},
};

}  // namespace

void install_crypt_api(lua_State* L) {
    for (const luaL_Reg* reg = kTopLevel; reg->name != nullptr; ++reg) {
        lua_pushcfunction(L, reg->func, reg->name);
        lua_setglobal(L, reg->name);
    }

    lua_newtable(L);
    for (const luaL_Reg* reg = kCryptTable; reg->name != nullptr; ++reg) {
        lua_pushcfunction(L, reg->func, reg->name);
        lua_setfield(L, -2, reg->name);
    }
    lua_setglobal(L, "crypt");
}

}  // namespace saintnova
