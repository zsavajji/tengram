#ifndef CONTACTPICKER_H
#define CONTACTPICKER_H

#include <bb/cascades/ArrayDataModel>
#include <bb/cascades/DataModel>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

class EngineLink;

// The new chat page's address book (PLAN.md §7, step 3.6), made by QML in the page's
// attachedObjects so it dies with the page. It asks contacts.list, draws each contact as a
// chat-list row (the tile the chat will have) and filters them as the user types. The new
// group page also ticks contacts here (m_chosen, so a filter keeps the ticks) and creates the group.
// The group-call page (step 7d.G4) uses it in a group-members mode: loadGroup() fills it with the
// group's members instead (us left out), toggle() stops at 31, and startGroupCall() rings the ticked,
// as a voice or a video call.
class ContactPicker : public QObject
{
    Q_OBJECT
    // The EngineLink; named link, not engine, for the reason in ChatController.h.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The rows shown: jid, kind 0, name, initials, color.
    Q_PROPERTY(bb::cascades::DataModel *model READ model CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    // How many contacts (in group mode: members other than us) there are in all, and how many the
    // filter shows (the model's size() isn't re-read by a binding).
    Q_PROPERTY(int total READ total NOTIFY loadedChanged)
    Q_PROPERTY(int shown READ shown NOTIFY shownChanged)
    // How many contacts the new group page has ticked.
    Q_PROPERTY(int chosenCount READ chosenCount NOTIFY chosenChanged)
public:
    explicit ContactPicker(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    bb::cascades::DataModel *model() const;
    bool loaded() const;
    int total() const;
    int shown() const;
    int chosenCount() const;

    Q_INVOKABLE void load();
    // The group-members mode: asks group.info for chat and lists its members but us, with a limit
    // of 31 ticks (a group call's most).
    Q_INVOKABLE void loadGroup(const QString &chat);
    // Shows the contacts with a word of the name starting with text, ignoring case; "" shows all.
    Q_INVOKABLE void filter(const QString &text);
    // Ticks or unticks the row at indexPath, and redraws just that row. False, and nothing
    // changes, when ticking would go past the limit (there is none outside group mode).
    Q_INVOKABLE bool toggle(const QVariantList &indexPath);
    // The ticked contacts' jids, for a story's audience (StoryAudienceSheet); in no order.
    Q_INVOKABLE QStringList chosen() const;
    // Unticks every contact ("Everyone" in the audience sheet).
    Q_INVOKABLE void clearChosen();
    // Asks group.create for a group of the ticked contacts; the request's id, "" when the
    // engine isn't connected. The reply comes as groupCreated.
    Q_INVOKABLE QString createGroup(const QString &name);
    // Asks call.start for a call of the group loadGroup() was given, with the ticked members, a
    // video call when video; the request's id, "" when the engine isn't connected. The call screen
    // opens on the call event, and a failure is worded by main.qml.
    Q_INVOKABLE QString startGroupCall(bool video);

signals:
    void loadedChanged();
    void shownChanged();
    void chosenChanged();
    // refused: how many the privacy settings kept out of the group.
    void groupCreated(const QString &jid, int refused);

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onConnected();
    void onPhoto(const QString &jid, const QString &path);

private:
    void ask();

    QPointer<EngineLink> m_engine;
    bb::cascades::ArrayDataModel *m_model;
    QVariantList m_all;
    QString m_text;
    QString m_askId; // the contacts.list (or group.info) request we wait for
    QString m_chat; // the group of loadGroup(), "" for the address book
    int m_limit; // most ticks, 0 for no limit
    QString m_createId; // the group.create request we wait for
    QSet<QString> m_chosen; // jids, kept across filters
    bool m_loaded;
    bool m_wanted; // load() was called
};

#endif
