// Opt-in portal role configuration. Writes only the user's XDG portals.conf.
#pragma once

#include <QString>

class PortalConfig
{
public:
    static bool enableFileChooser(QString *error);
    static bool disableFileChooser(QString *error);
    static QString status();
};
