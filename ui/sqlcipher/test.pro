# Test build of the SQLCipher-backed Qt SQL driver (scripts/build-qtsqlcipher-test.sh runs this).
TEMPLATE = app
TARGET = sqlciphertest
CONFIG += qt warn_on console
CONFIG -= app_bundle
QT = core sql
TD = ../../third_party/td/sqlite
GEN = ../../build/qtsqlcipher
INCLUDEPATH += $$TD $$GEN . ../../build/prefix/include
HEADERS += $$GEN/qsql_sqlcipher.h tdsqlite_names.h
SOURCES += test.cpp $$GEN/qsql_sqlcipher.cpp $$TD/sqlite/sqlite3.c aeabi_mem.c
# TDLib's own flags for this file (third_party/td/sqlite/CMakeLists.txt), without SQLITE_OMIT_DECLTYPE:
# Qt's driver asks for declared column types.
DEFINES += OMIT_MEMLOCK SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DEFAULT_RECURSIVE_TRIGGERS=1 \
    SQLITE_DEFAULT_SYNCHRONOUS=1 SQLITE_DISABLE_LFS SQLITE_HAS_CODEC SQLITE_OMIT_DEPRECATED \
    SQLITE_OMIT_DESERIALIZE SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_TEMP_STORE=2 \
    USE_PREAD=1
QMAKE_CFLAGS_WARN_ON = -w
LIBS += $$CRYPTO -lsocket
QMAKE_LFLAGS += -Wl,--gc-sections
