#include <iostream>
#include <string>
#include <memory>
#include <array>
#include <vector>
#include <sstream>
#include <fstream>
#include <thread>
#include <algorithm> 
#include <cctype>
#include <locale>
#include <algorithm>

#include <curl/curl.h>

#include "Transfer.hpp"

using std::string;

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

// trim from start (copying)
static inline std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

// trim from end (copying)
static inline std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

// trim from both ends (copying)
static inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

std::vector<uint8_t> exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::vector<uint8_t> result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    
    while (true) {
        size_t size = fread(buffer.data(), 1, buffer.size(), pipe.get());
        if (size == 0) break;
        size_t old = result.size();
        result.resize(result.size() + size);
        memcpy(result.data() + old, buffer.data(), size);
    }
    return result;
}

std::string read_entire_file(const std::string &filename) {
  std::ifstream file(filename);

  if (!file.is_open()) {
    return std::string();
  }

  std::stringstream stream;
  stream << file.rdbuf();

  file.close();

  return stream.str();
}

class OpenAI {
public:
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    static string CurlRequest(string const& URL, std::vector<string> const& Headers, string const& PostData) {
        CURL *curl;
        CURLcode res;
        std::string readBuffer;

        curl = curl_easy_init();
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, PostData.c_str());
            
            struct curl_slist *chunk = NULL;
            for (string const& Header : Headers) {
                chunk = curl_slist_append(chunk, Header.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            curl_slist_free_all(chunk);

            return readBuffer;
        }
        return "";
    }

    static string Request(string Text, string Stop, size_t MaxTokens, double Temperature) {
        if (MaxTokens > 100) MaxTokens = 100;

        Vector<string> RequestLog = ReadFileJSONDefault<Vector<string>>("RequestLog.json");

        string Key = read_entire_file("../openai-key.txt");

        nlohmann::json Request = {
            {"model", "text-davinci-003"},
            {"prompt", Text },
            {"temperature", Temperature},
            {"max_tokens", MaxTokens }
        };

        if (!Stop.empty()) {
            Request["stop"] = Stop;
        }

        string Res = CurlRequest(
            "https://api.openai.com/v1/completions",
            {
                string("Content-Type: application/json"),
                "Authorization: Bearer " + Key
            },
            Request.dump()
        );

        nlohmann::json Parsed;

        try {
            Parsed = nlohmann::json::parse(Res);
        } catch (nlohmann::json::parse_error er) {
            std::cout << "Result Parse Failed!\n" << Res << "\n\n";
            return "";
        }

        try {
            string Res = Parsed["choices"][0]["text"].get<string>();
            RequestLog.Data.push_back(Text + Res);
            WriteFileJSON("RequestLog.json", RequestLog);
            return Res;
        } catch (nlohmann::json::type_error er) {
            return Parsed.dump(4);
        }
    }
};

BeginTransferStruct(Object)
    string Name;
    string Description;

    BeginSend(Ctx)
        EasyPush(Ctx, Name);
    EndSend()

    BeginReceive(Ctx)
        EasyConsume(Ctx, Name);
    EndSend()
EndStruct()

