#ifndef LAZYLOADCSVMODEL_H
#define LAZYLOADCSVMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QStringList>
#include <QFile>
#include <QMap>
#include <QSet>

class CsvParser;

/**
 * LazyLoadCsvModel - Memory-efficient CSV model for large files
 *
 * Uses memory-mapped files and on-demand row parsing to handle
 * files that would be too large to load entirely into memory.
 */
class LazyLoadCsvModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit LazyLoadCsvModel(QObject *parent = nullptr);
    ~LazyLoadCsvModel();

    // Line ending types
    enum class LineEnding {
        LF,
        CRLF,
        CR
    };

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // File operations
    bool loadFromFile(const QString &path, char delimiter = '\0');
    bool saveToFile(const QString &path, char delimiter = ',', LineEnding lineEnding = LineEnding::LF);

    // Configuration
    void setDelimiter(char delimiter) { m_delimiter = delimiter; }
    char delimiter() const { return m_delimiter; }
    void setHasHeader(bool hasHeader) { m_hasHeader = hasHeader; }
    bool hasHeader() const { return m_hasHeader; }

    // Cache control
    void setCacheSize(int size);
    int cacheSize() const { return m_maxCacheSize; }
    void clearCache();

    // Statistics
    qint64 fileSize() const { return m_fileSize; }
    int totalRows() const { return m_rowCount; }
    int totalColumns() const { return m_columnCount; }
    QString filePath() const { return m_filePath; }
    QStringList comments() const { return m_comments; }

    // Line ending and encoding info
    LineEnding lineEnding() const { return m_lineEnding; }
    QString lineEndingString() const;
    QString encoding() const { return m_encoding; }

    // Cache statistics
    int cacheHits() const { return m_cacheHits; }
    int cacheMisses() const { return m_cacheMisses; }
    double cacheHitRate() const;

    // Dirty tracking
    bool isDirty() const { return m_dirty; }
    void setClean() { m_dirty = false; emit dirtyChanged(false); }

signals:
    void loadingStarted();
    void loadingProgress(int percent);
    void loadingFinished(bool success);
    void dirtyChanged(bool dirty);

private:
    bool buildRowIndex();
    void detectMetadata();
    LineEnding detectLineEnding(const QByteArray &sample) const;
    QString detectEncoding(const QByteArray &sample) const;

    QStringList parseRowAt(int rowIndex) const;
    QStringList parseCsvLine(const QString &line, char delimiter) const;
    void loadRow(int rowIndex) const;
    void evictOldestCachedRow() const;

    QString escapeCsvField(const QString &field, char delimiter) const;
    qint64 rowStartOffset(int rowIndex) const;
    qint64 rowEndOffset(int rowIndex) const;

    // Chunked file reading
    struct MappedChunk {
        qint64 chunkIndex;
        qint64 fileOffset;
        qint64 size;
        uchar *data;
        mutable qint64 lastAccessTime;
    };

    uchar* getDataAt(qint64 offset, qint64 length) const;
    void mapChunk(qint64 chunkIndex) const;
    void unmapChunk(qint64 chunkIndex) const;
    void unmapOldestChunk() const;
    void unmapAllChunks();

    // File data
    mutable QFile m_file;
    qint64 m_fileSize;
    QString m_filePath;

    static const qint64 CHUNK_SIZE = 10 * 1024 * 1024;  // 10 MB chunks
    mutable QMap<qint64, MappedChunk> m_mappedChunks;
    int m_maxMappedChunks;

    // Row index
    QVector<qint64> m_rowOffsets;
    int m_rowCount;
    int m_columnCount;

    // Row cache (LRU)
    struct CachedRow {
        QStringList cells;
        mutable qint64 lastAccessTime;
    };
    mutable QMap<int, CachedRow> m_rowCache;
    mutable int m_maxCacheSize;
    mutable int m_cacheHits;
    mutable int m_cacheMisses;

    // Modified rows
    struct ModifiedRow {
        QStringList cells;
        QSet<int> modifiedColumns;
        qint64 modificationTime;
    };
    mutable QMap<int, ModifiedRow> m_modifiedRows;

    // Deleted rows
    QSet<int> m_deletedRows;

    // Inserted rows
    struct InsertedRow {
        QStringList cells;
        qint64 insertionTime;
    };
    QVector<InsertedRow> m_insertedRows;

    // Metadata
    QStringList m_headers;
    QStringList m_comments;
    LineEnding m_lineEnding;
    QString m_encoding;

    // CSV settings
    char m_delimiter;
    char m_quoteChar;
    bool m_hasHeader;

    // Dirty tracking
    bool m_dirty = false;
    void markDirty() { if (!m_dirty) { m_dirty = true; emit dirtyChanged(true); } }

    // Parser
    CsvParser *m_parser;
};

#endif // LAZYLOADCSVMODEL_H
