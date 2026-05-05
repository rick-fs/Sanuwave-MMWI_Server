// PathUtils.h
#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <string>

class PathUtils
{
public:
    /**
     * @brief Create a directory recursively
     * @param path The directory path to create
     * @return true if directory exists or was created successfully, false otherwise
     */
    static bool createDirectory(const char* path);
    
    /**
     * @brief Create a directory recursively
     * @param path The directory path to create
     * @return true if directory exists or was created successfully, false otherwise
     */
    static bool createDirectory(const std::string& path);
    
    /**
     * @brief Get the user's home directory
     * @return The home directory path, or "/tmp" as fallback
     */
    static std::string getHomeDirectory();
    
    /**
     * @brief Get an appropriate log directory for the application
     * @param appName The application name (used in fallback path)
     * @param preferredPath The preferred system log path (e.g., "/var/log/sanuwave")
     * @return A valid writable log directory path
     */
    static std::string getLogDirectory(const std::string& appName, 
                                       const std::string& preferredPath);
    
    /**
     * @brief Check if a directory exists
     * @param path The directory path to check
     * @return true if the path exists and is a directory
     */
    static bool directoryExists(const char* path);
    
    /**
     * @brief Check if a directory exists
     * @param path The directory path to check
     * @return true if the path exists and is a directory
     */
    static bool directoryExists(const std::string& path);

private:
    PathUtils() = default;  // Prevent instantiation
};

#endif // PATHUTILS_H
