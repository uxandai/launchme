// Requests to the model: the request body, function calls, usage, hedged and streamed posts and
// ask() with its tool loop, retries and stream fallback.
#include "companion_internal.hpp"
#include "companion_person.hpp"
#include "companion_providers.hpp"
#include "json.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

namespace companion {
using namespace detail;

namespace {
std::string quote(std::string_view value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out + '"';
}
} // namespace

std::vector<Turn> alternate(const std::vector<Turn>& turns) {
    // The conversation starts with the player and alternates; consecutive turns of one side merge.
    std::vector<Turn> merged;
    for (const auto& turn : turns) {
        if (turn.text.empty() || (merged.empty() && !turn.user)) continue;
        if (!merged.empty() && merged.back().user == turn.user) merged.back().text += L"\n" + turn.text;
        else merged.push_back(turn);
    }
    return merged;
}

std::string requestBody(const std::wstring& system, const std::vector<Turn>& turns, int max_tokens, float temperature, const RequestOptions& options) {
    const auto merged = alternate(turns);
    std::string body = "{\"systemInstruction\":{\"parts\":[{\"text\":" + quote(text_encoding::utf8Lenient(system)) + "}]},\"contents\":[";
    for (size_t i = 0; i < merged.size(); ++i) {
        if (i) body += ',';
        body += std::string("{\"role\":\"") + (merged[i].user ? "user" : "model") + "\",\"parts\":[{\"text\":" +
                quote(text_encoding::utf8Lenient(merged[i].text)) + "}]}";
    }
    for (const auto& content : options.contents) { if (body.back() != '[') body += ','; body += content; }
    body += "]";
    if (options.tools && !options.tools->declarations.empty()) {
        body += ",\"tools\":[{\"functionDeclarations\":" + options.tools->declarations + "}]";
        body += std::string(",\"toolConfig\":{\"functionCallingConfig\":{\"mode\":\"") + (options.tools_off ? "NONE" : "AUTO") + "\"}}";
    }
    body += ",\"generationConfig\":{\"temperature\":" + std::to_string(std::clamp(temperature, 0.0f, 2.0f)).substr(0, 4) +
            ",\"maxOutputTokens\":" + std::to_string(std::clamp(max_tokens, 16, 8192));
    if (options.no_thinking) body += ",\"thinkingConfig\":{\"thinkingBudget\":0}";
    body += "}}";
    return body;
}

std::optional<Calls> functionCalls(const HttpResponse& response) {
    if (response.status != 200) return std::nullopt;
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
    const auto* candidates = parsed ? parsed->get("candidates") : nullptr;
    if (!candidates || candidates->type != json::Value::Type::array || candidates->elements.empty()) return std::nullopt;
    const auto* content = candidates->elements.front().get("content");
    const auto* parts = content ? content->get("parts") : nullptr;
    if (!parts || parts->type != json::Value::Type::array) return std::nullopt;
    Calls result;
    for (const auto& part : parts->elements) {
        const auto* call = part.get("functionCall");
        if (!call || call->type != json::Value::Type::object) continue;
        FunctionCall item;
        if (const auto* id = call->get("id")) item.id = id->stringOr();
        if (const auto* name = call->get("name")) item.name = name->stringOr();
        if (const auto* args = call->get("args"); args && args->type == json::Value::Type::object) item.args = *args;
        else item.args = json::object();
        if (!item.name.empty()) result.calls.push_back(std::move(item));
    }
    if (result.calls.empty()) return std::nullopt;
    // Sent back as it came (the role too): Gemini checks the thought signatures in it.
    auto turn = *content;
    if (!turn.get("role")) turn.properties["role"] = json::string("model");
    result.content = json::serialize(turn);
    return result;
}

void readUsage(const HttpResponse& response, Trace::Call& call) {
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
    if (!parsed) return;
    const auto number = [](const json::Value* parent, const char* name) {
        const auto* value = parent ? parent->get(name) : nullptr;
        return value && value->type == json::Value::Type::number ? static_cast<int>(value->intOr()) : -1;
    };
    if (const auto* gemini = parsed->get("usageMetadata")) {
        call.prompt_tokens = number(gemini, "promptTokenCount");
        call.output_tokens = number(gemini, "candidatesTokenCount");
        call.thought_tokens = number(gemini, "thoughtsTokenCount");
    } else if (const auto* claude = parsed->get("usage")) { // Claude and the Responses API (Grok).
        call.prompt_tokens = number(claude, "input_tokens");
        call.output_tokens = number(claude, "output_tokens");
        if (const int reasoning = number(claude->get("output_tokens_details"), "reasoning_tokens"); reasoning >= 0) {
            call.thought_tokens = reasoning;
            call.output_tokens = std::max(0, call.output_tokens - reasoning);
        }
    } else {
        call.prompt_tokens = number(&*parsed, "prompt_eval_count");
        call.output_tokens = number(&*parsed, "eval_count");
    }
}

namespace {
// One call raced against its copies (hedged()).
HttpResponse race(const Post& transport, const std::wstring& url, const std::wstring& headers, const std::string& body,
                  const std::vector<uint32_t>& after_ms, int& copies, int& answered_by) {
    struct Race {
        std::mutex mutex;
        std::condition_variable changed;
        std::optional<HttpResponse> answer, failure;
        int answered_by = 0, running = 0;
        std::atomic_bool stop{false};
    };
    const auto shared = std::make_shared<Race>();
    // Each attempt owns its copy of the request: one left behind may finish after this call returns.
    const auto job = std::make_shared<const std::tuple<Post, std::wstring, std::wstring, std::string>>(transport, url, headers, body);
    int launched = 0;
    const auto launch = [&] {
        const int index = launched++;
        { std::lock_guard lock(shared->mutex); ++shared->running; }
        std::thread([shared, job, index] {
            HttpCancellation cancellation(shared->stop);
            const auto& [send, address, head, payload] = *job;
            auto response = send(address, head, payload);
            std::lock_guard lock(shared->mutex);
            --shared->running;
            if (response.status == 200) { if (!shared->answer) { shared->answer = std::move(response); shared->answered_by = index; } }
            else shared->failure = std::move(response);
            shared->changed.notify_all();
        }).detach();
    };
    launch();
    const auto started = GetTickCount64();
    std::unique_lock lock(shared->mutex);
    while (!shared->answer && !(shared->running == 0 && shared->failure)) {
        if (httpCancelled()) { shared->stop = true; copies = launched - 1; return {}; }
        const auto elapsed = GetTickCount64() - started;
        const auto next = static_cast<size_t>(launched - 1);
        if (next < after_ms.size() && elapsed >= after_ms[next]) { lock.unlock(); launch(); lock.lock(); continue; }
        const uint64_t until_copy = next < after_ms.size() ? after_ms[next] - elapsed : 50;
        shared->changed.wait_for(lock, std::chrono::milliseconds(std::min<uint64_t>(50, until_copy)));
    }
    shared->stop = true; // The others are no longer needed.
    copies = launched - 1;
    answered_by = shared->answered_by;
    return shared->answer ? std::move(*shared->answer) : std::move(*shared->failure);
}
} // namespace

Post hedged(Post transport, std::vector<uint32_t> after_ms, std::shared_ptr<HedgeLog> log) {
    return [transport = std::move(transport), after_ms = std::move(after_ms), log = std::move(log)](
               const std::wstring& url, const std::wstring& headers, const std::string& body) {
        if (body.empty() || after_ms.empty()) return transport(url, headers, body); // Model lists go once.
        int copies = 0, answered_by = 0;
        auto response = race(transport, url, headers, body, after_ms, copies, answered_by);
        if (log) { std::lock_guard lock(log->mutex); log->calls.emplace_back(copies, answered_by); }
        return response;
    };
}

HttpResponse post(const std::wstring& url, const std::wstring& headers, const std::string& body) {
    // A server on this machine or the home network (Ollama) may take its time loading a model.
    if (url.starts_with(L"http://")) return httpService(url, headers, body.empty() ? nullptr : &body, 2 * 1024 * 1024, 120000);
    if (body.empty()) return httpGetPrivate(url, headers, 1024 * 1024);
    return httpPostLong(url, headers, body, 2 * 1024 * 1024, 60000);
}

HttpResponse postStream(const std::wstring& url, const std::wstring& headers, const std::string& body, const Stream::Piece& piece) {
    return httpPostStream(url, headers, body, 2 * 1024 * 1024, 60000, [&](const uint8_t* data, size_t size) {
        if (piece) piece(std::string_view(reinterpret_cast<const char*>(data), size));
    });
}

std::vector<std::string> EventReader::feed(std::string_view bytes) {
    for (const char ch : bytes) if (ch != '\r') buffer_ += ch;
    std::vector<std::string> events;
    for (auto end = buffer_.find("\n\n"); end != std::string::npos; end = buffer_.find("\n\n")) {
        const auto block = buffer_.substr(0, end);
        buffer_.erase(0, end + 2);
        std::string data;
        for (size_t start = 0; start <= block.size();) {
            auto stop = block.find('\n', start);
            if (stop == std::string::npos) stop = block.size();
            std::string_view line(block.data() + start, stop - start);
            if (line.starts_with("data:")) {
                line.remove_prefix(5);
                if (line.starts_with(" ")) line.remove_prefix(1);
                if (!data.empty()) data += '\n';
                data += line;
            }
            start = stop + 1;
        }
        if (!data.empty() && data != "[DONE]") events.push_back(std::move(data));
    }
    return events;
}

std::vector<std::string> EventReader::finish() {
    if (buffer_.empty()) return {};
    buffer_ += "\n\n";
    return feed({});
}

std::wstring streamWords(std::string_view chunk) {
    const auto root = json::parse(chunk);
    const auto* candidates = root ? root->get("candidates") : nullptr;
    if (!candidates || candidates->type != json::Value::Type::array || candidates->elements.empty()) return {};
    const auto* content = candidates->elements.front().get("content");
    const auto* parts = content ? content->get("parts") : nullptr;
    if (!parts || parts->type != json::Value::Type::array) return {};
    std::wstring words;
    for (const auto& part : parts->elements) {
        if (const auto* thought = part.get("thought"); thought && thought->flag) continue;
        if (part.get("functionCall")) continue;
        if (const auto* text = part.get("text"); text && text->type == json::Value::Type::string) words += text_encoding::wideLenient(text->text);
    }
    return words;
}

std::string joinStream(const std::vector<std::string>& chunks) {
    auto parts = json::array();
    std::string finish;
    std::optional<json::Value> usage;
    for (const auto& chunk : chunks) {
        const auto root = json::parse(chunk);
        if (!root || root->type != json::Value::Type::object) continue;
        if (const auto* metadata = root->get("usageMetadata")) usage = *metadata;
        const auto* candidates = root->get("candidates");
        if (!candidates || candidates->type != json::Value::Type::array || candidates->elements.empty()) continue;
        const auto& first = candidates->elements.front();
        if (const auto* reason = first.get("finishReason"); reason && !reason->stringOr().empty()) finish = reason->stringOr();
        const auto* content = first.get("content");
        const auto* list = content ? content->get("parts") : nullptr;
        if (!list || list->type != json::Value::Type::array) continue;
        for (const auto& part : list->elements) {
            if (const auto* thought = part.get("thought"); thought && thought->flag) continue;
            const auto* text = part.get("text");
            // Calls (with their thought signatures) and anything but words stay as they came.
            if (part.get("functionCall") || !text || text->type != json::Value::Type::string) { parts.elements.push_back(part); continue; }
            auto* last = parts.elements.empty() ? nullptr : &parts.elements.back();
            const bool words = last && !last->get("functionCall") && last->get("text") && last->get("text")->type == json::Value::Type::string;
            if (!words) { parts.elements.push_back(part); continue; }
            last->properties["text"].text += text->text;
            if (const auto* signature = part.get("thoughtSignature")) last->properties["thoughtSignature"] = *signature;
        }
    }
    auto content = json::object();
    content.properties["role"] = json::string("model");
    content.properties["parts"] = std::move(parts);
    auto candidate = json::object();
    candidate.properties["content"] = std::move(content);
    if (!finish.empty()) candidate.properties["finishReason"] = json::string(finish);
    auto candidates = json::array();
    candidates.elements.push_back(std::move(candidate));
    auto root = json::object();
    root.properties["candidates"] = std::move(candidates);
    if (usage) root.properties["usageMetadata"] = std::move(*usage);
    return json::serialize(root);
}

namespace {
// Streaming answers that a proxy refuses (it does not know streamGenerateContent) stop being tried.
std::atomic_bool stream_refused{false};
// One streamed call: her words go to stream.text as they come; the response is the whole of it,
// joined as generateContent would have answered (a transient failure before any word is tried once more).
HttpResponse sendStream(const Connection& connection, const std::string& body, const Stream& stream, Trace* trace, bool& spoke) {
    auto url = connection.endpoint();
    const std::wstring_view whole = L":generateContent";
    const auto at = url.rfind(whole);
    if (at == std::wstring::npos) return {404, {}};
    url.replace(at, whole.size(), L":streamGenerateContent?alt=sse");
    const auto headers = connection.headers();
    HttpResponse response;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto started = GetTickCount64();
        EventReader reader;
        std::vector<std::string> chunks;
        size_t received = 0;
        const auto take = [&](std::vector<std::string> events) {
            for (auto& event : events) {
                const auto words = streamWords(event);
                if (!words.empty()) { spoke = true; if (stream.text) stream.text(words); }
                chunks.push_back(std::move(event));
            }
        };
        response = stream.transport(url, headers, body, [&](std::string_view bytes) { received += bytes.size(); take(reader.feed(bytes)); });
        take(reader.finish());
        if (response.status == 200 && chunks.empty() && !response.body.empty()) {
            // Not server-sent events after all (a proxy that drops alt=sse): a JSON array of chunks, or one whole response.
            const std::string_view raw(reinterpret_cast<const char*>(response.body.data()), response.body.size());
            if (const auto parsed = json::parse(raw); parsed && parsed->type == json::Value::Type::array) {
                std::vector<std::string> elements;
                for (const auto& element : parsed->elements) elements.push_back(json::serialize(element));
                take(std::move(elements));
            } else if (parsed) take({std::string(raw)});
        }
        if (response.status == 200) {
            const auto joined = joinStream(chunks);
            response.body.assign(joined.begin(), joined.end());
        }
        if (trace) {
            Trace::Call call;
            call.ms = static_cast<uint32_t>(GetTickCount64() - started);
            call.status = response.status; call.sent = body.size(); call.received = received;
            if (response.status == 200) readUsage(response, call);
            trace->calls.push_back(call);
        }
        const bool transient = response.status == 0 || response.status == 429 || response.status == 500 || response.status == 502 ||
                               response.status == 503 || response.status == 504;
        if (!transient || spoke || attempt || httpCancelled()) break;
        for (int wait = 0; wait < 12 && !httpCancelled(); ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return response;
}
} // namespace

Reply ask(const Connection& given, const std::wstring& system, const std::vector<Turn>& turns, const Post& transport, int max_tokens, float temperature,
          const Tools* tools, Trace* trace, const Stream* stream) {
    if (!given.ready()) { Reply missing; missing.error = L"no connection"; return missing; }
    // Claude's family and Ollama's empty name become a model this account or server has.
    Connection connection = given;
    connection.model = wire::chooseModel(given, transport);
    if (connection.model.empty()) { Reply missing; missing.error = L"no model"; return missing; }
    // Gemini Flash answers at once without thinking; Pro models only work in thinking mode. Flash
    // still thinks briefly now and then although asked not to, and its thoughts count against
    // the token limit: the limit keeps room for them, so the reply itself is not cut off.
    // Grok always reasons (at low effort here) and its reasoning counts against the limit too.
    const auto dialect = connection.dialect();
    const bool gemini = dialect == Dialect::gemini;
    const bool thinking = gemini && connection.model.find("pro") != std::string::npos;
    int limit = dialect == Dialect::responses ? max_tokens + thought_room : !gemini ? max_tokens + 200 : thinking ? std::max(max_tokens, 2048) : max_tokens + thought_room;
    const bool tooled = tools && !tools->declarations.empty() && tools->run;
    wire::Request request;
    // Her texts speak of the person she talks with in both genders (‹on|ona›): one, as he or she is.
    const auto who = person();
    request.system = personalize(system, who);
    request.turns = turns;
    for (auto& turn : request.turns) turn.text = personalize(turn.text, who);
    request.temperature = temperature;
    request.tools = tooled ? tools : nullptr;
    request.no_thinking = gemini && !thinking;
    std::vector<std::string> used;
    std::vector<std::string> tried{connection.model}; // Models Google refused with 503 so far (overloaded): the next Flash is asked.
    int rounds = 0;
    bool retried = false, words = false, spoke = false;
    Reply reply;
    for (int attempt = 0; attempt < 8; ++attempt) {
        // After the last round of calls she answers in words.
        request.tools_off = tooled && (rounds >= tools->rounds || words);
        request.limit = limit;
        const auto body = wire::body(connection, request);
        HttpResponse response;
        if (stream && stream->transport && gemini && !stream_refused.load()) {
            response = sendStream(connection, body, *stream, trace, spoke);
            // A proxy that does not stream: whole answers from now on.
            if (!spoke && (response.status == 400 || response.status == 404 || response.status == 405 || response.status == 501)) {
                stream_refused = true;
                response = send(connection, body, transport, trace);
            }
        } else response = send(connection, body, transport, trace);
        // Google's "high demand" 503 stays on one model for a while and passes on another: try the other Flash models
        // (never a Pro, which thinks) before giving up. Only a direct Google key; Foundry has its own proxy.
        if (response.status == 503 && connection.provider == Provider::gemini && !spoke && !httpCancelled()) {
            const ModelInfo* next = nullptr;
            for (const auto& candidate : models) {
                if (std::string_view(candidate.id).find("pro") != std::string_view::npos) continue;
                if (std::find(tried.begin(), tried.end(), candidate.id) == tried.end()) { next = &candidate; break; }
            }
            if (next) { connection.model = next->id; tried.push_back(next->id); continue; }
        }
        if (auto calls = tooled && !request.tools_off ? wire::calls(dialect, response) : std::nullopt) {
            // Her calls, verbatim (Gemini requires its thought signatures back), then their answers.
            std::vector<json::Value> results;
            for (const auto& call : calls->calls) {
                const auto started = GetTickCount64();
                results.push_back(tools->run(call.name, call.args));
                if (trace) trace->tools.push_back({call.name, static_cast<uint32_t>(GetTickCount64() - started)});
                used.push_back(call.name);
            }
            auto answered = wire::answers(dialect, *calls, std::move(results));
            request.contents.push_back(std::move(calls->content));
            for (auto& message : answered) request.contents.push_back(std::move(message));
            ++rounds;
            // Words before her calls ("let me check…") are not her answer: it starts over.
            if (spoke && stream && stream->restart) stream->restart();
            spoke = false;
            if (httpCancelled()) break;
            continue;
        }
        reply = wire::reply(dialect, response);
        if (reply.ok || httpCancelled()) break;
        // After her tools the model now and then ends with no words at all: ask once more for them.
        if (!reply.truncated && rounds && !words && !spoke && response.status == 200) { words = true; continue; }
        // Cut before a single sentence was complete: once more with a far larger limit (not when
        // her words are already on screen: they would come twice).
        if (!reply.truncated || retried || spoke) break;
        retried = true;
        limit = std::min(8192, limit * 4);
    }
    reply.tools = std::move(used);
    return reply;
}

HttpResponse send(const Connection& connection, const std::string& body, const Post& transport, Trace* trace) {
    const auto headers = connection.headers();
    HttpResponse response;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto started = GetTickCount64();
        response = transport(connection.endpoint(), headers, body);
        if (trace) {
            Trace::Call call;
            call.ms = static_cast<uint32_t>(GetTickCount64() - started);
            call.status = response.status; call.sent = body.size(); call.received = response.body.size();
            readUsage(response, call);
            trace->calls.push_back(call);
        }
        const bool transient = response.status == 0 || response.status == 429 || response.status == 500 || response.status == 502 ||
                               response.status == 503 || response.status == 504;
        if (!transient || attempt || httpCancelled()) break;
        for (int wait = 0; wait < 12 && !httpCancelled(); ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return response;
}
} // namespace companion
