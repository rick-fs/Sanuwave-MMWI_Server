// src/command_router.cpp
#include "command_router.h"
#include "logger.h"
#include <algorithm>

namespace sanuwave
{

std::map<std::string, std::string> parseSimpleJSON(const std::string& json)
{
    std::map<std::string, std::string> result;
    size_t pos = 0;
    while (pos < json.length())
    {
        // Find next key
        size_t keyStart = json.find('"', pos);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = json.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        std::string key = json.substr(keyStart + 1, keyEnd - keyStart - 1);

        size_t colonPos = json.find(':', keyEnd);
        if (colonPos == std::string::npos) break;

        size_t valueStart = colonPos + 1;
        while (valueStart < json.length() &&
               (json[valueStart] == ' ' || json[valueStart] == '\t'))
            valueStart++;
        if (valueStart >= json.length()) break;

        std::string value;
        size_t valueEnd;

        if (json[valueStart] == '"')
        {
            // Quoted string value
            valueEnd = json.find('"', valueStart + 1);
            if (valueEnd == std::string::npos) break;
            value = json.substr(valueStart + 1, valueEnd - valueStart - 1);
            valueEnd++;
        }
        else if (json[valueStart] == '[')
        {
            // Array value — capture everything up to the matching ']'
            size_t depth = 0;
            valueEnd = valueStart;
            while (valueEnd < json.length())
            {
                if      (json[valueEnd] == '[') ++depth;
                else if (json[valueEnd] == ']') { if (--depth == 0) { ++valueEnd; break; } }
                ++valueEnd;
            }
            value = json.substr(valueStart, valueEnd - valueStart);
        }
        else
        {
            // Bare scalar (number, bool, null)
            valueEnd = valueStart;
            while (valueEnd < json.length() &&
                   json[valueEnd] != ',' && json[valueEnd] != '}' &&
                   json[valueEnd] != ' ' && json[valueEnd] != '\t' &&
                   json[valueEnd] != '\n' && json[valueEnd] != '\r')
                valueEnd++;
            value = json.substr(valueStart, valueEnd - valueStart);
        }

        result[key] = value;
        pos = valueEnd;
    }
    return result;
}

void CommandRouter::registerCommand(const std::string& command, CommandFunc handler)
{
    handlers_[command] = std::move(handler);
    LOG_TRACE << "Registered command: " << command << std::endl;
}

std::string CommandRouter::route(const std::string& jsonCommand)
{
    LOG_TRACE << "Routing command: " << jsonCommand << std::endl;

    auto params = parseSimpleJSON(jsonCommand);
    auto it = params.find("command");

    if (it == params.end() || it->second.empty())
        return buildJsonError("No command specified");

    const std::string& command = it->second;
    auto handlerIt = handlers_.find(command);

    if (handlerIt == handlers_.end())
        return buildJsonError("Unknown command: " + command);

    try
    {
        return handlerIt->second(params);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Command '" << command << "' threw exception: " << e.what() << std::endl;
        return buildJsonError(std::string("Command failed: ") + e.what());
    }
}

bool CommandRouter::hasCommand(const std::string& command) const
{
    return handlers_.find(command) != handlers_.end();
}

std::vector<std::string> CommandRouter::getRegisteredCommands() const
{
    std::vector<std::string> commands;
    commands.reserve(handlers_.size());
    for (const auto& [cmd, _] : handlers_)
        commands.push_back(cmd);
    std::sort(commands.begin(), commands.end());
    return commands;
}

std::string CommandRouter::buildJsonError(const std::string& message)
{
    return R"({"type":"error","message":")" + message + R"("})";
}

} // namespace sanuwave
