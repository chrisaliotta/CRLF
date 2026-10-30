#include "app/ConPtySession.h"

#include <QDir>
#include <QVector>

#include <string>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

using PseudoConsole = void *;
using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, PseudoConsole *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(PseudoConsole, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(PseudoConsole);

struct ConPtyApi
{
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
};

const ConPtyApi &conPtyApi()
{
    static const ConPtyApi api = [] {
        ConPtyApi loaded;
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        loaded.create = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(kernel, "CreatePseudoConsole"));
        loaded.resize = reinterpret_cast<ResizePseudoConsoleFn>(GetProcAddress(kernel, "ResizePseudoConsole"));
        loaded.close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        return loaded;
    }();
    return api;
}

// wsl.exe reads the raw command line. A quoted "-d" is not its distro option;
// bash then tries to run a command named -d.
bool needsQuotes(const QString &arg)
{
    if (arg.isEmpty())
        return true;
    for (const QChar c : arg) {
        if (c.isSpace() || c == QLatin1Char('"'))
            return true;
    }
    return false;
}

QString quoteArg(const QString &arg)
{
    if (!needsQuotes(arg))
        return arg;
    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : arg) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"'))
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
        else
            quoted += QString(backslashes, QLatin1Char('\\'));
        backslashes = 0;
        quoted += c;
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

QString lastError()
{
    const DWORD code = GetLastError();
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    const QString text = message ? QString::fromWCharArray(message).trimmed()
                                 : QStringLiteral("error %1").arg(code);
    if (message)
        LocalFree(message);
    return text;
}

QVector<wchar_t> environmentBlock()
{
    QVector<wchar_t> block;
    wchar_t *current = GetEnvironmentStringsW();
    if (current) {
        for (const wchar_t *entry = current; *entry;) {
            const QString text = QString::fromWCharArray(entry);
            entry += text.size() + 1;
            if (text.startsWith(QLatin1String("TERM="), Qt::CaseInsensitive))
                continue;
            const auto *data = reinterpret_cast<const wchar_t *>(text.utf16());
            for (int i = 0; i < text.size(); ++i)
                block.append(data[i]);
            block.append(L'\0');
        }
        FreeEnvironmentStringsW(current);
    }
    const QString term = QStringLiteral("TERM=xterm-256color");
    const auto *data = reinterpret_cast<const wchar_t *>(term.utf16());
    for (int i = 0; i < term.size(); ++i)
        block.append(data[i]);
    block.append(L'\0');
    block.append(L'\0');
    return block;
}

void closeHandle(void *&handle)
{
    if (!handle)
        return;
    CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

#endif

} // namespace

ConPtySession::ConPtySession(QObject *parent)
    : QObject(parent)
{
}

ConPtySession::~ConPtySession()
{
    stop();
}

void ConPtySession::start(const QString &program, const QStringList &arguments,
                          const QString &workingDirectory, int rows, int cols)
{
    stop();
    m_rows = qMax(1, rows);
    m_cols = qMax(1, cols);

#ifndef Q_OS_WIN
    Q_UNUSED(program);
    Q_UNUSED(arguments);
    Q_UNUSED(workingDirectory);
    fail(QStringLiteral("Couldn't start the shell.\n"));
    return;
#else
    const ConPtyApi &api = conPtyApi();
    if (!api.create || !api.resize || !api.close) {
        fail(QStringLiteral("Couldn't start the shell.\n"));
        return;
    }

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) || !CreatePipe(&outputRead, &outputWrite, nullptr, 0)) {
        const QString error = lastError();
        if (inputRead)
            CloseHandle(inputRead);
        if (inputWrite)
            CloseHandle(inputWrite);
        if (outputRead)
            CloseHandle(outputRead);
        if (outputWrite)
            CloseHandle(outputWrite);
        fail(QStringLiteral("Couldn't start the shell.\n") + error);
        return;
    }

    PseudoConsole console = nullptr;
    const HRESULT created = api.create(COORD{SHORT(m_cols), SHORT(m_rows)}, inputRead, outputWrite, 0, &console);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(created) || !console) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        fail(QStringLiteral("Couldn't start the shell.\n") + lastError());
        return;
    }

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    auto *attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeSize));
    InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize);
    // The attribute value is the pseudoconsole handle itself. Passing its
    // address starts the process with no console, so the panel stays blank
    // and keystrokes never reach the shell.
    UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(console),
                              nullptr, nullptr);

    QString command = quoteArg(QDir::toNativeSeparators(program));
    for (const QString &argument : arguments)
        command += QLatin1Char(' ') + quoteArg(argument);
    std::wstring commandLine = command.toStdWString();
    const std::wstring application = QDir::toNativeSeparators(program).toStdWString();
    const std::wstring directory = QDir::toNativeSeparators(workingDirectory).toStdWString();
    QVector<wchar_t> environment = environmentBlock();

    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process = {};
    const BOOL started = CreateProcessW(
        application.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, environment.data(),
        workingDirectory.isEmpty() ? nullptr : directory.c_str(), &startup.StartupInfo, &process);

    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);

    if (!started) {
        api.close(console);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        fail(QStringLiteral("Couldn't start the shell.\n") + lastError());
        return;
    }

    CloseHandle(process.hThread);
    m_process = process.hProcess;
    m_pseudoConsole = console;
    m_inputWrite = inputWrite;
    m_outputRead = outputRead;

    const int generation = ++m_generation;
    m_thread = std::thread([this, generation] { readLoop(generation); });
