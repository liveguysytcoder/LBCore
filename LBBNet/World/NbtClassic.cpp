#include "NbtClassic.h"

namespace {

bool readU16LE(const vector<uint8_t>& data, size_t& offset, uint16_t& out) {
    if (offset + 2 > data.size()) return false;
    out = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return true;
}

bool readU32LE(const vector<uint8_t>& data, size_t& offset, uint32_t& out) {
    if (offset + 4 > data.size()) return false;
    out = static_cast<uint32_t>(data[offset]) |
          (static_cast<uint32_t>(data[offset + 1]) << 8) |
          (static_cast<uint32_t>(data[offset + 2]) << 16) |
          (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool readI32LE(const vector<uint8_t>& data, size_t& offset, int32_t& out) {
    uint32_t u;
    if (!readU32LE(data, offset, u)) return false;
    out = static_cast<int32_t>(u);
    return true;
}

bool readString(const vector<uint8_t>& data, size_t& offset, string& out) {
    uint16_t len;
    if (!readU16LE(data, offset, len)) return false;
    if (offset + len > data.size()) return false;
    out.assign(data.begin() + offset, data.begin() + offset + len);
    offset += len;
    return true;
}

// Skips (without exposing) one tag's PAYLOAD ONLY -- caller has already
// consumed the tag id and name. Recurses for Compound/List so nested
// structures don't desync the offset for whatever comes after them.
// Ints found directly inside the TOP-LEVEL compound are captured into
// `out` by the caller before calling this for types it wants to record;
// this function alone is only used for types this project doesn't need
// to read back (skip-and-continue).
bool skipPayload(const vector<uint8_t>& data, size_t& offset, NbtTag tag);

bool skipCompoundBody(const vector<uint8_t>& data, size_t& offset) {
    while (true) {
        if (offset >= data.size()) return false;
        uint8_t rawTag = data[offset++];
        if (rawTag == static_cast<uint8_t>(NbtTag::End)) return true;
        string name;
        if (!readString(data, offset, name)) return false;
        if (!skipPayload(data, offset, static_cast<NbtTag>(rawTag))) return false;
    }
}

bool skipPayload(const vector<uint8_t>& data, size_t& offset, NbtTag tag) {
    switch (tag) {
        case NbtTag::Byte:
            if (offset + 1 > data.size()) return false;
            offset += 1; return true;
        case NbtTag::Short:
            if (offset + 2 > data.size()) return false;
            offset += 2; return true;
        case NbtTag::Int:
            if (offset + 4 > data.size()) return false;
            offset += 4; return true;
        case NbtTag::Long:
            if (offset + 8 > data.size()) return false;
            offset += 8; return true;
        case NbtTag::Float:
            if (offset + 4 > data.size()) return false;
            offset += 4; return true;
        case NbtTag::Double:
            if (offset + 8 > data.size()) return false;
            offset += 8; return true;
        case NbtTag::ByteArray: {
            uint32_t len;
            if (!readU32LE(data, offset, len)) return false;
            if (offset + len > data.size()) return false;
            offset += len; return true;
        }
        case NbtTag::String: {
            string tmp;
            return readString(data, offset, tmp);
        }
        case NbtTag::List: {
            if (offset + 1 > data.size()) return false;
            uint8_t elemType = data[offset++];
            uint32_t count;
            if (!readU32LE(data, offset, count)) return false;
            for (uint32_t i = 0; i < count; i++) {
                if (!skipPayload(data, offset, static_cast<NbtTag>(elemType))) return false;
            }
            return true;
        }
        case NbtTag::Compound:
            return skipCompoundBody(data, offset);
        case NbtTag::IntArray: {
            uint32_t count;
            if (!readU32LE(data, offset, count)) return false;
            if (offset + (size_t)count * 4 > data.size()) return false;
            offset += (size_t)count * 4; return true;
        }
        case NbtTag::LongArray: {
            uint32_t count;
            if (!readU32LE(data, offset, count)) return false;
            if (offset + (size_t)count * 8 > data.size()) return false;
            offset += (size_t)count * 8; return true;
        }
        default:
            return false; // unknown tag id -- can't safely skip
    }
}

} // namespace

bool readNbtCompoundClassic(const vector<uint8_t>& data, size_t& offset, NbtCompoundView& out) {
    if (offset >= data.size()) return false;
    uint8_t rootTag = data[offset++];
    if (rootTag != static_cast<uint8_t>(NbtTag::Compound)) return false;

    string rootName;
    if (!readString(data, offset, rootName)) return false; // usually empty, still present

    while (true) {
        if (offset >= data.size()) return false;
        uint8_t rawTag = data[offset++];
        if (rawTag == static_cast<uint8_t>(NbtTag::End)) return true;

        string name;
        if (!readString(data, offset, name)) return false;

        NbtTag tag = static_cast<NbtTag>(rawTag);
        if (tag == NbtTag::Int) {
            int32_t v;
            if (!readI32LE(data, offset, v)) return false;
            out.ints[name] = v;
        } else if (tag == NbtTag::String) {
            string v;
            if (!readString(data, offset, v)) return false;
            out.strings[name] = v;
        } else {
            if (!skipPayload(data, offset, tag)) return false;
        }
    }
}

void writeNbtCompoundStartClassic(vector<uint8_t>& buf) {
    buf.push_back(static_cast<uint8_t>(NbtTag::Compound));
    buf.push_back(0); buf.push_back(0); // root name length = 0 (u16 LE)
}

void writeNbtCompoundEndClassic(vector<uint8_t>& buf) {
    buf.push_back(static_cast<uint8_t>(NbtTag::End));
}

namespace {
void writeNameClassic(vector<uint8_t>& buf, const string& name) {
    uint16_t len = static_cast<uint16_t>(name.size());
    buf.push_back(len & 0xFF);
    buf.push_back((len >> 8) & 0xFF);
    buf.insert(buf.end(), name.begin(), name.end());
}
}

void writeNbtIntClassic(vector<uint8_t>& buf, const string& name, int32_t value) {
    buf.push_back(static_cast<uint8_t>(NbtTag::Int));
    writeNameClassic(buf, name);
    uint32_t u = static_cast<uint32_t>(value);
    buf.push_back(u & 0xFF);
    buf.push_back((u >> 8) & 0xFF);
    buf.push_back((u >> 16) & 0xFF);
    buf.push_back((u >> 24) & 0xFF);
}

void writeNbtLongClassic(vector<uint8_t>& buf, const string& name, int64_t value) {
    buf.push_back(static_cast<uint8_t>(NbtTag::Long));
    writeNameClassic(buf, name);
    uint64_t u = static_cast<uint64_t>(value);
    for (int i = 0; i < 8; i++) {
        buf.push_back((u >> (8 * i)) & 0xFF);
    }
}

void writeNbtByteClassic(vector<uint8_t>& buf, const string& name, uint8_t value) {
    buf.push_back(static_cast<uint8_t>(NbtTag::Byte));
    writeNameClassic(buf, name);
    buf.push_back(value);
}

void writeNbtStringClassic(vector<uint8_t>& buf, const string& name, const string& value) {
    buf.push_back(static_cast<uint8_t>(NbtTag::String));
    writeNameClassic(buf, name);
    uint16_t len = static_cast<uint16_t>(value.size());
    buf.push_back(len & 0xFF);
    buf.push_back((len >> 8) & 0xFF);
    buf.insert(buf.end(), value.begin(), value.end());
}
