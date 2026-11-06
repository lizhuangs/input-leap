#include "LinuxPackagedAutostart.h"

#include "Action.h"
#include "BaseConfig.h"
#include "Hotkey.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTextStream>

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <vector>

namespace inputleap::packaged_autostart {
namespace {

constexpr int kModifierCount = 6;
constexpr int kFixCount = 5;
constexpr int kSwitchCornerCount = 4;

struct ScreenConfig {
    QString name;
    QStringList aliases;
    std::array<int, kModifierCount> modifiers{};
    std::array<bool, kSwitchCornerCount> switch_corners{};
    int switch_corner_size = 0;
    std::array<bool, kFixCount> fixes{};
};

QString bool_string(bool value)
{
    return value ? "true" : "false";
}

QString normalize_host(const QString& host)
{
    if (host.isEmpty()) {
        return host;
    }

    if (host.startsWith('[') && host.endsWith(']')) {
        return host;
    }

    return "[" + host + "]";
}

QString program_dir()
{
    if (QCoreApplication::instance() != nullptr) {
        return QCoreApplication::applicationDirPath();
    }

    return QDir::currentPath();
}

QString log_level_name(int log_level)
{
    static const std::array<const char*, 7> kNames{
        "ERROR",
        "WARNING",
        "NOTE",
        "INFO",
        "DEBUG",
        "DEBUG1",
        "DEBUG2",
    };

    if (log_level < 0 || log_level >= static_cast<int>(kNames.size())) {
        return "INFO";
    }

    return QString::fromLatin1(kNames[log_level]);
}

void load_string_array(QSettings& settings, const QString& array_name, QStringList& values)
{
    const int entries = settings.beginReadArray(array_name + "Array");
    values.clear();
    for (int i = 0; i < entries; ++i) {
        settings.setArrayIndex(i);
        values.append(settings.value(array_name).toString());
    }
    settings.endArray();
}

template <typename T, std::size_t Size>
void load_fixed_array(QSettings& settings, const QString& array_name, const T& default_value,
                      std::array<T, Size>& values)
{
    settings.beginReadArray(array_name + "Array");
    for (int i = 0; i < Size; ++i) {
        settings.setArrayIndex(i);
        values[i] = settings.value(array_name, QVariant::fromValue(default_value)).template value<T>();
    }
    settings.endArray();
}

std::vector<ScreenConfig> load_screens(QSettings& settings, int* num_columns, int* num_rows)
{
    settings.beginGroup("internalConfig");
    *num_columns = settings.value("numColumns", 5).toInt();
    *num_rows = settings.value("numRows", 3).toInt();

    const int count = settings.beginReadArray("screens");
    std::vector<ScreenConfig> screens;
    screens.reserve(count);

    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        ScreenConfig screen;
        screen.name = settings.value("name").toString();
        if (!screen.name.isEmpty()) {
            screen.switch_corner_size = settings.value("switchCornerSize", 0).toInt();
            load_string_array(settings, "alias", screen.aliases);
            load_fixed_array<int>(settings, "modifier", -1, screen.modifiers);
            load_fixed_array<bool>(settings, "switchCorner", false, screen.switch_corners);
            load_fixed_array<bool>(settings, "fix", false, screen.fixes);
        }
        screens.push_back(std::move(screen));
    }

    settings.endArray();
    settings.endGroup();
    return screens;
}

QList<bool> load_global_switch_corners(QSettings& settings)
{
    settings.beginGroup("internalConfig");
    QList<bool> values;
    for (int i = 0; i < kSwitchCornerCount; ++i) {
        values.append(false);
    }
    settings.beginReadArray("switchCornerArray");
    for (int i = 0; i < kSwitchCornerCount; ++i) {
        settings.setArrayIndex(i);
        values[i] = settings.value("switchCorner", false).toBool();
    }
    settings.endArray();
    settings.endGroup();
    return values;
}

std::vector<Hotkey> load_hotkeys(QSettings& settings)
{
    settings.beginGroup("internalConfig");
    const int count = settings.beginReadArray("hotkeys");
    std::vector<Hotkey> hotkeys;
    hotkeys.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Hotkey hotkey;
        hotkey.loadSettings(settings);
        hotkeys.push_back(std::move(hotkey));
    }
    settings.endArray();
    settings.endGroup();
    return hotkeys;
}

