#ifndef PROFILE_H
#define PROFILE_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class EngineLink;

// My profile page (step 5.9), made by QML in the page's attachedObjects: the
// request's owner dies with the page, so no connection to the engine outlives it.
class Profile : public QObject
{
    Q_OBJECT
    // The EngineLink, set as ContactInfo's link is.
    Q_PROPERTY(QObject *link READ link WRITE setLink)
    // The last profile.get result {name, about, phone, picture}, with initials and color added for
    // the tile; empty until it comes.
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
public:
    explicit Profile(QObject *parent = 0);

    QObject *link() const;
    void setLink(QObject *link);
    QVariantMap info() const;

    // Asks the engine for the profile (profile.get); nothing without an engine.
    Q_INVOKABLE void ask();
    // Each sends profile.set and asks again when it is done; failed() follows a failure.
    Q_INVOKABLE void setName(const QString &name);
    Q_INVOKABLE void setAbout(const QString &about);
    Q_INVOKABLE void removePhoto();
    // Makes a 640x640 JPEG of the picture at croppedPath (the picker's crop), centred, and sends
    // it as the profile picture. False when the picture can't be read; nothing is sent then.
    Q_INVOKABLE bool setPhoto(const QString &croppedPath);

signals:
    void infoChanged();
    // A profile.set failed (the engine's failure is the reason).
    void failed();

private slots:
    void onReplied(const QString &id, const QString &cmd, const QVariant &result);
    void onFailure();
    void onConnected();

private:
    void set(const QString &key, const QString &value);

    QPointer<EngineLink> m_engine;
    QVariantMap m_info;
    QString m_id; // the profile.get request we wait for
    bool m_wanted; // the page asked; asked again when the engine connects, until it answers
};

#endif
