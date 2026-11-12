"""The code index behind codebase_search.

One interface and three backends. `grep` keeps nothing and reads the tree
for every search. `fts` keeps the project's text in SQLite's full-text
index and ranks by the words a query shares with it. `vector` keeps an
embedding of each piece of text and ranks by meaning.

    index = create(kind, root, directory, embedder, list_files)   # kind: "grep", "fts", or "vector"
    index.kind                       the backend in use: "grep" when fts was asked for and SQLite has no FTS5
    index.ready                      False until the first refresh finishes
    index.refresh(on_progress)       brings the index up to date with the disk; on_progress(done, total)
    index.search(query, limit)       -> [(path, first_line, last_line, text, score)], best first
    index.close()                    closes the calling thread's connection; a thread calls it when it is done

`root` is the project folder and `directory` is where the index is kept.
`list_files()` yields the project's files as paths relative to `root`.
`embedder` is from embeddings.py, and is used by `vector` only.

A file is cut into chunks of about 60 lines. `refresh` reads only the
files whose size or modified time changed. An embedding is stored under
the SHA-256 of its chunk's text, not under its file, so text that was
embedded once is never sent again: an edit embeds only the chunks it
changed, and switching branches and back embeds nothing.

`refresh` and `search` may run on different threads. Each thread has its
own SQLite connection, and a refresh that finds another one running
returns at once.
"""

import array
import hashlib
import heapq
import operator
import os
import re
import sqlite3
import sys
import threading

CHUNK_LINES = 60
OVERLAP_LINES = 10
# A chunk ends at a blank line when one is this close above its last line.
BREAK_LINES = 10
MAX_FILE_BYTES = 1024 * 1024
# Chunks embedded and saved together, so a refresh that is stopped keeps what it has done.
EMBED_BATCH = 64

SCHEMA = """
CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY, size INTEGER, mtime INTEGER);
CREATE TABLE IF NOT EXISTS chunks(id INTEGER PRIMARY KEY, path TEXT, first_line INTEGER, last_line INTEGER,
                                  hash TEXT, text TEXT);
CREATE INDEX IF NOT EXISTS chunks_path ON chunks(path);
CREATE INDEX IF NOT EXISTS chunks_hash ON chunks(hash);
CREATE TABLE IF NOT EXISTS meta(kind TEXT, embed_provider TEXT, embed_model TEXT, dimensions INTEGER);
INSERT INTO meta SELECT NULL, NULL, NULL, NULL WHERE NOT EXISTS (SELECT 1 FROM meta);
"""


def create(kind, root, directory, embedder, list_files):
    if kind == "vector":
        return VectorIndex(root, directory, list_files, embedder)
    if kind == "fts":
        if has_fts5():
            return FtsIndex(root, directory, list_files)
        sys.stderr.write("This Python's SQLite has no FTS5, so the index is grep.\n")
    return GrepIndex(root, list_files)


def has_fts5():
    probe = sqlite3.connect(":memory:")
    try:
        probe.execute("CREATE VIRTUAL TABLE probe USING fts5(text)")
        return True
    except sqlite3.OperationalError:
        return False
    finally:
        probe.close()


def _words(query):
    """The words of a query a model wrote, each once, in lower case."""
    return list(dict.fromkeys(word for word in re.findall(r"\w+", query.lower()) if len(word) > 1))


def _existing(root, list_files):
    """Git lists a file deleted from the disk until the deletion is staged, so each path is checked."""
    for path in list_files():
        full = os.path.join(root, path)
        if os.path.isfile(full):
            yield path, full


def _read(full):
    """The file's text, or None for a file that is binary or over 1 MB."""
    if os.path.getsize(full) > MAX_FILE_BYTES:
        return None
    with open(full, "rb") as handle:
        data = handle.read()
    return None if b"\0" in data[:1024] else data.decode("utf-8", "replace")


def _chunks(text):
    """Yields (first_line, last_line, text). Each chunk starts with the last
    lines of the one before it, so a passage cut by a boundary is whole in
    one of the two."""
    lines = text.splitlines()
    start = 0
    while start < len(lines):
        end = min(start + CHUNK_LINES, len(lines))
        if end < len(lines):
            for line in range(end, end - BREAK_LINES, -1):
                if not lines[line - 1].strip():
                    end = line
                    break
        body = "\n".join(lines[start:end])
        if body.strip():
            yield start + 1, end, body
        if end == len(lines):
            break
        start = end - OVERLAP_LINES


class GrepIndex:
    """No stored index. A chunk ranks by how many of the query's words it
    has, then by how often."""

    kind = "grep"
    ready = True

    def __init__(self, root, list_files):
        self.root = root
        self.list_files = list_files

    def refresh(self, on_progress):
        pass

    def close(self):
        pass

    def search(self, query, limit):
        words = _words(query)
        found = []
        for path, full in _existing(self.root, self.list_files):
            text = _read(full)
            if text is None:
                continue
            for first, last, body in _chunks(text):
                lowered = body.lower()
                counts = [lowered.count(word) for word in words]
                matched = sum(1 for count in counts if count)
                if matched:
                    # The words matched, and a fraction that grows with how often.
                    found.append((path, first, last, body, matched + 1 - 1.0 / (1 + sum(counts))))
        return heapq.nlargest(limit, found, key=operator.itemgetter(4))


