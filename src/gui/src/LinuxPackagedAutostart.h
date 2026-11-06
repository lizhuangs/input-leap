#pragma once

#include <QString>
#include <QStringList>

#include <optional>

class QSettings;

namespace inputleap::packaged_autostart {

enum class Role {
    Client,
    Server
};

struct ResolvedSettings {
    QString settings_path;
    Role role;
    QString screen_name;
    int port = 24800;
    QString interface_name;
    bool crypto_enabled = true;
    bool require_client_certificate = false;
    bool log_to_file = false;
    QString log_filename;
    QString log_level;
    bool use_internal_config = true;
    QString external_config_path;
    QString server_hostname;
    QString generated_server_config;
};

struct ResolvedCommand {
    QString program;
    QStringList arguments;
    QString xauthority;
};

QString find_settings_file(const QStringList& candidates);
QStringList default_settings_candidates();
std::optional<ResolvedSettings> load_settings(const QString& settings_path, QString* error);
std::optional<ResolvedCommand> build_command(const ResolvedSettings& settings, QString* error);

QString detect_display();
QString detect_xauthority(const QString& display);
int run_service();

} // namespace inputleap::packaged_autostart
