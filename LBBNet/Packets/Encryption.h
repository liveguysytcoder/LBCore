#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "../../LBNet/Server/Clients.h"

using namespace std;

// Bedrock's login encryption handshake uses NIST P-384 (secp384r1) ECDH key
// agreement plus ECDSA (JWT "ES384") signing — the same curve/algorithm the
// vanilla client and server use. This is only reached when a client logs in
// with a real Xbox Live identity chain (i.e. the login JWT's payload has an
// "identityPublicKey" claim) rather than the offline/no-chain flow this
// project supported before. A real official client — like Minecraft PE from
// the Play Store, signed in and online — always sends one.

// Generates the server's single EC keypair for this process's lifetime.
// Call once at startup (see executer.cpp), before any client can log in.
void generateServerKeyPair();

// The server's public key as base64 X.509 SubjectPublicKeyInfo DER — this
// goes in the ServerToClientHandshake JWT's "x5u" header so the client can
// verify the signature and derive the same shared secret.
string getServerPublicKeyBase64();

// Builds the signed ServerToClientHandshake JWT:
//   header:  {"alg":"ES384","x5u":"<server public key, base64 DER>"}
//   payload: {"salt":"<salt, base64>"}
// ES384-signed with the server's private key, in JOSE (r||s) form.
string buildHandshakeJwt(const vector<uint8_t>& salt);

// ECDH between the server's private key and the client's public key (DER
// bytes — caller base64-decodes the login JWT's "identityPublicKey" first).
// Returns the raw shared secret, or an empty vector if the client's key is
// malformed/not on the expected curve.
vector<uint8_t> deriveSharedSecret(const vector<uint8_t>& clientPublicKeyDer);

// encryptionKey = SHA-256(salt + sharedSecret). Per the Bedrock protocol
// this 32-byte hash is used directly as the AES-256 key, and its first 16
// bytes double as the initial CFB8 IV — there's no separate IV exchange.
vector<uint8_t> computeEncryptionKey(const vector<uint8_t>& salt, const vector<uint8_t>& sharedSecret);

// Sets up state's persistent AES-256-CFB8 encrypt/decrypt contexts from the
// derived key and resets both packet counters. Call once, right after
// ServerToClientHandshake is sent — from that point on every packet in
// both directions on this connection is encrypted.
void initClientEncryption(ClientState& state, const vector<uint8_t>& encryptionKey);

// Encrypts one outgoing batch body (varint length + packet bytes, as built
// by GamePacketSender) in place: appends the 8-byte checksum trailer, then
// runs it through state's persistent send cipher context. Advances
// state.sendPacketCounter.
void encryptGamePacket(ClientState& state, vector<uint8_t>& batch);

// Decrypts one incoming ciphertext blob in place through state's
// persistent receive cipher context, then verifies and strips the trailing
// 8-byte checksum. Advances state.recvPacketCounter regardless of outcome
// (the counter is part of the stream, not just the check). Returns false
// if the checksum doesn't match — the caller should drop the packet.
bool decryptGamePacket(ClientState& state, vector<uint8_t>& data);

// Frees state's AES cipher contexts. Call when a client disconnects
// (see LBNet/Server/Clients.cpp's removeClient) to avoid leaking them.
void destroyClientEncryption(ClientState& state);

// --- Xbox Live chain verification ---
//
// Mojang's known root public key (base64 X.509 SubjectPublicKeyInfo DER,
// P-384). It appears as the "identityPublicKey" of whichever link in a
// real Xbox Live-authenticated chain Mojang itself signed. This value is
// constant and widely published (e.g. gophertunnel's minecraft/protocol/
// login/jwt package, and multiple other open-source Bedrock server
// projects cite the identical string) -- verified against Minecraft
// Wiki's own login-sequence documentation page.
extern const char* kMojangRootPublicKeyBase64;

// Verifies one JWT's ES384 signature against `publicKeyBase64Der` (base64
// X.509 SubjectPublicKeyInfo DER -- the same format as
// getServerPublicKeyBase64()'s output and the chain's own
// "identityPublicKey" claims). `signingInput` is the JWT with its
// signature segment removed ("<base64url header>.<base64url payload>");
// `rawSignatureB64Url` is the JWT's third (signature) segment, still
// base64url-encoded exactly as the token carried it. Converts the raw
// JOSE (r||s) signature back to ASN.1 DER internally -- the reverse of
// what signES384() (used for our own outgoing handshake JWT) does.
bool verifyES384Signature(const string& signingInput, const string& rawSignatureB64Url,
                           const string& publicKeyBase64Der);
