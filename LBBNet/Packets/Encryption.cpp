#include "Encryption.h"
#include "Base64Url.h"
#include <iostream>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/x509.h>

using namespace std;

namespace {

// One EC keypair for the whole server process — generated once at startup
// by generateServerKeyPair() and reused for every client's handshake.
EVP_PKEY* g_serverKey = nullptr;

void logOpenSslError(const string& where) {
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    cout << "[Encryption] " << where << " failed: " << buf << "\n";
}

// SHA-256 over arbitrary bytes.
vector<uint8_t> sha256(const vector<uint8_t>& input) {
    vector<uint8_t> hash(EVP_MAX_MD_SIZE);
    unsigned int hashLen = 0;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(mdctx, input.data(), input.size());
    EVP_DigestFinal_ex(mdctx, hash.data(), &hashLen);
    EVP_MD_CTX_free(mdctx);

    hash.resize(hashLen); // 32 bytes
    return hash;
}

// The 8-byte checksum trailer appended to every encrypted batch:
// SHA-256(counter as 8-byte little-endian + plaintext-batch + encryptionKey)[0:8].
// `counter` is per-direction (send/recv each have their own) and increments
// once per packet — it's what stops a captured packet from being replayed.
vector<uint8_t> computeTrailer(const vector<uint8_t>& key, uint64_t counter, const vector<uint8_t>& plaintext) {
    vector<uint8_t> input;
    input.reserve(8 + plaintext.size() + key.size());
    for (int i = 0; i < 8; i++) {
        input.push_back(static_cast<uint8_t>((counter >> (8 * i)) & 0xFF));
    }
    input.insert(input.end(), plaintext.begin(), plaintext.end());
    input.insert(input.end(), key.begin(), key.end());

    vector<uint8_t> hash = sha256(input);
    return vector<uint8_t>(hash.begin(), hash.begin() + 8);
}

} // namespace

void generateServerKeyPair() {
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!pctx || EVP_PKEY_keygen_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_secp384r1) <= 0 ||
        EVP_PKEY_keygen(pctx, &g_serverKey) <= 0) {
        logOpenSslError("generateServerKeyPair");
        cout << "[Encryption] FATAL: could not generate server EC keypair — "
                "encrypted (Xbox Live) logins will not work this run\n";
    } else {
        cout << "[Encryption] Generated server P-384 keypair for the login handshake\n";
    }
    if (pctx) EVP_PKEY_CTX_free(pctx);
}

string getServerPublicKeyBase64() {
    if (!g_serverKey) return "";

    unsigned char* derBuf = nullptr;
    int len = i2d_PUBKEY(g_serverKey, &derBuf);
    if (len <= 0) {
        logOpenSslError("getServerPublicKeyBase64 (i2d_PUBKEY)");
        return "";
    }

    vector<uint8_t> der(derBuf, derBuf + len);
    OPENSSL_free(derBuf);
    return base64Encode(der);
}

namespace {

// Signs `data` with the server's private key (ECDSA/SHA-384) and returns
// the raw JOSE signature: r and s, each left-padded to 48 bytes (P-384's
// field size) and concatenated — NOT the ASN.1 DER form OpenSSL produces
// by default, which is what JWT's "ES384" alg actually requires.
vector<uint8_t> signES384(const vector<uint8_t>& data) {
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha384(), nullptr, g_serverKey) <= 0) {
        logOpenSslError("signES384 (DigestSignInit)");
        EVP_MD_CTX_free(mdctx);
        return {};
    }
    if (EVP_DigestSignUpdate(mdctx, data.data(), data.size()) <= 0) {
        logOpenSslError("signES384 (DigestSignUpdate)");
        EVP_MD_CTX_free(mdctx);
        return {};
    }

    size_t sigLen = 0;
    EVP_DigestSignFinal(mdctx, nullptr, &sigLen);
    vector<uint8_t> derSig(sigLen);
    if (EVP_DigestSignFinal(mdctx, derSig.data(), &sigLen) <= 0) {
        logOpenSslError("signES384 (DigestSignFinal)");
        EVP_MD_CTX_free(mdctx);
        return {};
    }
    derSig.resize(sigLen);
    EVP_MD_CTX_free(mdctx);

    const unsigned char* p = derSig.data();
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(derSig.size()));
    if (!sig) {
        logOpenSslError("signES384 (d2i_ECDSA_SIG)");
        return {};
    }

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig, &r, &s);

    vector<uint8_t> raw(96, 0); // 48 bytes r + 48 bytes s
    BN_bn2binpad(r, raw.data(), 48);
    BN_bn2binpad(s, raw.data() + 48, 48);
    ECDSA_SIG_free(sig);

    return raw;
}

} // namespace

