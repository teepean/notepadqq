#ifndef CSVMODEL_H
#define CSVMODEL_H

#include <QAbstractTableModel>
#include <QVector>
#include <QStringList>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QUndoStack>

class CsvParser;

class CsvModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit CsvModel(QObject *parent = nullptr);
    ~CsvModel();

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    // Line ending types
    enum class LineEnding {
        LF,      // Unix/Linux (\n)
        CRLF,    // Windows (\r\n)
        CR       // Old Mac (\r)
    };

    // CSV-specific methods
    bool loadFromFile(const QString &path, char delimiter = '\0');
    bool loadFromString(const QString &content, char delimiter = ',');
    bool saveToFile(const QString &path, char delimiter = ',', LineEnding lineEnding = LineEnding::LF);
    void clear();

    void setDelimiter(char delimiter);
    char delimiter() const { return m_delimiter; }

    void setHasHeader(bool hasHeader);
    bool hasHeader() const { return m_hasHeader; }
    void toggleFirstRowAsHeader(bool useFirstRowAsHeader);

    // Row operations
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // Column operations
    bool insertColumns(int column, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeColumns(int column, int count, const QModelIndex &parent = QModelIndex()) override;
    void reorderColumns(const QList<int> &newOrder);
    void duplicateColumn(int column, const QString &newName = QString());
    void combineColumns(const QList<int> &columns, const QString &delimiter, const QString &newName);
    void splitColumn(int column, const QString &delimiter);
    void hideColumn(int column);
    void showColumn(int column);
    QList<int> hiddenColumns() const { return m_hiddenColumns; }

    // Data operations
    void sortByColumn(int column, Qt::SortOrder order = Qt::AscendingOrder);
    void removeDuplicateRows();
    void removeDuplicateRows(const QList<int> &columns);
    int findInColumn(const QString &pattern, int column, int startRow = 0, bool useRegex = false);
    int replaceInColumn(const QString &pattern, const QString &replacement,
                        int column, bool useRegex = false);

    // Filtering
    void setFilter(const QString &pattern, int column = -1, bool useRegex = false);
    void clearFilter();
    bool isFiltered() const { return m_isFiltered; }
    int visibleRowCount() const;

    // Statistics
    qint64 fileSize() const { return m_fileSize; }
    int totalRows() const { return m_data.size(); }
    int totalColumns() const { return m_columnCount; }

    QString filePath() const { return m_filePath; }
    QStringList comments() const { return m_comments; }

    // Line ending and encoding info
    LineEnding lineEnding() const { return m_lineEnding; }
    QString lineEndingString() const;
    QString encoding() const { return m_encoding; }

    // Dirty tracking
    bool isDirty() const { return m_dirty; }
    void setClean() { m_dirty = false; emit dirtyChanged(false); }

    // Undo/Redo
    QUndoStack *undoStack() { return &m_undoStack; }

    // Internal methods used by undo commands (bypass undo stack)
    void setDataInternal(int row, int col, const QString &value);
    bool insertRowsInternal(int row, int count);
    bool removeRowsInternal(int row, int count);
    bool insertColumnsInternal(int column, int count);
    bool removeColumnsInternal(int column, int count);
    void setHeaderDataInternal(int section, const QString &value);
    void sortByColumnInternal(int column, Qt::SortOrder order);
    void restoreDataInternal(const QVector<QStringList> &data);

    // Access internal data for undo snapshots
    QVector<QStringList> dataSnapshot() const { return m_data; }
    QStringList rowData(int row) const;
    QStringList columnData(int column) const;
    QStringList headerSnapshot() const { return m_headers; }

    // Row flagging
    void toggleRowFlag(int row);
    void flagRows(const QList<int> &rows);
    void unflagAll();
    void invertFlags();
    bool isFlagged(int row) const { return m_flaggedRows.contains(row); }
    QSet<int> flaggedRows() const { return m_flaggedRows; }
    int flaggedRowCount() const { return m_flaggedRows.size(); }
    void deleteFlaggedRows();
    void keepOnlyFlaggedRows();

signals:
    void loadingStarted();
    void loadingFinished(bool success);
    void dirtyChanged(bool dirty);

private:
    void markDirty() { if (!m_dirty) { m_dirty = true; emit dirtyChanged(true); } }

    bool m_dirty = false;
    QString escapeCsvField(const QString &field, char delimiter) const;
    bool rowMatchesFilter(int row) const;
    LineEnding detectLineEnding(const QByteArray &data) const;
    QString detectEncoding(const QByteArray &data) const;

    // Data storage
    QVector<QStringList> m_data;
    QStringList m_headers;
    QStringList m_comments;

    // Filtering
    bool m_isFiltered;
    QString m_filterPattern;
    int m_filterColumn;
    bool m_filterUseRegex;
    QVector<int> m_visibleRows;

    // File info
    QString m_filePath;
    qint64 m_fileSize;
    LineEnding m_lineEnding;
    QString m_encoding;

    // CSV settings
    char m_delimiter;
    char m_quoteChar;
    bool m_hasHeader;

    // Cache
    int m_columnCount;
    QList<int> m_hiddenColumns;

    // Undo
    QUndoStack m_undoStack;
    bool m_undoInProgress = false;

    // Row flagging
    QSet<int> m_flaggedRows;

    // Parser
    CsvParser *m_parser;
};

#endif // CSVMODEL_H
