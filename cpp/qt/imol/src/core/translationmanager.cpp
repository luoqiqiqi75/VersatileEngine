#include "imol/translationmanager.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTranslator>
#include <QCoreApplication>

#include "imol/modulemanager.h"
#include "imol/logmanager.h"

using namespace imol;

TranslationManager::TranslationManager(QObject *parent) : QObject(parent),
    m_qt_translator(nullptr)
{
    QString cfg_language = m("ve.config.common.language")->getString("en");
//    installQMTranslator(QString(":/imol/language/qt_%1.qm").arg(cfg_language == "cn" ? "zh_CN" : cfg_language));
    foreach (auto fi, QDir("./i18n").entryInfoList(QStringList() << QString("*_%1.qm").arg(cfg_language), QDir::Files | QDir::NoDotAndDotDot)) {
        bool success = installQMTranslator(fi.absoluteFilePath());
        ILOG << "<translator> install " << cfg_language << " language qm " << fi.baseName() << (success ? " succeed" : " failed");
    }
}

TranslationManager::~TranslationManager()
{
    m_static_translators.clear();
    m_dynamic_translators.clear();
}

TranslationManager & TranslationManager::instance()
{
    static TranslationManager manager;
    return manager;
}

bool TranslationManager::installQMTranslator(const QString &qm_path)
{
    if (m_static_translators.contains(qm_path)) {
        WSLOG << "qm translator already exists:" << qm_path;
        return false;
    }

    QTranslator *qm_translator = new QTranslator(qApp);
    if (!qm_translator->load(qm_path)) {
        ESLOG << "qm transtor load failed:" << qm_path;
        delete qm_translator;
        return false;
    }

    if (!qApp->installTranslator(qm_translator)) {
        ESLOG << "qm transtor install failed:" << qm_path;
        delete qm_translator;
        return false;
    }

    m_static_translators.insert(qm_path, qm_translator);
    return true;
}

bool TranslationManager::removeQMTranslator(const QString &qm_path)
{
    if (!m_static_translators.contains(qm_path)) {
        WSLOG << "qm translator not exist:" << qm_path;
        return false;
    }

    QTranslator *qm_translator = m_static_translators.value(qm_path);
    if (!qApp->removeTranslator(qm_translator)) {
        ESLOG << "qm translator remove failed:" << qm_path;
        return false;
    }

    delete qm_translator;
    m_static_translators.remove(qm_path);
    return true;
}

bool TranslationManager::installJsonTranslator(const QString &name, const QJsonObject &json_obj)
{
    bool wrong_format = json_obj.isEmpty();
    foreach (QJsonValue value, json_obj) {
        if (value.isString()) continue;
        wrong_format = true;
        break;
    }
    if (wrong_format) {
        WSLOG << "json translator detects wrong format!";
        return false;
    }

    m_dynamic_translators.insert(name, json_obj);
    return true;
}

bool TranslationManager::installJsonTranslator(const QString &name, const QString &json_path)
{
    QFile json_file(json_path);
    if (!json_file.open(QIODevice::ReadOnly)) {
        ESLOG << "json translator file cannot read:" << json_path;
        return false;
    }

    QJsonDocument json_doc = QJsonDocument::fromJson(json_file.readAll());
    json_file.close();

    return installJsonTranslator(name, json_doc.object());
}

bool TranslationManager::removeJsonTranslator(const QString &name)
{
    if (!m_dynamic_translators.contains(name)) {
        ESLOG << "json translator not exist:" << name;
        return false;
    }

    m_dynamic_translators.remove(name);
    return true;
}

QString TranslationManager::dtr(const QString &name, const QString &text) const
{
    return m_dynamic_translators.contains(name) ? m_dynamic_translators.value(name).value(text).toString(text) : text;
}

bool TranslationManager::exists(const QString &name, const QString &text) const
{
    return m_dynamic_translators.value(name, QJsonObject()).contains(text);
}

imol::TranslationManager & translator()
{
    return TranslationManager::instance();
}
