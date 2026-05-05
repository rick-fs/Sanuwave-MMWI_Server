// include/command_router.h
#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <map>
#include <vector>

namespace sanuwave
{

/// Parses simple flat JSON into key-value pairs
std::map<std::string, std::string> parseSimpleJSON(const std::string& json);

/// Command handler function signature
using CommandFunc = std::function<std::string(const std::map<std::string, std::string>&)>;

/// Routes incoming JSON commands to appropriate handler functions
class CommandRouter
{
public:
    CommandRouter() = default;
    
    void registerCommand(const std::string& command, CommandFunc handler);
    std::string route(const std::string& jsonCommand);
    bool hasCommand(const std::string& command) const;
    std::vector<std::string> getRegisteredCommands() const;

private:
    std::unordered_map<std::string, CommandFunc> handlers_;
    std::string buildJsonError(const std::string& message);
};


} // namespace sanuwave
