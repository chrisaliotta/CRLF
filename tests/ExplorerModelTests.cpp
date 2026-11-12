// Confirms that the explorer's tree follows the folder it shows without
// losing its place. Reading a folder again used to rebuild all of its rows,
// which collapsed every expanded subfolder, so the tree could not be kept up
// to date on its own. These cases use a real temporary folder: a re-read
// changes only the rows that differ, a change made on disk by anything
// reaches the tree with no call from the panel, and a new folder gets a row
// among the folders. See docs/processes.md.

#include "app/ExplorerModel.h"
#include "app/ProcessOrchestrator.h"

#include <QDir>
#include <QFile>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

constexpr int kWaitMs = 10000;

void writeFile(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
}

QStringList childNames(const ExplorerModel &model, const QModelIndex &parent)
{
    QStringList names;
    for (int row = 0; row < model.rowCount(parent); ++row)
        names.append(model.fileName(model.index(row, 0, parent)));
    return names;
}

} // namespace

class ExplorerModelTests : public QObject
{
    Q_OBJECT

private slots:
    // A folder with two files and a subfolder holding one file, listed and
    // with the subfolder opened, as a user would have it.
    void init()
    {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        m_root = QDir(m_directory->path());
        QVERIFY(m_root.mkdir(QStringLiteral("sub")));
        writeFile(m_root.filePath(QStringLiteral("a.txt")));
        writeFile(m_root.filePath(QStringLiteral("b.txt")));
        writeFile(m_root.filePath(QStringLiteral("sub/inner.txt")));

        m_model = std::make_unique<ExplorerModel>();
        m_model->setOrchestrator(&m_orchestrator);
        QSignalSpy loaded(m_model.get(), &ExplorerModel::directoryLoaded);
        m_model->setRootPath(m_root.absolutePath());
        QVERIFY(loaded.wait(kWaitMs));
        m_model->fetchMore(m_model->existingIndex(m_root.filePath(QStringLiteral("sub"))));
        QVERIFY(loaded.wait(kWaitMs));
    }

    // The model holds a handle on the folder, so it goes first.
    void cleanup()
    {
        m_model.reset();
        m_directory.reset();
    }

    // One file went and one came. The re-read removes one row and inserts
    // one. The rows that are still on disk are the same rows as before, so
    // the opened subfolder keeps its file and stays open in the view.
    void rereadingChangesOnlyTheRowsThatDiffer()
    {
        const QPersistentModelIndex kept = m_model->existingIndex(m_root.filePath(QStringLiteral("a.txt")));
        const QPersistentModelIndex inner = m_model->existingIndex(m_root.filePath(QStringLiteral("sub/inner.txt")));
        QVERIFY(kept.isValid());
        QVERIFY(inner.isValid());
        QVERIFY(QFile::remove(m_root.filePath(QStringLiteral("b.txt"))));
        writeFile(m_root.filePath(QStringLiteral("c.txt")));

        const QStringList expected{QStringLiteral("sub"), QStringLiteral("a.txt"), QStringLiteral("c.txt")};
        QSignalSpy removed(m_model.get(), &ExplorerModel::rowsRemoved);
        QSignalSpy inserted(m_model.get(), &ExplorerModel::rowsInserted);
        m_model->refresh(m_root.absolutePath());
        QTRY_COMPARE_WITH_TIMEOUT(childNames(*m_model, m_model->rootIndex()), expected, kWaitMs);

        QCOMPARE(removed.size(), 1);
        QCOMPARE(inserted.size(), 1);
        QVERIFY(kept.isValid());
        QVERIFY(inner.isValid());
        QCOMPARE(m_model->fileName(inner), QStringLiteral("inner.txt"));
    }

    // Nothing tells the model about these changes: a file written, a folder
    // made, and a file deleted, in the root and in the opened subfolder, as
    // an agent, a build, or another program would do. The tree shows each
    // one by itself.
    void changesOnDiskReachTheTree()
    {
        QVERIFY(m_model->watched());
        const QString added = m_root.filePath(QStringLiteral("sub/added.txt"));
        writeFile(added);
        QVERIFY(m_root.mkdir(QStringLiteral("made")));
        QVERIFY(QFile::remove(m_root.filePath(QStringLiteral("a.txt"))));

        const QStringList expected{QStringLiteral("made"), QStringLiteral("sub"), QStringLiteral("b.txt")};
        QTRY_COMPARE_WITH_TIMEOUT(childNames(*m_model, m_model->rootIndex()), expected, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_model->existingIndex(added).isValid(), kWaitMs);
        QVERIFY(m_model->isDir(m_model->existingIndex(m_root.filePath(QStringLiteral("made")))));
    }

    // New Folder makes the folder and asks for its row at once, so the name
    // can be typed. The row is a folder, placed among the folders, and
    // cancelling removes the folder from disk again.
    void aNewFolderGetsARowAmongTheFolders()
    {
        QVERIFY(m_root.mkdir(QStringLiteral("untitled folder")));
        const QModelIndex row = m_model->index(m_root.filePath(QStringLiteral("untitled folder")));
        QVERIFY(m_model->isDir(row));
        const QStringList expected{QStringLiteral("sub"), QStringLiteral("untitled folder"), QStringLiteral("a.txt"),
                                   QStringLiteral("b.txt")};
        QCOMPARE(childNames(*m_model, m_model->rootIndex()), expected);

        QVERIFY(m_model->remove(row));
        QVERIFY(!m_root.exists(QStringLiteral("untitled folder")));
    }

private:
    ProcessOrchestrator m_orchestrator;
    std::unique_ptr<QTemporaryDir> m_directory;
    QDir m_root;
    std::unique_ptr<ExplorerModel> m_model;
};

QTEST_MAIN(ExplorerModelTests)

#include "ExplorerModelTests.moc"