int adjacent_screen_index(const std::vector<ScreenConfig>& screens, int num_columns, int idx,
                          int delta_column, int delta_row)
{
    if (idx < 0 || idx >= static_cast<int>(screens.size()) || screens[idx].name.isEmpty()) {
        return -1;
    }

    if ((delta_column > 0 && (idx + 1) % num_columns == 0) ||
        (delta_column < 0 && idx % num_columns == 0)) {
        return -1;
    }

    const int array_pos = idx + delta_column + delta_row * num_columns;
    if (array_pos < 0 || array_pos >= static_cast<int>(screens.size())) {
        return -1;
    }

    return array_pos;
}

QString render_server_config(QSettings& settings)
{
    int num_columns = 0;
    int num_rows = 0;
    const auto screens = load_screens(settings, &num_columns, &num_rows);
    const auto hotkeys = load_hotkeys(settings);

    settings.beginGroup("internalConfig");
    const bool has_heartbeat = settings.value("hasHeartbeat", false).toBool();
    const int heartbeat = settings.value("heartbeat", 5000).toInt();
    const bool relative_mouse_moves = settings.value("relativeMouseMoves", false).toBool();
    const bool screen_saver_sync = settings.value("screenSaverSync", true).toBool();
    const bool win32_keep_foreground = settings.value("win32KeepForeground", false).toBool();
    const bool has_switch_delay = settings.value("hasSwitchDelay", false).toBool();
    const int switch_delay = settings.value("switchDelay", 250).toInt();
    const bool has_switch_double_tap = settings.value("hasSwitchDoubleTap", false).toBool();
    const int switch_double_tap = settings.value("switchDoubleTap", 250).toInt();
    const int switch_corner_size = settings.value("switchCornerSize", 0).toInt();
    const bool clipboard_sharing = settings.value("clipboardSharing", true).toBool();
    const auto clipboard_sharing_size =
        settings.value("clipboardSharingSize", 1024 * 1024).toULongLong();
    settings.endGroup();

    const auto global_switch_corners = load_global_switch_corners(settings);

    QString output;
    QTextStream stream(&output);

    stream << "section: screens\n";
    for (const auto& screen : screens) {
        if (screen.name.isEmpty()) {
            continue;
        }

        stream << "\t" << screen.name << ":\n";
        for (int i = 0; i < kModifierCount; ++i) {
            if (screen.modifiers[i] != i && screen.modifiers[i] >= 0 &&
                screen.modifiers[i] < kModifierCount) {
                stream << "\t\t" << BaseConfig::modifierName(static_cast<BaseConfig::Modifier>(i))
                       << " = "
                       << BaseConfig::modifierName(
                              static_cast<BaseConfig::Modifier>(screen.modifiers[i]))
                       << "\n";
            }
        }

        for (int i = 0; i < kFixCount; ++i) {
            stream << "\t\t" << BaseConfig::fixName(static_cast<BaseConfig::Fix>(i)) << " = "
                   << bool_string(screen.fixes[i]) << "\n";
        }

        stream << "\t\t" << "switchCorners = none ";
        for (int i = 0; i < kSwitchCornerCount; ++i) {
            if (screen.switch_corners[i]) {
                stream << "+" << BaseConfig::switchCornerName(
                                        static_cast<BaseConfig::SwitchCorner>(i))
                       << " ";
            }
        }
        stream << "\n";
        stream << "\t\t" << "switchCornerSize = " << screen.switch_corner_size << "\n";
    }
    stream << "end\n\n";

    stream << "section: aliases\n";
    for (const auto& screen : screens) {
        if (screen.name.isEmpty() || screen.aliases.isEmpty()) {
            continue;
        }

        stream << "\t" << screen.name << ":\n";
        for (const auto& alias : screen.aliases) {
            stream << "\t\t" << alias << "\n";
        }
    }
    stream << "end\n\n";

    stream << "section: links\n";
    static const std::array<std::pair<int, int>, 4> kDirections{{
        {1, 0},
        {-1, 0},
        {0, -1},
        {0, 1},
    }};
    static const std::array<const char*, 4> kDirectionNames{{"right", "left", "up", "down"}};
    for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
        if (screens[i].name.isEmpty()) {
            continue;
        }

        stream << "\t" << screens[i].name << ":\n";
        for (int direction = 0; direction < static_cast<int>(kDirections.size()); ++direction) {
            const int adjacent = adjacent_screen_index(
                screens, num_columns, i, kDirections[direction].first, kDirections[direction].second);
            if (adjacent != -1 && !screens[adjacent].name.isEmpty()) {
                stream << "\t\t" << kDirectionNames[direction] << " = "
                       << screens[adjacent].name << "\n";
            }
        }
    }
    stream << "end\n\n";

    stream << "section: options\n";
    if (has_heartbeat) {
        stream << "\theartbeat = " << heartbeat << "\n";
    }
    stream << "\trelativeMouseMoves = " << bool_string(relative_mouse_moves) << "\n";
    stream << "\tscreenSaverSync = " << bool_string(screen_saver_sync) << "\n";
    stream << "\twin32KeepForeground = " << bool_string(win32_keep_foreground) << "\n";
    stream << "\tclipboardSharing = " << bool_string(clipboard_sharing) << "\n";
    stream << "\tclipboardSharingSize = " << clipboard_sharing_size << "\n";
    if (has_switch_delay) {
        stream << "\tswitchDelay = " << switch_delay << "\n";
    }
    if (has_switch_double_tap) {
        stream << "\tswitchDoubleTap = " << switch_double_tap << "\n";
    }
    stream << "\tswitchCorners = none ";
    for (int i = 0; i < global_switch_corners.size(); ++i) {
        if (global_switch_corners[i]) {
            stream << "+" << BaseConfig::switchCornerName(
                                    static_cast<BaseConfig::SwitchCorner>(i))
                   << " ";
        }
    }
    stream << "\n";
    stream << "\tswitchCornerSize = " << switch_corner_size << "\n";
    for (const auto& hotkey : hotkeys) {
        stream << hotkey;
    }
    stream << "end\n\n";

    return output;
}

