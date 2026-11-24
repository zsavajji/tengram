#ifndef CONTACTBOOK_H
#define CONTACTBOOK_H

#include <QObject>
#include <QVariant>

class EngineLink;
class Settings;
class QThread;
class QTimer;
namespace bb { namespace pim { namespace contacts { class ContactService; } } }

// Reads the phone's address book on its own thread, since ContactService::contacts() blocks. The
// ContactService is made in start(), on that thread, because it belongs to the thread that made it.
class BookReader : public QObject
{
    Q_OBJECT
public:
    BookReader(); // no parent: it moves to a thread

public slots:
    // Run by the thread's started(): opens the service; the first read waits for readIn().
    void start();
    // The first read, ms from now: asked by the UI once the scene is set, so it doesn't compete with the app's start.
    void readIn(int ms);

signals:
    // The book as [{number, name}], one entry per number of each contact.
    void ready(const QVariantList &entries);

private slots:
    // Any change to the address book: read again 2 s after the last one, since a sync fires in bursts. Not before
    // the first read, which takes the whole book anyway.
    void changed();
    void read();

private:
    bb::pim::contacts::ContactService *m_service;
    QTimer *m_timer;
    bool m_first; // true until the first read has started
};

// The phone's own address book, handed to the engine (contacts.book, docs/protocol.md) so a person
// saved on the Q10 shows under that name. The engine can't read it: only the UI has the permission.
// The book only ever goes to our own engine, over the loopback link; the UI sends it nowhere else,
// and keeps it in memory only (the engine keeps data/book.json). Sent when read, on every
// (re)connect of the engine link, and again when the address book changes, but only while Settings >
// Privacy and security > Sync contacts is on (off by default), and at once when it is switched on.
class ContactBook : public QObject
{
    Q_OBJECT
public:
    ContactBook(EngineLink *engine, Settings *settings, QObject *parent = 0);
    ~ContactBook();
    // The first read of the address book, ms after now: main.cpp asks once the scene is set.
    void readAfter(int ms);

private slots:
    void onRead(const QVariantList &entries);
    void send();

private:
    EngineLink *m_engine;
    Settings *m_settings;
    QThread *m_thread;
    BookReader *m_reader;
    QVariantList m_entries;
};

#endif
