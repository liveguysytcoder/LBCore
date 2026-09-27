#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <map>

using namespace std;

// Classic little-endian NBT -- Minecraft's on-disk save format, used by
// level.dat. This is a DIFFERENT wire format from the "network NBT" used
// elsewhere in this project (see BiomeDefinitionData.cpp): tag/string
// lengths here are fixed 2-byte (String) or 4-byte (ByteArray/List/
// IntArray) little-endian integers, NOT VarInts, and Int/Long payloads
// are fixed-width LE, NOT ZigZag VarInts. Per Minecraft Wiki's Bedrock
// Edition level format page: level.dat's NBT body uses this classic
// scheme, not the network one.
//
// This is a minimal reader/writer: only the tag types level.dat actually
// needs (Int, String, plus enough structure-walking to skip anything
// else) are implemented. It is NOT a general-purpose NBT library.

enum class NbtTag : uint8_t {
    End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6,
    ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12
};

// A tiny read-only view of a parsed compound's top-level scalar fields,
// good enough for level.dat's purposes (checking StorageVersion, reading
// LevelName/Spawn coordinates back for our own generated worlds). Nested
// compounds/lists are walked (so parsing doesn't desync) but their
// contents are not exposed here -- not needed for anything this project
// currently reads level.dat for.
struct NbtCompoundView {
    map<string, int32_t> ints;
    map<string, string> strings;
};

// Parses a classic-LE NBT compound starting at `offset` in `data`
// (immediately after level.dat's 8-byte version+length header). Advances
// offset past the parsed compound. Returns false if the data is
// truncated/malformed partway through -- callers should treat that as
// "couldn't read this file" rather than crash.
bool readNbtCompoundClassic(const vector<uint8_t>& data, size_t& offset, NbtCompoundView& out);

// --- Writer side ---
// Appends one named Int field (tag id, u16 name length + name, then the
// 4-byte LE value) to `buf`.
void writeNbtIntClassic(vector<uint8_t>& buf, const string& name, int32_t value);

// Appends one named String field.
void writeNbtStringClassic(vector<uint8_t>& buf, const string& name, const string& value);

// Appends one named Byte field.
void writeNbtByteClassic(vector<uint8_t>& buf, const string& name, uint8_t value);

// Appends one named Long field (8-byte LE).
void writeNbtLongClassic(vector<uint8_t>& buf, const string& name, int64_t value);

// Writes the TAG_Compound(0x0A) header + empty root name (u16 length=0)
// that must open every level.dat NBT body -- call once before any of the
// writeNbt*Classic() field functions above, then close with
// writeNbtCompoundEndClassic().
void writeNbtCompoundStartClassic(vector<uint8_t>& buf);

// Appends the TAG_End(0x00) byte that closes the root compound.
void writeNbtCompoundEndClassic(vector<uint8_t>& buf);