QString generated_server_config_path()
{
    return QStringLiteral("/run/input-leap/input-leap.conf");
}

bool persist_server_config(const QString& content, QString* error)
{
    QDir().mkpath(QStringLiteral("/run/input-leap"));

    QFile file(generated_server_config_path());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot write generated server config %1")
                         .arg(generated_server_config_path());
        }
        return false;
    }

    QTextStream stream(&file);
    stream << content;
    file.close();
    return true;
}

QString resolve_settings_override()
{
    return qEnvironmentVariable("INPUTLEAP_SETTINGS_PATH");
}

} // namespace

QStringList default_settings_candidates()
{
    QStringList candidates;

    const auto override = resolve_settings_override();
    if (!override.isEmpty()) {
        candidates.append(override);
        return candidates;
    }

    const QStringList base_dirs{QStringLiteral("/root"), QStringLiteral("/home")};
    for (const auto& base_dir : base_dirs) {
        QFileInfo base_info(base_dir);
        if (!base_info.exists() || !base_info.isDir()) {
            continue;
        }

        if (base_dir == QStringLiteral("/root")) {
            candidates.append(base_dir + QStringLiteral("/.config/InputLeap/InputLeap.conf"));
            candidates.append(base_dir + QStringLiteral("/.config/Debauchee/Barrier.conf"));
            continue;
        }

        const QDir home_dir(base_dir);
        for (const auto& entry :
             home_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable)) {
            candidates.append(entry.filePath() + QStringLiteral("/.config/InputLeap/InputLeap.conf"));
            candidates.append(entry.filePath() + QStringLiteral("/.config/Debauchee/Barrier.conf"));
        }
    }

    return candidates;
}

QString find_settings_file(const QStringList& candidates)
{
    QFileInfo newest_info;
    QString newest_path;

    for (const auto& candidate : candidates) {
        QFileInfo info(candidate);
        if (!info.isFile() || !info.isReadable()) {
            continue;
        }

        QSettings settings(candidate, QSettings::IniFormat);
        if (!settings.value("autoStart", false).toBool()) {
            continue;
        }

        if (newest_path.isEmpty() || info.lastModified() > newest_info.lastModified()) {
            newest_info = info;
            newest_path = candidate;
        }
    }

    return newest_path;
}

std::optional<ResolvedSettings> load_settings(const QString& settings_path, QString* error)
{
    if (settings_path.isEmpty()) {
        if (error) {
            *error = QStringLiteral("no Input Leap settings file found");
        }
        return std::nullopt;
    }

    QSettings settings(settings_path, QSettings::IniFormat);
    if (!settings.value("autoStart", false).toBool()) {
        if (error) {
            *error = QStringLiteral("autoStart is disabled in %1").arg(settings_path);
        }
        return std::nullopt;
    }

    ResolvedSettings resolved;
    resolved.settings_path = settings_path;
    resolved.role = settings.value("groupServerChecked", false).toBool() ? Role::Server
                                                                         : Role::Client;
    resolved.screen_name = settings.value("screenName").toString();
    resolved.port = settings.value("port", 24800).toInt();
    resolved.interface_name = settings.value("interface").toString();
    resolved.crypto_enabled = settings.value("cryptoEnabled", true).toBool();
    resolved.require_client_certificate =
        settings.value("requireClientCertificate", false).toBool();
    resolved.log_to_file = settings.value("logToFile", false).toBool();
    resolved.log_filename = settings.value("logFilename").toString();
    resolved.log_level = log_level_name(settings.value("logLevel", 3).toInt());
    resolved.use_internal_config = settings.value("useInternalConfig", true).toBool();
    resolved.external_config_path = settings.value("configFile").toString();
    resolved.server_hostname = settings.value("serverHostname").toString();

    if (resolved.screen_name.isEmpty()) {
        if (error) {
            *error = QStringLiteral("screenName is empty in %1").arg(settings_path);
        }
        return std::nullopt;
    }

    if (resolved.role == Role::Client) {
        if (resolved.server_hostname.isEmpty()) {
            if (error) {
                *error = QStringLiteral("serverHostname is empty in %1").arg(settings_path);
            }
            return std::nullopt;
        }
        return resolved;
    }

    if (resolved.use_internal_config) {
        resolved.generated_server_config = render_server_config(settings);
        return resolved;
    }

    if (resolved.external_config_path.isEmpty() ||
        !QFileInfo::exists(resolved.external_config_path)) {
        if (error) {
            *error = QStringLiteral("external config file is missing for %1").arg(settings_path);
        }
        return std::nullopt;
    }

    return resolved;
}

