#pragma once

#include <string>
#include <vector>
#include <concepts>
#include <fstream>
#include <sstream>

#include <iostream>
#include <nlohmann/json.hpp>

struct StreamTransferError {
    std::string Message;
};

struct NamedScopes;

struct StreamScope {
    NamedScopes& Ctx;

    StreamScope(NamedScopes& Ctx, std::string const& Name);
    ~StreamScope();
};

#define EasyPush(CtxName, VarName) CtxName.template Push(#VarName, VarName)
#define EasyConsume(CtxName, VarName) VarName = CtxName.template Consume<decltype(VarName)>(#VarName)

using HashResult = std::string;

template <class T>
concept Primitive = (std::is_integral<T>::value || std::is_floating_point<T>::value || std::is_same<T, bool>::value || std::is_same<T, std::string>::value);

struct NamedScopes {
    std::vector<std::string> Scopes;

    inline std::string DumpScopes() {
        std::string Res;
        for (int i = 0; i < Scopes.size(); ++i) {
            Res += Scopes[Scopes.size() - i - 1];
            Res += "\n";
        }
        return Res;
    }

    inline virtual void BeginScope(std::string const& Name) { }
    inline virtual void EndScope() { }
};

struct JSONSerializer : public NamedScopes {
    nlohmann::json Data;
    std::vector<uint8_t> Binary;

    std::vector<nlohmann::json*> Scopes;

    nlohmann::json& GetCurrentScope() {
        return Scopes.empty() ? Data : *Scopes.back();
    }

    nlohmann::json& AtChecked(std::string const& Name) {
        if (GetCurrentScope().find(Name) != GetCurrentScope().end()) throw StreamTransferError { "Name " + Name + " already in use\n" };
        return GetCurrentScope()[Name];
    }

    template<typename T>
    requires (!Primitive<T> && !std::is_enum<T>::value)
    inline void Push(std::string const& Name, const T& Val) {
        BeginScope(Name);
        Val.Send(*this);
        EndScope();
    }

    template<typename T>
    requires (Primitive<T>)
    inline void Push(std::string const& Name, const T& Val) {
        if (GetCurrentScope().find(Name) != GetCurrentScope().end()) throw StreamTransferError { "Name already in use\n" };
        GetCurrentScope()[Name] = Val;
    }

    template<typename T>
    requires (std::is_enum<T>::value)
    inline void Push(std::string const& Name, const T& Val) {
        if (GetCurrentScope().find(Name) != GetCurrentScope().end()) throw StreamTransferError { "Name already in use\n" };
        GetCurrentScope()[Name] = static_cast<typename std::underlying_type<T>::type>(Val);
    }

    inline void PushBytes(std::string const& Name, const std::vector<uint8_t>& Bytes) {
        //std::string Base64;
        //Base64Encode(Base64, Bytes.data(), Bytes.size());
        //AtChecked(Name) = Base64;
        size_t Begin = Binary.size();
        size_t End = Binary.size() + Bytes.size();
        AtChecked("Begin") = Begin;
        AtChecked("End") = End;
        Binary.insert(Binary.end(), Bytes.begin(), Bytes.end());
    }

    inline virtual void BeginScope(std::string const& Name) override {
        nlohmann::json& NewScope = AtChecked(Name);
        Scopes.push_back(&NewScope);
    }
    inline virtual void EndScope() override {
        Scopes.pop_back();
    }

    void Dump(std::vector<uint8_t>& OutData, int Indent = -1) {
        std::string StringData = Data.dump(Indent);

        size_t OldIndex = OutData.size();
        OutData.resize(OldIndex + StringData.size() + 1);
        memcpy(&OutData[OldIndex], StringData.data(), StringData.size() + 1);
    }
};

struct JSONDeserializer : public NamedScopes {
    nlohmann::json Data;
    std::vector<uint8_t> Binary;

    std::vector<nlohmann::json*> Scopes;

    nlohmann::json& GetCurrentScope() {
        return Scopes.empty() ? Data : *Scopes.back();
    }

    nlohmann::json& AtChecked(std::string const& Name) {
        if (GetCurrentScope().find(Name) == GetCurrentScope().end()) throw StreamTransferError { "Scope " + Name + " not found in\n" + Scopes.back()->dump(2) };
        return GetCurrentScope()[Name];
    }

