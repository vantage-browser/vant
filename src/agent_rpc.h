#ifndef VANTAGE_AGENT_RPC_H
#define VANTAGE_AGENT_RPC_H
#include <functional>
#include <memory>
#include <mutex>
#include <deque>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace vantage {
struct AgentRequest { std::string id; std::string method; std::string params_json{"{}"}; };
using AgentReply = std::function<void(std::string)>;
using AgentHandler = std::function<void(AgentRequest, AgentReply)>;
std::string agent_socket_path();
std::string json_escape(std::string_view value);
std::string json_string(std::string_view value);
std::string json_param_string(std::string_view json, std::string_view key);
long long json_param_integer(std::string_view json, std::string_view key, long long fallback = 0);
bool json_param_bool(std::string_view json, std::string_view key, bool fallback = false);
std::string agent_ok(std::string_view id, std::string_view result_json = "null");
std::string agent_error(std::string_view id, std::string_view code, std::string_view message);

struct AgentEvent { std::uint64_t sequence{}; std::string type; std::string payload_json{"{}"}; };
class AgentEventLog {
public:
    explicit AgentEventLog(std::size_t capacity = 1024);
    std::uint64_t publish(std::string type, std::string payload_json = "{}");
    std::vector<AgentEvent> since(std::uint64_t after, std::size_t limit = 256) const;
    std::uint64_t latest() const;
    std::uint64_t dropped() const;
    void clear();
private:
    mutable std::mutex mutex_;
    std::deque<AgentEvent> events_;
    std::size_t capacity_;
    std::uint64_t next_{1};
    std::uint64_t dropped_{};
};

class AgentRpcServer {
public:
    explicit AgentRpcServer(AgentHandler handler);
    ~AgentRpcServer();
    AgentRpcServer(const AgentRpcServer&) = delete;
    AgentRpcServer& operator=(const AgentRpcServer&) = delete;
    bool start(std::string *error = nullptr);
    void stop();
    const std::string &path() const noexcept;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
int run_agent_cli(const std::vector<std::string_view> &arguments);
}
#endif
