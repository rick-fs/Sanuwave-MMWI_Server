// version.h
#ifndef VERSION_H
#define VERSION_H

#include <QString>

// These are defined by CMake via target_compile_definitions()
// VERSION_STRING    - Full version: "v1.0.0.123.abcdef0" or "v1.0.0.123.abcdef0-dirty"
// VERSION_DISPLAY   - Display version: "v1.0.0 (build 123)"
// GIT_HASH          - Short git hash: "abcdef0"
// GIT_HASH_FULL     - Full git hash: "abcdef0123456789..."
// GIT_COMMIT_COUNT  - Total commits: 123
// GIT_BRANCH        - Branch name: "main"

// Provide defaults if not defined (e.g., when building without CMake)
#ifndef VERSION_STRING
#define VERSION_STRING "v0.0.0.0.unknown"
#endif

#ifndef VERSION_DISPLAY
#define VERSION_DISPLAY "v0.0.0 (build 0)"
#endif

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

#ifndef GIT_HASH_FULL
#define GIT_HASH_FULL "unknown"
#endif

#ifndef GIT_COMMIT_COUNT
#define GIT_COMMIT_COUNT 0
#endif

#ifndef GIT_BRANCH
#define GIT_BRANCH "unknown"
#endif

namespace Version
{
    // Full version string: "v1.0.0.123.abcdef0-dirty"
    inline QString fullVersion() 
    { 
        return QStringLiteral(VERSION_STRING); 
    }
    
    // Display version: "v1.0.0 (build 123)"
    inline QString displayVersion() 
    { 
        return QStringLiteral(VERSION_DISPLAY); 
    }
    
    // Short git hash: "abcdef0"
    inline QString gitHash() 
    { 
        return QStringLiteral(GIT_HASH); 
    }
    
    // Full git hash
    inline QString gitHashFull() 
    { 
        return QStringLiteral(GIT_HASH_FULL); 
    }
    
    // Total commit count
    inline int commitCount() 
    { 
        return GIT_COMMIT_COUNT; 
    }
    
    // Branch name
    inline QString branch() 
    { 
        return QStringLiteral(GIT_BRANCH); 
    }
    
    // Build info string for About dialog
    inline QString buildInfo()
    {
        return QString("Version: %1\n"
                       "Build: %2\n"
                       "Branch: %3\n"
                       "Commit: %4")
            .arg(QStringLiteral(VERSION_STRING))
            .arg(GIT_COMMIT_COUNT)
            .arg(QStringLiteral(GIT_BRANCH))
            .arg(QStringLiteral(GIT_HASH));
    }
    
    // Window title with version
    inline QString windowTitle(const QString& appName)
    {
        return QString("%1 - %2").arg(appName, QStringLiteral(VERSION_DISPLAY));
    }
}

#endif // VERSION_H