string buildHandshakeJwt(const vector<uint8_t>& salt) {
    string header  = "{\"alg\":\"ES384\",\"x5u\":\"" + getServerPublicKeyBase64() + "\"}";
    string payload = "{\"salt\":\"" + base64Encode(salt) + "\"}";

    string signingInput = base64UrlEncode(vector<uint8_t>(header.begin(), header.end())) +
                           "." +
                           base64UrlEncode(vector<uint8_t>(payload.begin(), payload.end()));

    vector<uint8_t> signature = signES384(vector<uint8_t>(signingInput.begin(), signingInput.end()));

    return signingInput + "." + base64UrlEncode(signature);
}

vector<uint8_t> deriveSharedSecret(const vector<uint8_t>& clientPublicKeyDer) {
    if (clientPublicKeyDer.empty() || !g_serverKey) return {};

    const unsigned char* p = clientPublicKeyDer.data();
    EVP_PKEY* clientKey = d2i_PUBKEY(nullptr, &p, static_cast<long>(clientPublicKeyDer.size()));
    if (!clientKey) {
        logOpenSslError("deriveSharedSecret (d2i_PUBKEY — malformed client key)");
        return {};
    }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(g_serverKey, nullptr);
    vector<uint8_t> secret;

    if (!ctx || EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx, clientKey) <= 0) {
        logOpenSslError("deriveSharedSecret (derive_init/set_peer)");
    } else {
        size_t secretLen = 0;
        if (EVP_PKEY_derive(ctx, nullptr, &secretLen) > 0) {
            secret.resize(secretLen);
            if (EVP_PKEY_derive(ctx, secret.data(), &secretLen) > 0) {
                secret.resize(secretLen);
            } else {
                logOpenSslError("deriveSharedSecret (derive)");
                secret.clear();
            }
        }
    }

    if (ctx) EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(clientKey);

    // EVP_PKEY_derive() for EC keys returns the shared secret as the raw
    // big-endian encoding of the agreed point's X coordinate, WITHOUT
    // left-padding to the curve's field width. secp384r1's field is 48
    // bytes, but whenever that X coordinate's top byte is 0x00 (~1/256 of
    // handshakes), OpenSSL just omits it and hands back 47 bytes instead.
    // The client always treats the secret as a fixed 48-byte value, so an
    // unpadded secret here silently produces a different encryptionKey —
    // every packet after ServerToClientHandshake then fails the checksum
    // check and gets dropped, hanging the connection. Left-pad with zero
    // bytes back to the field width to match what the client computes.
    constexpr size_t kP384FieldBytes = 48;
    if (!secret.empty() && secret.size() < kP384FieldBytes) {
        vector<uint8_t> padded(kP384FieldBytes - secret.size(), 0);
        padded.insert(padded.end(), secret.begin(), secret.end());
        secret = padded;
    }
    cout << "[DEBUG] derived secret length: " << secret.size() << " bytes\n";
    return secret;
}

vector<uint8_t> computeEncryptionKey(const vector<uint8_t>& salt, const vector<uint8_t>& sharedSecret) {
    vector<uint8_t> input;
    input.reserve(salt.size() + sharedSecret.size());
    input.insert(input.end(), salt.begin(), salt.end());
    input.insert(input.end(), sharedSecret.begin(), sharedSecret.end());
    return sha256(input); // 32 bytes — used directly as the AES-256 key
}

void initClientEncryption(ClientState& state, const vector<uint8_t>& encryptionKey) {
    state.encryptionKey = encryptionKey;

    // AES-256-CTR — NOT CFB8. Per the real client's implementation (see
    // gophertunnel's Encoder.EnableEncryption), the nonce/IV is the first
    // 12 bytes of the encryption key followed by a 4-byte big-endian
    // counter starting at 2 — there's no separate IV exchange, but it is
    // NOT simply "first 16 bytes of the key" as CFB8 mode would use.
    // Using the wrong mode/IV here silently produces ciphertext the client
    // can't decrypt (and vice versa): every packet after
    // ServerToClientHandshake then fails the checksum check and gets
    // dropped, hanging the connection — even though the ECDH secret and
    // encryptionKey themselves are computed correctly.
    vector<uint8_t> iv(encryptionKey.begin(), encryptionKey.begin() + 12);
    iv.push_back(0);
    iv.push_back(0);
    iv.push_back(0);
    iv.push_back(2);

    EVP_CIPHER_CTX* enc = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(enc, EVP_aes_256_ctr(), nullptr, encryptionKey.data(), iv.data());
    state.sendCipherCtx = enc;

    EVP_CIPHER_CTX* dec = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(dec, EVP_aes_256_ctr(), nullptr, encryptionKey.data(), iv.data());
    state.recvCipherCtx = dec;

    state.sendPacketCounter = 0;
    state.recvPacketCounter = 0;
    state.encryptionEnabled = true;
}

