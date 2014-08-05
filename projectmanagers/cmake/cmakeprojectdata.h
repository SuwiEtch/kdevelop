#ifndef CMAKEPROJECTDATA_H
#define CMAKEPROJECTDATA_H

#include <QStringList>
#include <QFileSystemWatcher>
#include "cmaketypes.h"
#include <util/path.h>

/**
 * Represents any file in a cmake project that has been added
 * to the project.
 *
 * Contains the required information to compile it properly
 */
struct CMakeFile
{
    KDevelop::Path::List includes;
    QHash<QString, QString> defines;
};
inline QDebug &operator<<(QDebug debug, const CMakeFile& file)
{
    debug << "CMakeFile(-I" << file.includes << ", -D" << file.defines << ")";
    return debug.maybeSpace();
}

struct CMakeProjectData
{
    CMakeProjectData() : watcher(new QFileSystemWatcher) {}
    ~CMakeProjectData() {}

    CMakeProperties properties;
    CacheValues cache;
    QHash<KDevelop::Path, CMakeFile> files;
    QSharedPointer<QFileSystemWatcher> watcher;
};

#endif
