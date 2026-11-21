#include "ChatController.h"

#include "EngineLink.h"

#include <QLocale>

// The long-press Translate dialog's request: one message, one reply. The connections are made at the first use, so a
// chat that never translates carries none.

void ChatController::translate(const QString &id, const QString &to)
{
    if (!m_engine)
        return;
    connect(m_engine, SIGNAL(replied(QString, QString, QVariant)), this,
            SLOT(onTranslateReplied(QString, QString, QVariant)), Qt::UniqueConnection);
    connect(m_engine, SIGNAL(failureChanged()), this, SLOT(onTranslateFailure()), Qt::UniqueConnection);
    QVariantMap args;
    args["chat"] = m_jid;
    args["ids"] = QVariantList() << id;
    args["to"] = to;
    m_translateTo = to;
    // Not connected, request() fails at once, inside the call: the "-" is there for onTranslateFailure.
    m_translateId = "-";
    m_translateId = m_engine->request("msg.translate", args);
}

void ChatController::onTranslateReplied(const QString &id, const QString &cmd, const QVariant &result)
{
    if (cmd != "msg.translate" || id != m_translateId)
        return;
    m_translateId.clear();
    const QVariantList items = result.toMap()["items"].toList();
    // Left out of the items: not a text or a caption, nothing to translate.
    if (items.isEmpty()) {
        emit translateFailed("translate_failed");
        return;
    }
    const QVariantMap item = items.first().toMap();
    emit translated(item["id"].toString(), item["text"].toString(), m_translateTo);
}

void ChatController::onTranslateFailure()
{
    const QVariantMap failure = m_engine ? m_engine->failure() : QVariantMap();
    if (m_translateId.isEmpty() || failure.value("cmd").toString() != "msg.translate")
        return;
    m_translateId.clear();
    emit translateFailed(failure.value("code").toString());
}

QString ChatController::languageName(const QString &code) const
{
    QString name = QLocale(code).nativeLanguageName();
    if (name.isEmpty() || QLocale(code).language() == QLocale::C)
        return code;
    return name.left(1).toUpper() + name.mid(1);
}
