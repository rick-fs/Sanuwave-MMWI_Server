#include "path_utils.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <cstring>

bool PathUtils::directoryExists(const char* path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

bool PathUtils::directoryExists(const std::string& path)
{
    return directoryExists(path.c_str());
}

bool PathUtils::createDirectory(const char* path)
{
    // Check if directory already exists
    if (directoryExists(path)) {
        return true;
    }
    
    // Try to create the directory with rwxr-xr-x permissions
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    
    // If failed due to missing parent directories, create them recursively
    if (errno == ENOENT) {
        std::string pathStr(path);
        size_t pos = pathStr.find_last_of('/');
        
        // If there's a parent directory
        if (pos != std::string::npos && pos > 0) {
            std::string parent = pathStr.substr(0, pos);
            
            // Recursively create parent directory
            if (createDirectory(parent.c_str())) {
                // Try again to create this directory
                return mkdir(path, 0755) == 0;
            }
        }
    }
    
    return false;
}

bool PathUtils::createDirectory(const std::string& path)
{
    return createDirectory(path.c_str());
}

std::string PathUtils::getHomeDirectory()
{
    // Try HOME environment variable first
    const char* homeDir = getenv("HOME");
    if (homeDir && homeDir[0] != '\0') {
        return std::string(homeDir);
    }
    
    // Try to get home directory from password database
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return std::string(pw->pw_dir);
    }
    
    // Fallback to /tmp
    return "/tmp";
}

std::string PathUtils::getLogDirectory(const std::string& appName, 
                                       const std::string& preferredPath)
{
    // Try preferred system log path first (e.g., /var/log/sanuwave)
    if (createDirectory(preferredPath)) {
        return preferredPath;
    }
    
    // Fallback to user home directory
    std::string fallbackPath = getHomeDirectory() + "/" + appName + "/logs";
    if (createDirectory(fallbackPath)) {
        return fallbackPath;
    }
    
    // Last resort: use /tmp
    std::string tmpPath = "/tmp/" + appName + "/logs";
    createDirectory(tmpPath);
    return tmpPath;
}