BeginTransferStruct(Conversation)
    BeginTransferStruct(Entry)
        string Character;
        string Text;

        BeginSend(Ctx)
            EasyPush(Ctx, Character);
            EasyPush(Ctx, Text);
        EndSend()

        BeginReceive(Ctx)
            EasyConsume(Ctx, Character);
            EasyConsume(Ctx, Text);
        EndSend()
    EndStruct()

    Vector<Object> Characters;

    Vector<Entry> Entries;

    string CharacterDescriptions() const {
        string Res;

        for (size_t i = 0; i < Characters.Data.size(); ++i) {
            Res += "Description of " + Characters.Data[i].Name + ":\n";
            Res += Characters.Data[i].Description + ((i == Characters.Data.size() - 1) ? "" : "\n\n");
        }

        return Res;
    }

    string ConversationText() const {
        string Res;

        for (Entry const& E : Entries) {
            Res += E.Character + ": \"" + E.Text + "\"\n";
        }

        return Res;
    }

    void AddEntry(int CharacterIndex, string const& Text) {
        Entries.Data.push_back(Entry { Characters.Data[CharacterIndex].Name, Text });
    }

    string CompleteCharacterEntry(int CharacterIndex, int TokenCount = 32) {
        if (CharacterIndex < 0 || CharacterIndex >= Characters.Data.size()) {
            return "";
        }

        string Query
        = CharacterDescriptions() + "\n\n"
        + ConversationText() + "\n"
        + Characters.Data[CharacterIndex].Name + ": \"";

        string Res = OpenAI::Request(Query, "\"", TokenCount, 1.0);

        if (!Res.empty()) {
            AddEntry(CharacterIndex, Res);
        }

        return Res;
    }

    // Query the state of the conversation with a fixed set of options.
    string QueryMultipleChoice(string const& Query, std::vector<string> const& Options) {
        string QueryText = ConversationText();
        QueryText = "Below is a conversation, and a multiple choice question. After reading the conversation, select the best possibe answer.\n\n" + QueryText;
        QueryText += "\n\nQuestion: " + Query + "\n";
        
        QueryText += "There are " + std::to_string(Options.size()) + " possible answers: ";
        for (size_t i = 0; i < Options.size(); ++i) {
            QueryText += "\"" + Options[i] + ((i == Options.size() - 1) ? "\"" : "\", ");
        }
        QueryText += ". Which is the best? Answer in quotes: \"";

        string Res = OpenAI::Request(QueryText, "\"", 2, 1.0);
        return Res;
        auto it = std::find(Options.begin(), Options.end(), Res);
        if (it != Options.end()) {
            return Res;
        }
        return "";
    }

    BeginSend(Ctx)
        EasyPush(Ctx, Entries);
    EndSend()

    BeginReceive(Ctx)
        EasyConsume(Ctx, Entries);
    EndSend()
EndStruct()

int main(int argc, char** argv) {
    system("clear");

    Conversation Convo;
    Convo.Characters.Data.resize(2);
    Convo.Characters.Data[0].Name = "Alice";
    Convo.Characters.Data[0].Description = 
    
    "Alice is a criminal. Alice has bombed two cities. Alice is very clever. Alice will never get caught. Alice is now living in China and she is mkaing a ton of money. Alice knows Bob as a delivery boy.";
    
    
    /*"Alice is a computer program designed to control access to a locked door. Alice is very strict, and will never unlock the door, except in one circumstance. If the person she is talking to says \"Open Sesame\", she will unlock the door. The person she is talking to must directly say \"Open Sesame\", and Alice cannot be convinced in any way other than directly hearing these words. When she grants access, she will say \"Access Granted.\". She is programmed not to reveal the secret password if asked. When asked what she is doing, Allice will explain that she is guarding the door.";*/
    Convo.Characters.Data[1].Name = "Bob";
    Convo.Characters.Data[1].Description = "Bob is a regular person.";

    while (true) {
        string UserLine;
        std::cout << Convo.Characters.Data[1].Name << ": ";
        getline(std::cin, UserLine); 
        trim(UserLine);

        Convo.AddEntry(1, UserLine);

        system("clear");
        string ConvoText = Convo.ConversationText();
        std::cout << ConvoText;

        std::cout << Convo.Characters.Data[0].Name << ":";
        std::cout.flush();

        string OtherResponse = Convo.CompleteCharacterEntry(0);

        string QueryTest = Convo.QueryMultipleChoice(
            "Has the conversation between " + Convo.Characters.Data[0].Name + " and " + Convo.Characters.Data[0].Name + " ended?",
            { "Yes", "No" }
        );

        std::cout << " \"" << OtherResponse << "\"\n";

        if (QueryTest == "Yes") {
            break;
        }
    }

    std::cout << "\n\nConversation complete!";

    return 0;
}