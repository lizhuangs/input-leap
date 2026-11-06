#include "../src/LinuxPackagedAutostart.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include <gtest/gtest.h>

#include <unistd.h>

namespace {

QString writeSettingsFile(const QTemporaryDir& dir, const QString& name, const QString& content)
{
    const auto path = dir.filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
    file.close();
    return path;
}

TEST(LinuxPackagedAutostartTests, FindsNewestAutoStartSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const auto older = writeSettingsFile(dir, "older.conf", "[General]\nautoStart=true\n");
    sleep(1);
    const auto newer = writeSettingsFile(dir, "newer.conf", "[General]\nautoStart=true\n");

    const auto found = inputleap::packaged_autostart::find_settings_file({older, newer});
    EXPECT_EQ(found, newer);
}

TEST(LinuxPackagedAutostartTests, LoadsClientSettingsAndBuildsCommand)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const auto settings_path = writeSettingsFile(
        dir, "client.conf",
        "[General]\n"
        "autoStart=true\n"
        "groupClientChecked=true\n"
        "screenName=client-screen\n"
        "serverHostname=192.168.22.200\n"
        "port=24800\n"
        "cryptoEnabled=false\n"
        "logLevel=4\n");

    QString error;
    auto resolved = inputleap::packaged_autostart::load_settings(settings_path, &error);
    ASSERT_TRUE(resolved.has_value()) << error.toStdString();
    EXPECT_EQ(resolved->screen_name, "client-screen");

    auto command = inputleap::packaged_autostart::build_command(*resolved, &error);
    ASSERT_TRUE(command.has_value()) << error.toStdString();
    EXPECT_TRUE(command->program.endsWith("/input-leapc"));
    EXPECT_NE(command->arguments.indexOf("--disable-crypto"), -1);
    EXPECT_TRUE(command->arguments.contains("[192.168.22.200]:24800"));
}

TEST(LinuxPackagedAutostartTests, RendersInternalServerConfig)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const auto settings_path = writeSettingsFile(
        dir, "server.conf",
        "[General]\n"
        "autoStart=true\n"
        "groupServerChecked=true\n"
        "screenName=server-screen\n"
        "port=24800\n"
        "useInternalConfig=true\n"
        "\n"
        "[internalConfig]\n"
        "numColumns=2\n"
        "numRows=1\n"
        "relativeMouseMoves=false\n"
        "screenSaverSync=true\n"
        "win32KeepForeground=false\n"
        "clipboardSharing=true\n"
        "clipboardSharingSize=1234\n"
        "screens\\size=2\n"
        "screens\\1\\name=server-screen\n"
        "screens\\1\\switchCornerSize=0\n"
        "screens\\2\\name=client-screen\n"
        "screens\\2\\switchCornerSize=0\n"
        "switchCornerArray\\size=4\n"
        "switchCornerArray\\1\\switchCorner=false\n"
        "switchCornerArray\\2\\switchCorner=false\n"
        "switchCornerArray\\3\\switchCorner=false\n"
        "switchCornerArray\\4\\switchCorner=false\n");

    QString error;
    auto resolved = inputleap::packaged_autostart::load_settings(settings_path, &error);
    ASSERT_TRUE(resolved.has_value()) << error.toStdString();
    EXPECT_EQ(resolved->role, inputleap::packaged_autostart::Role::Server);
    EXPECT_TRUE(resolved->generated_server_config.contains("server-screen"));
    EXPECT_TRUE(resolved->generated_server_config.contains("right = client-screen"));
    EXPECT_TRUE(resolved->generated_server_config.contains("clipboardSharingSize = 1234"));
}

} // namespace
