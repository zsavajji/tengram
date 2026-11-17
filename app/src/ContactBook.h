#ifndef CONTACTBOOK_H
#define CONTACTBOOK_H

#include <QObject>
#include <QVariant>

class EngineLink;
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
    // Run by the thread's started(): opens the service and schedules the first read.
    void start();

signals:
    // The book as [{number, name}], one entry per number of each contact.
    void ready(const QVariantList &entries);

private slots:
    // Any change to the address book: read again 2 s after the last one, since a sync fires in bursts.
    void changed();
    void read();

private:
    bb::pim::contacts::ContactService *m_service;
    QTimer *m_timer;
};

// The phone's own address book, handed to the engine (contacts.book, docs/protocol.md) so a person
// saved on the Q10 shows under that name. The engine can't read it: only the UI has the permission.
// The book only ever goes to our own engine, over the loopback link; the UI sends it nowhere else,
// and keeps it in memory only (the engine keeps data/book.json). Sent when read, on every
// (re)connect of the engine link, and again when the address book changes.
class ContactBook : public QObject
{
    Q_OBJECT
public:
    explicit ContactBook(EngineLink *engine, QObject *parent = 0);
    ~ContactBook();

private slots:
    void onRead(const QVariantList &entries);
    void send();

private:
    EngineLink *m_engine;
    QThread *m_thread;
    QVariantList m_entries;
};

#endif