class _SqliteIndex:
    """The files and their chunks in <directory>/index.sqlite. A backend
    adds its own table and keeps it in step in _store and _forget."""

    kind = None

    def __init__(self, root, directory, list_files):
        self.root = root
        self.list_files = list_files
        self.ready = False
        self._file = os.path.join(directory, "index.sqlite")
        self._local = threading.local()
        self._refreshing = threading.Lock()
        if not os.path.isdir(directory):
            # The default is .crlf/data, so the folders above it are made too.
            os.makedirs(directory)
            # The index is rebuilt from the project, so git has no use for it.
            with open(os.path.join(directory, ".gitignore"), "w", encoding="utf-8") as handle:
                handle.write("*\n")
        db = self._db()
        db.executescript(SCHEMA)
        with db:
            self._prepare(db, db.execute("SELECT kind FROM meta").fetchone()[0])
            db.execute("UPDATE meta SET kind = ?", (self.kind,))

    def _db(self):
        """This thread's connection."""
        db = getattr(self._local, "db", None)
        if db is None:
            db = self._local.db = sqlite3.connect(self._file, timeout=60)
            # A search reads while a refresh writes.
            db.execute("PRAGMA journal_mode = WAL")
            db.execute("PRAGMA synchronous = NORMAL")
            self._connected(db)
        return db

    def close(self):
        db = getattr(self._local, "db", None)
        if db is not None:
            db.close()
            del self._local.db

    def _connected(self, db):
        pass

    def _prepare(self, db, previous_kind):
        """Creates the backend's table. `previous_kind` is the backend that used this file last."""

    def _forget(self, db, path):
        db.execute("DELETE FROM chunks WHERE path = ?", (path,))

    def _store(self, db, path, text):
        db.executemany("INSERT INTO chunks(path, first_line, last_line, hash, text) VALUES (?, ?, ?, ?, ?)",
                       [(path, first, last, hashlib.sha256(body.encode("utf-8")).hexdigest(), body)
                        for first, last, body in _chunks(text)])

    def _finish(self, db, on_progress):
        """What a backend does once the chunks match the disk."""

    def refresh(self, on_progress):
        if not self._refreshing.acquire(blocking=False):
            return
        try:
            db = self._db()
            self._read_changes(db, on_progress)
            self._finish(db, on_progress)
            self.ready = True
        finally:
            self._refreshing.release()

    def _read_changes(self, db, on_progress):
        on_disk = {}
        for path, full in _existing(self.root, self.list_files):
            stat = os.stat(full)
            on_disk[path] = (full, stat.st_size, stat.st_mtime_ns)
        known = {path: (size, mtime) for path, size, mtime in db.execute("SELECT path, size, mtime FROM files")}
        with db:
            for path in known:
                if path not in on_disk:
                    self._forget(db, path)
                    db.execute("DELETE FROM files WHERE path = ?", (path,))
        changed = [path for path in on_disk if known.get(path) != on_disk[path][1:]]
        for done, path in enumerate(changed, 1):
            full, size, mtime = on_disk[path]
            text = _read(full)
            with db:
                self._forget(db, path)
                if text is not None:
                    self._store(db, path, text)
                db.execute("INSERT OR REPLACE INTO files VALUES (?, ?, ?)", (path, size, mtime))
            on_progress(done, len(changed))


class FtsIndex(_SqliteIndex):
    """SQLite's FTS5 over the chunks' text, ranked by BM25. Words are
    stemmed, so "parsing" finds "parse"."""

    kind = "fts"

    def _prepare(self, db, previous_kind):
        db.execute("CREATE VIRTUAL TABLE IF NOT EXISTS fts USING fts5(text, content='chunks', content_rowid='id', "
                   "tokenize='porter unicode61')")
        if previous_kind != self.kind:
            # Another backend changed the chunks and left this table behind.
            db.execute("INSERT INTO fts(fts) VALUES ('rebuild')")

    def _forget(self, db, path):
        db.execute("INSERT INTO fts(fts, rowid, text) SELECT 'delete', id, text FROM chunks WHERE path = ?", (path,))
        super()._forget(db, path)

    def _store(self, db, path, text):
        super()._store(db, path, text)
        db.execute("INSERT INTO fts(rowid, text) SELECT id, text FROM chunks WHERE path = ?", (path,))

    def search(self, query, limit):
        words = _words(query)
        if not words:
            return []
        # The query is free text, and FTS5 has a syntax of its own. Quoted
        # words joined with OR are always a valid expression.
        expression = " OR ".join('"%s"' % word for word in words)
        return self._db().execute(
            "SELECT chunks.path, chunks.first_line, chunks.last_line, chunks.text, -bm25(fts) FROM fts "
            "JOIN chunks ON chunks.id = fts.rowid WHERE fts MATCH ? ORDER BY bm25(fts) LIMIT ?",
            (expression, limit)).fetchall()


