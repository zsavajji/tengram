#include "ContactBook.h"

#include <bb/pim/contacts/Contact>
#include <bb/pim/contacts/ContactAttribute>
#include <bb/pim/contacts/ContactListFilters>
#include <bb/pim/contacts/ContactService>
#include <QDebug>
#include <QThread>
#include <QTimer>

#include "EngineLink.h"

using namespace bb::pim::contacts;

// ContactService's own advice: no more than 200 contacts a page.
static const int PageSize = 200;
// ponytail: the engine's reader takes one line of at most 1 MiB (engine/server.go), and past it the
// connection drops (and the UI would send the book again on the reconnect). JsonDataAccess keeps
// its indentation, so an entry is about 110 bytes, 200 with a long non-ASCII name: 4000 fit, and
// numbers past them keep WhatsApp's name. Chunk the book if one is ever that large.
static const int MaxEntries = 4000;

BookReader::BookReader()
    : m_service(0),
      m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setInterval(2000);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(read()));
}

void BookReader::start()
{
    m_service = new ContactService(this);
    connect(m_service, SIGNAL(contactsAdded(QList<int>)), this, SLOT(changed()));
    connect(m_service, SIGNAL(contactsChanged(QList<int>)), this, SLOT(changed()));
    connect(m_service, SIGNAL(contactsDeleted(QList<int>)), this, SLOT(changed()));
    connect(m_service, SIGNAL(contactsReset()), this, SLOT(changed()));
    m_timer->start(); // not at once, so the first read doesn't compete with the app's start
}

void BookReader::changed()
{
    m_timer->start(); // restarts a running timer
}

void BookReader::read()
{
    QVariantList entries;
    int contacts = 0;
    ContactListFilters options;
    options.setLimit(PageSize);
    options.setHasAttribute(AttributeKind::Phone);
    options.setIncludeAttribute(AttributeKind::Phone);
    ContactId anchor = -1;
    for (;;) {
        const QList<Contact> page = m_service->contacts(options);
        if (page.isEmpty())
            break;
        foreach (const Contact &listed, page) {
            if (entries.size() >= MaxEntries)
                break;
            // The list gives partial contacts. If one comes without its numbers or its name,
            // ask for it whole.
            // ponytail: untried on the phone; one extra call per such contact.
            Contact c = listed;
            if (c.phoneNumbers().isEmpty() || c.displayName().isEmpty())
                c = m_service->contactDetails(listed.id());
            ++contacts;
            const QString name = c.displayName();
            foreach (const ContactAttribute &phone, c.phoneNumbers()) {
                if (name.isEmpty() || phone.value().isEmpty() || entries.size() >= MaxEntries)
                    break;
                QVariantMap entry;
                entry["number"] = phone.value();
                entry["name"] = name;
                entries << entry;
            }
        }
        // A short page is the last one; so is a page that doesn't move the anchor.
        if (page.size() < PageSize || entries.size() >= MaxEntries || page.last().id() == anchor)
            break;
        anchor = page.last().id();
        options.setAnchorId(anchor);
    }
    // Counts only: the book is names and numbers.
    qDebug() << "contact book:" << contacts << "contacts," << entries.size() << "numbers";
    emit ready(entries);
}

ContactBook::ContactBook(EngineLink *engine, QObject *parent)
    : QObject(parent),
      m_engine(engine),
      m_thread(new QThread) // no parent, see ~ContactBook
{
    BookReader *reader = new BookReader;
    reader->moveToThread(m_thread);
    connect(m_thread, SIGNAL(started()), reader, SLOT(start()));
    connect(reader, SIGNAL(ready(QVariantList)), this, SLOT(onRead(QVariantList)), Qt::QueuedConnection);
    connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(send()));
    // Low, so the read doesn't slow the UI down.
    m_thread->start(QThread::LowPriority);
}

ContactBook::~ContactBook()
{
    m_thread->quit();
    m_thread->wait(2000);
    // The thread (and the reader on it) is deliberately not deleted: a QThread destroyed while it
    // still runs aborts the process, as it would if the reader is in a long read, and this object
    // lasts as long as the app does.
}

void ContactBook::onRead(const QVariantList &entries)
{
    // ponytail: an empty read changes nothing. A refused permission or a PIM service that isn't up
    // yet looks just like an empty book, and sending it would rename everyone back. So deleting
    // every contact, or taking the permission away, keeps the old names; send an empty list if
    // that ever matters.
    if (entries.isEmpty()) {
        qDebug() << "contact book: empty read, kept as it was";
        return;
    }
    m_entries = entries;
    send();
}

// Also on every connect, for an engine that restarted or lost data/book.json: a book that hasn't
// changed costs it nothing.
void ContactBook::send()
{
    if (!m_engine->connected() || m_entries.isEmpty())
        return;
    QVariantMap args;
    args["entries"] = m_entries;
    m_engine->requestQuiet("contacts.book", args);
}
