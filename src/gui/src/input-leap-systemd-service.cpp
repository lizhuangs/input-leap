#include "LinuxPackagedAutostart.h"

#include <QCoreApplication>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const auto args = app.arguments();
    if (args.size() > 1 && args[1] == "--has-config") {
        const auto settings_path = inputleap::packaged_autostart::find_settings_file(
            inputleap::packaged_autostart::default_settings_candidates());
        QString error;
        return inputleap::packaged_autostart::load_settings(settings_path, &error).has_value() ? 0
                                                                                               : 1;
    }

    return inputleap::packaged_autostart::run_service();
}