QString detect_display()
{
    const auto display = qEnvironmentVariable("DISPLAY");
    if (!display.isEmpty()) {
        return display;
    }

    return QStringLiteral(":0");
}

QString detect_xauthority(const QString& display)
{
    const auto environment_xauthority = qEnvironmentVariable("XAUTHORITY");
    if (!environment_xauthority.isEmpty()) {
        return environment_xauthority;
    }

    const QStringList candidates{
        QStringLiteral("/var/run/lightdm/root/") + display,
        QStringLiteral("/run/lightdm/root/") + display,
    };

    for (const auto& candidate : candidates) {
        QFileInfo info(candidate);
        if (info.isFile() && info.isReadable()) {
            return candidate;
        }
    }

    return {};
}

std::optional<ResolvedCommand> build_command(const ResolvedSettings& settings, QString* error)
{
    const QString display = detect_display();
    const QString xauthority = detect_xauthority(display);

    ResolvedCommand command;
    command.xauthority = xauthority;
    command.arguments << "-f" << "--display" << display << "--no-tray" << "--debug"
                      << settings.log_level << "--name" << settings.screen_name;

    if (!settings.crypto_enabled) {
        command.arguments << "--disable-crypto";
    }

    if (settings.log_to_file && !settings.log_filename.isEmpty()) {
        command.arguments << "--log" << settings.log_filename;
    }

    if (settings.role == Role::Client) {
        command.program = program_dir() + QStringLiteral("/input-leapc");
        command.arguments
            << QStringLiteral("%1:%2")
                   .arg(normalize_host(settings.server_hostname))
                   .arg(settings.port);
        return command;
    }

    command.program = program_dir() + QStringLiteral("/input-leaps");
    if (!settings.require_client_certificate) {
        command.arguments << "--disable-client-cert-checking";
    }

    QString config_path = settings.external_config_path;
    if (settings.use_internal_config) {
        if (!persist_server_config(settings.generated_server_config, error)) {
            return std::nullopt;
        }
        config_path = generated_server_config_path();
    }

    QString address;
    if (!settings.interface_name.isEmpty()) {
        address = normalize_host(settings.interface_name);
    }
    address += QStringLiteral(":%1").arg(settings.port);

    command.arguments << "-c" << config_path << "--address" << address;
    return command;
}

int run_service()
{
    QString error;
    const auto settings_path = find_settings_file(default_settings_candidates());
    const auto resolved_settings = load_settings(settings_path, &error);
    if (!resolved_settings) {
        fprintf(stderr, "%s\n", error.toUtf8().constData());
        return 1;
    }

    const auto command = build_command(*resolved_settings, &error);
    if (!command) {
        fprintf(stderr, "%s\n", error.toUtf8().constData());
        return 1;
    }

    if (!command->xauthority.isEmpty()) {
        qputenv("XAUTHORITY", command->xauthority.toUtf8());
    }
    qputenv("DISPLAY", detect_display().toUtf8());

    QByteArray program = command->program.toUtf8();
    std::vector<QByteArray> utf8_arguments;
    utf8_arguments.reserve(command->arguments.size() + 1);
    utf8_arguments.push_back(program);
    for (const auto& argument : command->arguments) {
        utf8_arguments.push_back(argument.toUtf8());
    }

    std::vector<char*> argv;
    argv.reserve(utf8_arguments.size() + 1);
    for (auto& argument : utf8_arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    execv(program.constData(), argv.data());
    perror("execv");
    return 1;
}

} // namespace inputleap::packaged_autostart