def _pack(vector):
    """The vector at length one as 32-bit floats, so the cosine of two stored vectors is their dot product."""
    length = sum(value * value for value in vector) ** 0.5 or 1.0
    return array.array("f", [value / length for value in vector]).tobytes()


class VectorIndex(_SqliteIndex):
    """An embedding for each chunk's text, ranked by cosine similarity to
    the query's. The comparison runs in sqlite-vec when that package is
    installed, else in numpy, else in plain Python."""

    kind = "vector"

    def __init__(self, root, directory, list_files, embedder):
        self.embedder = embedder
        self._numpy = None
        self._sqlite_vec = None
        # The vectors in memory, for numpy and plain Python: (generation, hashes, vectors).
        self._generation = 0
        self._loaded = None
        probe = sqlite3.connect(":memory:")
        try:
            import sqlite_vec
            probe.enable_load_extension(True)
            sqlite_vec.load(probe)
            self._sqlite_vec = sqlite_vec
        except (ImportError, AttributeError, sqlite3.OperationalError):
            # Not installed, or this Python's SQLite cannot load extensions.
            try:
                import numpy
                self._numpy = numpy
            except ImportError:
                pass
        finally:
            probe.close()
        self.comparison = "sqlite-vec" if self._sqlite_vec else "numpy" if self._numpy else "python"
        super().__init__(root, directory, list_files)

    def _connected(self, db):
        if self._sqlite_vec:
            db.enable_load_extension(True)
            self._sqlite_vec.load(db)
            db.enable_load_extension(False)

    def _prepare(self, db, previous_kind):
        db.execute("CREATE TABLE IF NOT EXISTS vectors(hash TEXT PRIMARY KEY, embedding BLOB)")
        names = (self.embedder.provider, self.embedder.model)
        if db.execute("SELECT embed_provider, embed_model FROM meta").fetchone() != names:
            # Vectors from two models cannot be compared, so the old ones go.
            db.execute("DELETE FROM vectors")
            db.execute("UPDATE meta SET embed_provider = ?, embed_model = ?, dimensions = NULL", names)

    def _finish(self, db, on_progress):
        missing = db.execute("SELECT hash, text FROM chunks WHERE hash NOT IN (SELECT hash FROM vectors) "
                             "GROUP BY hash").fetchall()
        for start in range(0, len(missing), EMBED_BATCH):
            batch = missing[start:start + EMBED_BATCH]
            vectors = self.embedder.embed([text for _, text in batch])
            with db:
                db.executemany("INSERT INTO vectors VALUES (?, ?)",
                               [(hash, _pack(vector)) for (hash, _), vector in zip(batch, vectors)])
                db.execute("UPDATE meta SET dimensions = ?", (len(vectors[0]),))
            on_progress(start + len(batch), len(missing))
        # Vectors no chunk uses are kept for the text coming back, as it does
        # when a branch is switched and switched again. They are held to the
        # number in use; past that the oldest go.
        with db:
            in_use = db.execute("SELECT COUNT(DISTINCT hash) FROM chunks").fetchone()[0]
            db.execute("DELETE FROM vectors WHERE hash IN (SELECT hash FROM vectors WHERE hash NOT IN "
                       "(SELECT hash FROM chunks) ORDER BY rowid DESC LIMIT -1 OFFSET ?)", (in_use,))
        self._generation += 1

    def search(self, query, limit):
        db = self._db()
        target = _pack(self.embedder.embed([query])[0])
        results = []
        for hash, score in self._nearest(db, target, limit):
            # The same text in two files is one vector and two results.
            for row in db.execute("SELECT path, first_line, last_line, text FROM chunks WHERE hash = ?", (hash,)):
                results.append(row + (score,))
        return results[:limit]

    def _nearest(self, db, target, limit):
        """[(hash, cosine similarity)] of the chunks nearest the query, best first."""
        in_use = "FROM vectors WHERE hash IN (SELECT hash FROM chunks)"
        if self._sqlite_vec:
            return db.execute("SELECT hash, 1 - vec_distance_cosine(embedding, ?) AS score %s "
                              "ORDER BY score DESC LIMIT ?" % in_use, (target, limit)).fetchall()
        generation = self._generation
        if self._loaded is None or self._loaded[0] != generation:
            rows = db.execute("SELECT hash, embedding %s" % in_use).fetchall()
            hashes = [hash for hash, _ in rows]
            if self._numpy and rows:
                vectors = self._numpy.frombuffer(b"".join(blob for _, blob in rows),
                                                 dtype=self._numpy.float32).reshape(len(rows), -1)
            else:
                vectors = [array.array("f", blob) for _, blob in rows]
            self._loaded = (generation, hashes, vectors)
        _, hashes, vectors = self._loaded
        if not hashes:
            return []
        if self._numpy:
            scores = vectors @ self._numpy.frombuffer(target, dtype=self._numpy.float32)
            return [(hashes[row], float(scores[row])) for row in self._numpy.argsort(-scores)[:limit]]
        wanted = array.array("f", target)
        scores = [sum(map(operator.mul, vector, wanted)) for vector in vectors]
        return heapq.nlargest(limit, zip(hashes, scores), key=operator.itemgetter(1))