void encryptGamePacket(ClientState& state, vector<uint8_t>& batch) {
    vector<uint8_t> trailer = computeTrailer(state.encryptionKey, state.sendPacketCounter, batch);
    state.sendPacketCounter++;
    batch.insert(batch.end(), trailer.begin(), trailer.end());

    vector<uint8_t> out(batch.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0;
    EVP_EncryptUpdate(static_cast<EVP_CIPHER_CTX*>(state.sendCipherCtx),
                       out.data(), &outLen, batch.data(), static_cast<int>(batch.size()));
    out.resize(outLen);
    batch = out;
}

bool decryptGamePacket(ClientState& state, vector<uint8_t>& data) {
    vector<uint8_t> out(data.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0;
    EVP_DecryptUpdate(static_cast<EVP_CIPHER_CTX*>(state.recvCipherCtx),
                       out.data(), &outLen, data.data(), static_cast<int>(data.size()));
    out.resize(outLen);

    if (out.size() < 8) return false;

    vector<uint8_t> plaintext(out.begin(), out.end() - 8);
    vector<uint8_t> trailer(out.end() - 8, out.end());

    vector<uint8_t> expected = computeTrailer(state.encryptionKey, state.recvPacketCounter, plaintext);
    state.recvPacketCounter++;

    if (trailer != expected) {
        return false;
    }

    data = plaintext;
    return true;
}

void destroyClientEncryption(ClientState& state) {
    if (state.sendCipherCtx) {
        EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(state.sendCipherCtx));
        state.sendCipherCtx = nullptr;
    }
    if (state.recvCipherCtx) {
        EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(state.recvCipherCtx));
        state.recvCipherCtx = nullptr;
    }
}

// Verified against Minecraft Wiki's Bedrock Login Sequence documentation
// page, which cites this exact base64 string as Mojang's constant root
// public key (P-384 / secp384r1, X.509 SubjectPublicKeyInfo DER) used to
// sign one link in a real Xbox Live-authenticated chain.
const char* kMojangRootPublicKeyBase64 =
    "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE8ELkixyLcwlZryUQcu1TvPOmI2B7vX83ndnW"
    "RUaXm74wFfa5f/lwQNTfrLVHa2PmenpGI6JhIMUJaWZrjmMj90NoKNFSNBuKdm8rYiXs"
    "faz3K36x/1U26HpG0ZxK/V1V";

bool verifyES384Signature(const string& signingInput, const string& rawSignatureB64Url,
                           const string& publicKeyBase64Der) {
    vector<uint8_t> derKey = base64Decode(publicKeyBase64Der);
    if (derKey.empty()) return false;

    const unsigned char* p = derKey.data();
    EVP_PKEY* pubKey = d2i_PUBKEY(nullptr, &p, static_cast<long>(derKey.size()));
    if (!pubKey) {
        logOpenSslError("verifyES384Signature (d2i_PUBKEY — malformed public key)");
        return false;
    }

    vector<uint8_t> rawSig = base64UrlDecode(rawSignatureB64Url);
    if (rawSig.size() != 96) { // 48 bytes r + 48 bytes s (P-384 field width)
        EVP_PKEY_free(pubKey);
        return false;
    }

    // Reverse of signES384(): rebuild the ASN.1 DER signature OpenSSL's
    // verify functions expect from the raw JOSE (r||s) form the JWT carries.
    BIGNUM* r = BN_bin2bn(rawSig.data(), 48, nullptr);
    BIGNUM* s = BN_bin2bn(rawSig.data() + 48, 48, nullptr);
    ECDSA_SIG* sig = ECDSA_SIG_new();
    ECDSA_SIG_set0(sig, r, s); // sig takes ownership of r and s

    unsigned char* derSig = nullptr;
    int derSigLen = i2d_ECDSA_SIG(sig, &derSig);
    ECDSA_SIG_free(sig);
    if (derSigLen <= 0) {
        logOpenSslError("verifyES384Signature (i2d_ECDSA_SIG)");
        EVP_PKEY_free(pubKey);
        return false;
    }

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    bool ok = false;
    if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha384(), nullptr, pubKey) > 0 &&
        EVP_DigestVerifyUpdate(mdctx, signingInput.data(), signingInput.size()) > 0) {
        ok = (EVP_DigestVerifyFinal(mdctx, derSig, static_cast<size_t>(derSigLen)) == 1);
    } else {
        logOpenSslError("verifyES384Signature (DigestVerifyInit/Update)");
    }

    EVP_MD_CTX_free(mdctx);
    OPENSSL_free(derSig);
    EVP_PKEY_free(pubKey);
    return ok;
}
