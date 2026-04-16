#ifndef IMOL_TRANSLATIONMANAGER_H
#define IMOL_TRANSLATIONMANAGER_H

#include "core_global.h"

#include <QObject>
#include <QHash>

class QTranslator;
namespace imol {
class CORESHARED_EXPORT TranslationManager : public QObject
{
    Q_OBJECT

public:
    explicit TranslationManager(QObject *parent = nullptr);
    ~TranslationManager();
    static TranslationManager & instance();

    bool installQMTranslator(const QString &qm_path);
    bool removeQMTranslator(const QString &qm_path);

    bool installJsonTranslator(const QString &name, const QJsonObject &json_obj);
    bool installJsonTranslator(const QString &name, const QString &json_path);
    bool removeJsonTranslator(const QString &name);

    //!
    //! \brief dtr means dynamic translate
    //! \param name [in] translator name
    //! \param text [in] source text
    //! \return translated text
    //!
    QString dtr(const QString &name, const QString &text) const;

    bool exists(const QString &name, const QString &text) const;

signals:

public slots:

private:
    QTranslator *m_qt_translator;
    QHash<QString, QTranslator *> m_static_translators;
    QHash<QString, QJsonObject> m_dynamic_translators;
};
}

//output methods
CORESHARED_EXPORT imol::TranslationManager & translator();

#endif // IMOL_TRANSLATIONMANAGER_H
