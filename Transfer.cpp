#include "Transfer.hpp"

//#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

uint64_t FNV1(uint8_t* Data, size_t Size) {
    uint64_t Hash = 14695981039346656037ull;
    for (size_t i = 0; i < Size; ++i) {
        Hash = Hash * 1099511628211ull;
        Hash = Hash ^ static_cast<size_t>(Data[i]);
    }
    return Hash;
}

/*std::string SHA256(uint8_t* Data, size_t Size)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, Data, Size);
    SHA256_Final(hash, &sha256);
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}*/

StreamScope::StreamScope(NamedScopes& Ctx, std::string const& Name) : Ctx(Ctx) {
    Ctx.Scopes.push_back(Name);
}
StreamScope::~StreamScope() {
    Ctx.Scopes.pop_back();
}

bool ReadFileJSONCb(std::string const& Path, std::function<void(JSONDeserializer&)> const& Func) {
    std::ifstream t(Path, std::ios::binary | std::ios::ate);

    if (!t.good()) {
        return false;
    }
    
    std::vector<uint8_t> buffer;

    {
        std::streamsize size = t.tellg();
        t.seekg(0, std::ios::beg);
        buffer.resize(size);

        if (!t.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return false;
        }
    }

    std::string StringData = reinterpret_cast<char*>(buffer.data());
    buffer.erase(buffer.begin() + StringData.size() + 1, buffer.end());

    JSONDeserializer Deser;
    Deser.Data = nlohmann::json::parse(StringData);
    Deser.Binary = std::move(buffer);

    Func(Deser);

    return true;
}

void WriteFileJSONCb(std::string const& Path, std::function<void(JSONSerializer&)> const& Func) {
    JSONSerializer Ser;

    Func(Ser);

    std::ofstream t(Path, std::ios::trunc | std::ios::binary);
    std::string stringData = Ser.Data.dump(2);
    stringData.push_back('\0');
    t << stringData;

    t.write(reinterpret_cast<char*>(Ser.Binary.data()), Ser.Binary.size());
}

uint64_t HashCb(std::function<void(JSONSerializer&)> const& Func) {
    JSONSerializer Ser;

    Func(Ser);
    std::string StringData = Ser.Data.dump();
    uint64_t Hashes[2];
    Hashes[0] = FNV1(reinterpret_cast<uint8_t*>(StringData.data()), StringData.size());
    Hashes[1] = FNV1(Ser.Binary.data(), Ser.Binary.size());

    return FNV1(reinterpret_cast<uint8_t*>(Hashes), sizeof(Hashes));
}