    inline nlohmann::json ConsumeValue(std::string const& Name) {
        auto Element = GetCurrentScope().find(Name);
        if (Element == GetCurrentScope().end()) {
            throw StreamTransferError { "Element named " + Name + " does not exist:\n" + DumpScopes() };
        }
        nlohmann::json Res = *Element;
        GetCurrentScope().erase(Element);
        return Res;
    }

    template<typename T>
    requires (!Primitive<T> && !std::is_enum<T>::value)
    inline T Consume(std::string const& Name) {
        BeginScope(Name);
        T Res;
        Res.Receive(*this);
        EndScope();
        return Res;
    }

    template<typename T>
    requires (Primitive<T>)
    inline T Consume(std::string const& Name) {
        try {
            return ConsumeValue(Name).get<T>();
        } catch (nlohmann::json::parse_error Err) {
            throw StreamTransferError { "Wrong Type: " + std::string(Err.what()) + "\n" + DumpScopes() };
        } catch (nlohmann::json::type_error Err) {
            throw StreamTransferError { "Wrong Type: " + std::string(Err.what()) + "\n" + DumpScopes() };
        }
    }

    template<typename T>
    requires (std::is_enum<T>::value)
    inline T Consume(std::string const& Name) {
        try {
            return static_cast<T>(ConsumeValue(Name).get<typename std::underlying_type<T>::type>());
        } catch (nlohmann::json::parse_error Err) {
            throw StreamTransferError { "Wrong Type: " + std::string(Err.what()) + "\n" + DumpScopes() };
        }
    }

    template<typename T>
    inline void ConsumeCheck(std::string const& Name, const T& Value) {
        if (ConsumeValue<T>(Name) != Value) {
            throw StreamTransferError { "Checked consume did not match expected value:\n" + DumpScopes() };
        }
    }

    // Overwrite all data in Bytes
    inline void ConsumeBytes(std::string const& Name, std::vector<uint8_t>& Bytes) {
        /*std::string Base64 = Consume<std::string>(Name);
        Base64Decode(Bytes, Base64);
        if (Bytes.size() != N) {
            throw StreamTransferError { "Ran out of bytes:\n" + DumpScopes() };
        }*/

        Bytes.resize(0);
        size_t Begin = AtChecked("Begin").get<size_t>();
        size_t End = AtChecked("End").get<size_t>();

        if (End < Begin || Begin > Binary.size() || End > Binary.size()) {
            std::cout << Binary.size() << "," << End << "\n";
            throw StreamTransferError { "Binary range was invalid:\n" + DumpScopes() };
        }

        Bytes.insert(Bytes.end(), Binary.begin() + Begin, Binary.begin() + End);
    }

    inline virtual void BeginScope(std::string const& Name) override {
        nlohmann::json& NewScope = AtChecked(Name);
        Scopes.push_back(&NewScope);
    }
    inline virtual void EndScope() override {
        Scopes.pop_back();
    }
};


#define BeginTransferStruct(Name) struct Name { private: static constexpr const char StructName[] = #Name; public:
#define BeginStructBased(Name, BaseName) struct Name : public BaseName { private: static constexpr const char StructName[] = #Name; public:
#define BeginStructBased2(Name, BaseName1, BaseName2) struct Name : public BaseName1, BaseName2 { private: static constexpr const char StructName[] = #Name; public:
#define EndStruct() };