#endif
}

void ConPtySession::writeInput(const QByteArray &data)
{
#ifdef Q_OS_WIN
    if (!m_inputWrite || data.isEmpty())
        return;
    const char *bytes = data.constData();
    int remaining = data.size();
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(static_cast<HANDLE>(m_inputWrite), bytes, DWORD(remaining), &written, nullptr) || written == 0)
            return;
        bytes += written;
        remaining -= int(written);
    }
#else
    Q_UNUSED(data);
#endif
}

void ConPtySession::resize(int rows, int cols)
{
    if (rows < 1 || cols < 1)
        return;
    m_rows = rows;
    m_cols = cols;
#ifdef Q_OS_WIN
    if (!m_pseudoConsole)
        return;
    conPtyApi().resize(static_cast<PseudoConsole>(m_pseudoConsole), COORD{SHORT(m_cols), SHORT(m_rows)});
#endif
}

void ConPtySession::stop()
{
    if (!m_thread.joinable() && !m_pseudoConsole && !m_outputRead && !m_process && !m_inputWrite)
        return;
    ++m_generation;
    release();
}

void ConPtySession::readLoop(int generation)
{
#ifdef Q_OS_WIN
    HANDLE output = static_cast<HANDLE>(m_outputRead);
    char buffer[4096];
    while (output) {
        DWORD count = 0;
        if (!ReadFile(output, buffer, sizeof buffer, &count, nullptr) || count == 0)
            break;
        const QByteArray bytes(buffer, int(count));
        QMetaObject::invokeMethod(this, [this, generation, bytes] {
            if (generation != m_generation)
                return;
            emit readyRead(bytes);
        }, Qt::QueuedConnection);
    }
#endif
    QMetaObject::invokeMethod(this, [this, generation] {
        if (generation != m_generation)
            return;
        release();
        emit exited();
    }, Qt::QueuedConnection);
}

void ConPtySession::release()
{
#ifdef Q_OS_WIN
    if (m_pseudoConsole) {
        conPtyApi().close(static_cast<PseudoConsole>(m_pseudoConsole));
        m_pseudoConsole = nullptr;
    }
    closeHandle(m_outputRead);
    if (m_thread.joinable())
        m_thread.join();
    if (m_process) {
        if (WaitForSingleObject(static_cast<HANDLE>(m_process), 200) == WAIT_TIMEOUT)
            TerminateProcess(static_cast<HANDLE>(m_process), 1);
        closeHandle(m_process);
    }
    closeHandle(m_inputWrite);
#else
    if (m_thread.joinable())
        m_thread.join();
#endif
}

void ConPtySession::fail(const QString &text)
{
    QByteArray bytes = text.toUtf8();
    if (!bytes.endsWith('\n'))
        bytes.append('\n');
    emit readyRead(bytes);
    emit exited();
}
