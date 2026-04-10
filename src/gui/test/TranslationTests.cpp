/*  InputLeap -- mouse and keyboard sharing utility
    Copyright (C) 2026

    This package is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    found in the file LICENSE that should have accompanied this file.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <gtest/gtest.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QXmlStreamReader>

namespace {

QString sourceRoot()
{
    const QFileInfo test_file_info(QStringLiteral(__FILE__));
    return test_file_info.dir().absoluteFilePath(QStringLiteral(".."));
}

QSet<QString> readUiStrings(const QString& ui_path)
{
    QFile file(ui_path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text))
        << "Unable to open UI file: " << ui_path.toStdString();

    QSet<QString> strings;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("string")) {
            strings.insert(xml.readElementText());
        }
    }

    EXPECT_FALSE(xml.hasError()) << xml.errorString().toStdString();
    return strings;
}

QSet<QString> readTranslatedSources(const QString& ts_path, const QString& context_name)
{
    QFile file(ts_path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text))
        << "Unable to open translation file: " << ts_path.toStdString();

    QSet<QString> translated_sources;
    QXmlStreamReader xml(&file);
    QString current_context;

    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }

        if (xml.name() == QLatin1String("name")) {
            current_context = xml.readElementText();
            continue;
        }

        if (xml.name() != QLatin1String("message") || current_context != context_name) {
            continue;
        }

        QString source;
        QString translation;
        bool translation_unfinished = false;

        while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("message")) && !xml.atEnd()) {
            const auto message_token = xml.readNext();
            if (message_token != QXmlStreamReader::StartElement) {
                continue;
            }

            if (xml.name() == QLatin1String("source")) {
                source = xml.readElementText();
                continue;
            }

            if (xml.name() == QLatin1String("translation")) {
                translation_unfinished = xml.attributes().value(QLatin1String("type")) == QLatin1String("unfinished");
                translation = xml.readElementText();
            }
        }

        if (!source.isEmpty() && !translation_unfinished && !translation.isEmpty()) {
            translated_sources.insert(source);
        }
    }

    EXPECT_FALSE(xml.hasError()) << xml.errorString().toStdString();
    return translated_sources;
}

} // namespace

QStringList missingSettingsDialogStrings(const QString& locale)
{
    const QString gui_root = sourceRoot();
    const QSet<QString> ui_strings = readUiStrings(gui_root + QStringLiteral("/src/SettingsDialog.ui"));
    const QSet<QString> translated_sources =
        readTranslatedSources(gui_root + QStringLiteral("/res/lang/gui_") + locale + QStringLiteral(".ts"),
                              QStringLiteral("SettingsDialog"));

    QStringList missing_strings;
    for (const auto& ui_string : ui_strings) {
        if (!translated_sources.contains(ui_string)) {
            missing_strings.push_back(ui_string);
        }
    }

    missing_strings.sort();
    return missing_strings;
}

TEST(TranslationTests, SettingsDialogZhCnCoversUiStrings)
{
    const QStringList missing_strings = missingSettingsDialogStrings(QStringLiteral("zh-CN"));
    EXPECT_TRUE(missing_strings.isEmpty())
        << "Missing zh-CN translations for SettingsDialog UI strings:\n"
        << missing_strings.join(QStringLiteral("\n")).toStdString();
}

TEST(TranslationTests, SettingsDialogZhTwCoversUiStrings)
{
    const QStringList missing_strings = missingSettingsDialogStrings(QStringLiteral("zh-TW"));
    EXPECT_TRUE(missing_strings.isEmpty())
        << "Missing zh-TW translations for SettingsDialog UI strings:\n"
        << missing_strings.join(QStringLiteral("\n")).toStdString();
}

void expectTranslatedSources(const QString& locale, const QString& context_name,
                             std::initializer_list<const char*> sources)
{
    const QString gui_root = sourceRoot();
    const QSet<QString> translated_sources =
        readTranslatedSources(gui_root + QStringLiteral("/res/lang/gui_") + locale + QStringLiteral(".ts"),
                              context_name);

    QStringList missing_strings;
    for (const auto* source : sources) {
        const QString source_text = QString::fromUtf8(source);
        if (!translated_sources.contains(source_text)) {
            missing_strings.push_back(source_text);
        }
    }

    missing_strings.sort();
    EXPECT_TRUE(missing_strings.isEmpty())
        << "Missing " << locale.toStdString() << " translations for " << context_name.toStdString() << ":\n"
        << missing_strings.join(QStringLiteral("\n")).toStdString();
}

TEST(TranslationTests, MainWindowZhTwTranslatesVisibleMenuStrings)
{
    expectTranslatedSources(QStringLiteral("zh-TW"), QStringLiteral("MainWindow"), {
        "Ser&ver (share this computer's mouse and keyboard):",
        "&Client (use another computer's mouse and keyboard):",
        "Auto config",
        "SSL Fingerprint:",
        "S&ave configuration",
        "Change &Settings",
        "Edit settings",
        "Show &Log",
        "Show Log",
    });
}

TEST(TranslationTests, LogWindowZhTwTranslatesVisibleStrings)
{
    expectTranslatedSources(QStringLiteral("zh-TW"), QStringLiteral("LogWindow"), {
        "Log - InputLeap",
        "&Clear Log",
        "&Hide",
    });
}