#define BeginSend(CtxName)\
template<typename SerT>\
inline void Send(SerT& CtxName) const { StreamScope Scope(CtxName, StructName);

#define EndSend() }

#define BeginReceive(CtxName)\
template<typename SerT>\
inline void Receive(SerT& CtxName) { StreamScope Scope(CtxName, StructName);

#define EndSend() }

BeginTransferStruct(Buffer)
    std::vector<uint8_t> Data;

    std::string GetString() {
        std::string Res;
        Res.resize(Data.size());
        memcpy(Res.data(), Data.data(), Data.size());
        return Res;
    }

    void SetString(std::string const& Val) {
        Data.resize(Val.size());
        memcpy(Data.data(), Val.data(), Val.size());
    }

    Buffer() = default;
    inline Buffer(std::string const& Val) {
        SetString(Val);
    }

    BeginSend(Ctx)
        Ctx.PushBytes("Data", Data);
    EndSend()

    BeginReceive(Ctx)
        Ctx.ConsumeBytes("Data", Data);
    EndSend()
EndStruct()

template<typename T>
BeginTransferStruct(Vector)
    std::vector<T> Data;

    decltype(Data.begin()) begin() { return Data.begin(); }
    decltype(Data.end()) end() { return Data.end(); }

    decltype(((const std::vector<T>&)Data).begin()) begin() const { return Data.begin(); }
    decltype(((const std::vector<T>&)Data).end()) end() const { return Data.end(); }

    void erase(typename std::vector<T>::iterator const& it) { Data.erase(it); }

    BeginSend(Ctx)
        Ctx.template Push("Size", Data.size());
        for (size_t i = 0; i < Data.size(); ++i) {
            Ctx.template Push(std::to_string(i), Data[i]);
        }
    EndSend()

    BeginReceive(Ctx)
        size_t Size = Ctx.template Consume<size_t>("Size");
        Data.resize(0);
        Data.reserve(Size);
        for (size_t i = 0; i < Size; ++i) {
            StreamScope Scope(Ctx, "Receiving element " + std::to_string(i) + " of " + std::to_string(Size));
            Data.push_back(Ctx.template Consume<T>(std::to_string(i)));
        }
    EndSend()
EndStruct()

template<typename T>
BeginTransferStruct(Optional)
    std::optional<T> Value;

    void operator=(T const& Rhs) {
        Value = Rhs;
    }

    BeginSend(Ctx)
        if (Value.has_value()) {
            Ctx.template Push("ExistingOptional", *Value);
        }
    EndSend()

    BeginReceive(Ctx)
        if (Ctx.AtChecked("ExistingOptional") != nullptr) {
            Value = Ctx.template Consume<T>("ExistingOptional");
        }
    EndSend()
EndStruct()

template<typename T, size_t N>
BeginTransferStruct(Array)
    T Data[N];

    BeginSend(Ctx)
        Ctx.template Push("Size", N);
        for (size_t i = 0; i < N; ++i) {
            Ctx.template Push(std::to_string(i), Data[i]);
        }
    EndSend()

    BeginReceive(Ctx)
        Ctx.template ConsumeCheck<size_t>("Size", N);
        for (size_t i = 0; i < N; ++i) {
            StreamScope Scope(Ctx, "Receiving element " + std::to_string(i) + " of " + std::to_string(N));
            Data[i] = Ctx.template Consume<T>(std::to_string(i));
        }
    EndSend()
EndStruct()

bool ReadFileJSONCb(std::string const& Path, std::function<void(JSONDeserializer&)> const& Func);

void WriteFileJSONCb(std::string const& Path, std::function<void(JSONSerializer&)> const& Func);

template<typename T>
inline bool ReadFileJSON(std::string const& Path, T& Value) {
    return ReadFileJSONCb(Path, [&Value](JSONDeserializer& Deser) {
        Value.Receive(Deser);
    });
}

template<typename T>
inline T ReadFileJSONDefault(std::string const& Path) {
    T Res;
    ReadFileJSONCb(Path, [&Res](JSONDeserializer& Deser) {
        Res.Receive(Deser);
    });
    return Res;
}

template<typename T>
inline void WriteFileJSON(std::string const& Path, T& Value) {
    WriteFileJSONCb(Path, [&Value](JSONSerializer& Ser) {
        Value.Send(Ser);
    });
}

uint64_t HashCb(std::function<void(JSONSerializer&)> const& Func);

template<typename T>
uint64_t Hash(T& Value) {
    return HashCb([&Value](JSONSerializer& Ser) {
        Value.Send(Ser);
    });
}

template<typename T>
bool IsEqual(T& Lhs, T& Rhs) {
    return Hash(Lhs) == Hash(Rhs);
}

template<typename T>
class FileBacked {
public:
    const std::filesystem::path Path;

    T Value;

    void operator=(T const& Rhs) {
        Value = Rhs;
    }

    T* operator->() {
        return &Value;
    }

    const T* operator->() const {
        return &Value;
    }

    inline FileBacked(std::filesystem::path const& Path)
    : Path(Path) {
        ReadFileJSON(Path, Value);
    }

    void Flush() const {
        WriteFileJSON(Path, const_cast<T&>(Value));
    }

    ~FileBacked() {
        Flush();
    }
};