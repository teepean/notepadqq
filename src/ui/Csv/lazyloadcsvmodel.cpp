#include "include/Csv/lazyloadcsvmodel.h"
#include "include/Csv/csvparser.h"
#include <QFile>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QColor>
#include <QFont>

// Define static const member
const qint64 LazyLoadCsvModel::CHUNK_SIZE;

LazyLoadCsvModel::LazyLoadCsvModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_fileSize(0)
    , m_maxMappedChunks(3)
    , m_rowCount(0)
    , m_columnCount(0)
    , m_maxCacheSize(1000)
    , m_cacheHits(0)
    , m_cacheMisses(0)
    , m_lineEnding(LineEnding::LF)
    , m_encoding("UTF-8")
    , m_delimiter(',')
    , m_quoteChar('"')
    , m_hasHeader(true)
{
    m_parser = new CsvParser();
}

LazyLoadCsvModel::~LazyLoadCsvModel()
{
    unmapAllChunks();

    if (m_file.isOpen()) {
        m_file.close();
    }

    delete m_parser;
}

bool LazyLoadCsvModel::loadFromFile(const QString &path, char delimiter)
{
    qDebug() << "LazyLoadCsvModel: Loading file:" << path;
    emit loadingStarted();

    m_filePath = path;
    m_comments.clear();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << path;
        emit loadingFinished(false);
        return false;
    }

    m_fileSize = m_file.size();
    qDebug() << "File size:" << m_fileSize << "bytes";

    detectMetadata();

    if (delimiter == '\0') {
        qint64 sampleSize = qMin(m_fileSize, qint64(10240));
        uchar *sampleData = getDataAt(0, sampleSize);
        if (!sampleData) {
            qWarning() << "Failed to read sample for delimiter detection";
            m_file.close();
            emit loadingFinished(false);
            return false;
        }
        QByteArray sample((const char*)sampleData, sampleSize);
        QString sampleStr = QString::fromUtf8(sample);

        m_delimiter = CsvParser::detectDelimiterFromString(sampleStr);
        qDebug() << "Auto-detected delimiter:" << m_delimiter;
    } else {
        m_delimiter = delimiter;
    }

    CsvParser::Options options;
    options.delimiter = m_delimiter;
    options.quoteChar = m_quoteChar;
    options.hasHeader = m_hasHeader;
    m_parser->setOptions(options);

    qDebug() << "Building row index...";
    if (!buildRowIndex()) {
        qWarning() << "Failed to build row index";
        unmapAllChunks();
        m_file.close();
        emit loadingFinished(false);
        return false;
    }

    qDebug() << "Row index built:" << m_rowCount << "rows indexed";

    if (m_hasHeader && m_rowCount > 0) {
        m_headers = parseRowAt(0);
        m_columnCount = m_headers.size();
        qDebug() << "Header parsed:" << m_columnCount << "columns";

        m_rowOffsets.removeFirst();
        m_rowCount--;
    } else {
        if (m_rowCount > 0) {
            QStringList firstRow = parseRowAt(0);
            m_columnCount = firstRow.size();
        }
    }

    m_dirty = false;

    qDebug() << "File loaded successfully using lazy loading";
    qDebug() << "Rows:" << m_rowCount << "Columns:" << m_columnCount;

    emit loadingFinished(true);
    return true;
}

bool LazyLoadCsvModel::buildRowIndex()
{
    m_rowOffsets.clear();
    m_rowOffsets.reserve(10000);

    m_rowOffsets.append(0);

    qint64 progressInterval = m_fileSize / 100;
    qint64 nextProgress = progressInterval;
    int progressPercent = 0;

    bool inQuotes = false;
    char prevChar = '\0';

    qint64 numChunks = (m_fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    for (qint64 chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx) {
        mapChunk(chunkIdx);

        if (!m_mappedChunks.contains(chunkIdx)) {
            qWarning() << "Failed to map chunk" << chunkIdx;
            return false;
        }

        const MappedChunk &chunk = m_mappedChunks[chunkIdx];
        qint64 chunkStart = chunk.fileOffset;
        qint64 chunkSize = chunk.size;
        const uchar *chunkData = chunk.data;

        for (qint64 i = 0; i < chunkSize; ++i) {
            qint64 fileOffset = chunkStart + i;
            char c = chunkData[i];

            if (c == m_quoteChar) {
                if (!inQuotes || prevChar != '\\') {
                    inQuotes = !inQuotes;
                }
            }

            if (!inQuotes) {
                if (c == '\n') {
                    if (fileOffset + 1 < m_fileSize) {
                        m_rowOffsets.append(fileOffset + 1);
                    }
                } else if (c == '\r') {
                    if (fileOffset + 1 < m_fileSize) {
                        char nextChar;
                        if (i + 1 < chunkSize) {
                            nextChar = chunkData[i + 1];
                        } else {
                            uchar *nextData = getDataAt(fileOffset + 1, 1);
                            nextChar = nextData ? *nextData : '\0';
                        }

                        if (nextChar != '\n') {
                            m_rowOffsets.append(fileOffset + 1);
                        }
                    }
                }
            }

            prevChar = c;

            if (fileOffset >= nextProgress) {
                progressPercent++;
                emit loadingProgress(progressPercent);
                nextProgress += progressInterval;
            }
        }
    }

    m_rowCount = m_rowOffsets.size();

    return true;
}

void LazyLoadCsvModel::detectMetadata()
{
    qint64 sampleSize = qMin(m_fileSize, qint64(65536));
    uchar *sampleData = getDataAt(0, sampleSize);
    if (!sampleData) {
        return;
    }
    QByteArray sample((const char*)sampleData, sampleSize);

    m_lineEnding = detectLineEnding(sample);
    m_encoding = detectEncoding(sample);
}

LazyLoadCsvModel::LineEnding LazyLoadCsvModel::detectLineEnding(const QByteArray &sample) const
{
    int crlfCount = sample.count("\r\n");
    int lfCount = sample.count('\n') - crlfCount;
    int crCount = sample.count('\r') - crlfCount;

    if (crlfCount >= lfCount && crlfCount >= crCount && crlfCount > 0) {
        return LineEnding::CRLF;
    } else if (crCount > lfCount && crCount > 0) {
        return LineEnding::CR;
    } else {
        return LineEnding::LF;
    }
}

QString LazyLoadCsvModel::detectEncoding(const QByteArray &sample) const
{
    if (sample.startsWith("\xEF\xBB\xBF")) {
        return "UTF-8 BOM";
    } else if (sample.startsWith("\xFF\xFE")) {
        return "UTF-16 LE";
    } else if (sample.startsWith("\xFE\xFF")) {
        return "UTF-16 BE";
    }

    QString test = QString::fromUtf8(sample);
    if (!test.contains(QChar::ReplacementCharacter)) {
        return "UTF-8";
    }

    bool isPureAscii = true;
    for (char c : sample) {
        if (static_cast<unsigned char>(c) > 127) {
            isPureAscii = false;
            break;
        }
    }

    if (isPureAscii) {
        return "ASCII";
    }

    return "Latin-1";
}

QString LazyLoadCsvModel::lineEndingString() const
{
    switch (m_lineEnding) {
    case LineEnding::LF:
        return "LF";
    case LineEnding::CRLF:
        return "CRLF";
    case LineEnding::CR:
        return "CR";
    default:
        return "LF";
    }
}

qint64 LazyLoadCsvModel::rowStartOffset(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_rowOffsets.size())
        return -1;
    return m_rowOffsets[rowIndex];
}

qint64 LazyLoadCsvModel::rowEndOffset(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_rowOffsets.size())
        return -1;

    if (rowIndex + 1 < m_rowOffsets.size()) {
        return m_rowOffsets[rowIndex + 1] - 1;
    } else {
        return m_fileSize;
    }
}

QStringList LazyLoadCsvModel::parseRowAt(int rowIndex) const
{
    qint64 start = rowStartOffset(rowIndex);
    qint64 end = rowEndOffset(rowIndex);

    if (start < 0 || end < 0 || start >= end)
        return QStringList();

    qint64 length = end - start;

    if (length > 0) {
        uchar *endData = getDataAt(end - 1, 1);
        if (endData && *endData == '\r') {
            length--;
        }
    }
    if (length > 0) {
        uchar *data = getDataAt(start + length - 1, 1);
        if (data && *data == '\r') {
            length--;
        }
    }

    uchar *rowDataPtr = getDataAt(start, length);
    if (!rowDataPtr) {
        return QStringList();
    }

    QByteArray rowData((const char*)rowDataPtr, length);
    QString line = QString::fromUtf8(rowData);

    return m_parser->parseLine(line);
}

QStringList LazyLoadCsvModel::parseCsvLine(const QString &line, char delimiter) const
{
    Q_UNUSED(delimiter);
    return m_parser->parseLine(line);
}

void LazyLoadCsvModel::loadRow(int rowIndex) const
{
    if (m_rowCache.contains(rowIndex)) {
        m_cacheHits++;
        m_rowCache[rowIndex].lastAccessTime = QDateTime::currentMSecsSinceEpoch();
        return;
    }

    m_cacheMisses++;

    QStringList cells = parseRowAt(rowIndex);

    if (m_rowCache.size() >= m_maxCacheSize) {
        evictOldestCachedRow();
    }

    CachedRow cachedRow;
    cachedRow.cells = cells;
    cachedRow.lastAccessTime = QDateTime::currentMSecsSinceEpoch();
    m_rowCache[rowIndex] = cachedRow;
}

void LazyLoadCsvModel::evictOldestCachedRow() const
{
    if (m_rowCache.isEmpty())
        return;

    int oldestRow = -1;
    qint64 oldestTime = LLONG_MAX;

    for (auto it = m_rowCache.begin(); it != m_rowCache.end(); ++it) {
        if (it.value().lastAccessTime < oldestTime) {
            oldestTime = it.value().lastAccessTime;
            oldestRow = it.key();
        }
    }

    if (oldestRow >= 0) {
        m_rowCache.remove(oldestRow);
    }
}

int LazyLoadCsvModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rowCount;
}

int LazyLoadCsvModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_columnCount;
}

QVariant LazyLoadCsvModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= m_rowCount || col < 0 || col >= m_columnCount)
        return QVariant();

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        {
            int originalRowCount = m_rowOffsets.size();

            if (row >= originalRowCount) {
                int insertedIndex = row - originalRowCount;
                if (insertedIndex >= 0 && insertedIndex < m_insertedRows.size()) {
                    const InsertedRow &insertedRow = m_insertedRows[insertedIndex];
                    if (col < insertedRow.cells.size()) {
                        return insertedRow.cells[col];
                    }
                }
                return QVariant();
            }

            if (m_modifiedRows.contains(row)) {
                const ModifiedRow &modRow = m_modifiedRows[row];
                if (col < modRow.cells.size()) {
                    return modRow.cells[col];
                }
            }

            loadRow(row);

            if (m_rowCache.contains(row)) {
                const QStringList &cells = m_rowCache[row].cells;
                if (col < cells.size()) {
                    return cells[col];
                }
            }
        }
        break;

    case Qt::BackgroundRole:
        if (row % 2 == 0) {
            return QColor(255, 255, 255);
        } else {
            return QColor(245, 248, 255);
        }
        break;

    case Qt::TextAlignmentRole:
        if (m_modifiedRows.contains(row)) {
            const ModifiedRow &modRow = m_modifiedRows[row];
            if (col < modRow.cells.size()) {
                QString value = modRow.cells[col];
                bool isNumber;
                value.toDouble(&isNumber);
                if (isNumber) {
                    return QVariant(Qt::AlignRight | Qt::AlignVCenter);
                }
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }

        loadRow(row);
        if (m_rowCache.contains(row)) {
            const QStringList &cells = m_rowCache[row].cells;
            if (col < cells.size()) {
                QString value = cells[col];
                bool isNumber;
                value.toDouble(&isNumber);
                if (isNumber) {
                    return QVariant(Qt::AlignRight | Qt::AlignVCenter);
                }
            }
        }
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        break;
    }

    return QVariant();
}

QVariant LazyLoadCsvModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            if (m_hasHeader && section < m_headers.size()) {
                return m_headers[section];
            }
            return QString("Column %1").arg(section + 1);
        } else {
            return section + 1;
        }
    }

    if (role == Qt::BackgroundRole && orientation == Qt::Horizontal) {
        return QColor(230, 230, 230);
    }

    if (role == Qt::FontRole && orientation == Qt::Horizontal) {
        QFont font;
        font.setBold(true);
        return font;
    }

    return QVariant();
}

Qt::ItemFlags LazyLoadCsvModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool LazyLoadCsvModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= m_rowCount || col < 0 || col >= m_columnCount)
        return false;

    int originalRowCount = m_rowOffsets.size();

    if (row >= originalRowCount) {
        int insertedIndex = row - originalRowCount;
        if (insertedIndex >= 0 && insertedIndex < m_insertedRows.size()) {
            InsertedRow &insertedRow = m_insertedRows[insertedIndex];
            if (col >= insertedRow.cells.size()) {
                while (insertedRow.cells.size() < col + 1)
                    insertedRow.cells.append(QString());
            }
            insertedRow.cells[col] = value.toString();
            markDirty();
            emit dataChanged(index, index, {role});
            return true;
        }
        return false;
    }

    loadRow(row);

    QStringList originalCells;
    if (m_rowCache.contains(row)) {
        originalCells = m_rowCache[row].cells;
    } else {
        return false;
    }

    if (!m_modifiedRows.contains(row)) {
        ModifiedRow modRow;
        modRow.cells = originalCells;
        modRow.modificationTime = QDateTime::currentMSecsSinceEpoch();
        m_modifiedRows[row] = modRow;
    }

    ModifiedRow &modRow = m_modifiedRows[row];

    if (col >= modRow.cells.size()) {
        while (modRow.cells.size() < col + 1)
            modRow.cells.append(QString());
    }

    modRow.cells[col] = value.toString();
    modRow.modifiedColumns.insert(col);
    modRow.modificationTime = QDateTime::currentMSecsSinceEpoch();

    if (m_rowCache.contains(row)) {
        m_rowCache[row].cells = modRow.cells;
    }

    markDirty();
    emit dataChanged(index, index, {role});
    return true;
}

bool LazyLoadCsvModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (count <= 0 || row < 0 || row > m_rowCount)
        return false;

    beginInsertRows(parent, row, row + count - 1);

    for (int i = 0; i < count; ++i) {
        InsertedRow newRow;
        while (newRow.cells.size() < m_columnCount)
            newRow.cells.append(QString());
        newRow.insertionTime = QDateTime::currentMSecsSinceEpoch();
        m_insertedRows.append(newRow);
    }

    m_rowCount += count;
    endInsertRows();
    markDirty();

    return true;
}

bool LazyLoadCsvModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (count <= 0 || row < 0 || row + count > m_rowCount)
        return false;

    beginRemoveRows(parent, row, row + count - 1);

    for (int i = row; i < row + count; ++i) {
        m_deletedRows.insert(i);
        m_modifiedRows.remove(i);
        m_rowCache.remove(i);
    }

    m_rowCount -= count;
    endRemoveRows();
    markDirty();

    return true;
}

bool LazyLoadCsvModel::saveToFile(const QString &path, char delimiter, LineEnding lineEnding)
{
    QFile outFile(path);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for writing:" << path;
        return false;
    }

    QTextStream out(&outFile);

    QString lineEndStr;
    switch (lineEnding) {
    case LineEnding::CRLF:
        lineEndStr = "\r\n";
        break;
    case LineEnding::CR:
        lineEndStr = "\r";
        break;
    case LineEnding::LF:
    default:
        lineEndStr = "\n";
        break;
    }

    if (m_hasHeader && !m_headers.isEmpty()) {
        for (int col = 0; col < m_columnCount; ++col) {
            if (col > 0) {
                out << delimiter;
            }
            if (col < m_headers.size()) {
                out << escapeCsvField(m_headers[col], delimiter);
            }
        }
        out << lineEndStr;
    }

    int savedRows = 0;
    int originalRowCount = m_rowOffsets.size();

    for (int row = 0; row < originalRowCount; ++row) {
        if (m_deletedRows.contains(row))
            continue;

        QStringList cells;

        if (m_modifiedRows.contains(row)) {
            cells = m_modifiedRows[row].cells;
        } else {
            cells = parseRowAt(row);
        }

        for (int col = 0; col < cells.size(); ++col) {
            if (col > 0) {
                out << delimiter;
            }
            out << escapeCsvField(cells[col], delimiter);
        }
        out << lineEndStr;
        savedRows++;
    }

    for (const InsertedRow &insertedRow : m_insertedRows) {
        for (int col = 0; col < insertedRow.cells.size(); ++col) {
            if (col > 0) {
                out << delimiter;
            }
            out << escapeCsvField(insertedRow.cells[col], delimiter);
        }
        out << lineEndStr;
        savedRows++;
    }

    outFile.close();

    m_modifiedRows.clear();
    m_deletedRows.clear();
    m_insertedRows.clear();
    m_dirty = false;
    emit dirtyChanged(false);

    return true;
}

void LazyLoadCsvModel::setCacheSize(int size)
{
    m_maxCacheSize = size;
}

void LazyLoadCsvModel::clearCache()
{
    m_rowCache.clear();
    m_cacheHits = 0;
    m_cacheMisses = 0;
}

double LazyLoadCsvModel::cacheHitRate() const
{
    int total = m_cacheHits + m_cacheMisses;
    if (total == 0)
        return 0.0;
    return (m_cacheHits * 100.0) / total;
}

QString LazyLoadCsvModel::escapeCsvField(const QString &field, char delimiter) const
{
    if (field.contains(delimiter) || field.contains(m_quoteChar) ||
        field.contains('\n') || field.contains('\r')) {

        QString escaped = field;
        escaped.replace(m_quoteChar, QString(m_quoteChar) + m_quoteChar);
        return m_quoteChar + escaped + m_quoteChar;
    }

    return field;
}

uchar* LazyLoadCsvModel::getDataAt(qint64 offset, qint64 length) const
{
    Q_UNUSED(length);

    if (offset < 0 || offset >= m_fileSize)
        return nullptr;

    qint64 chunkIndex = offset / CHUNK_SIZE;

    if (!m_mappedChunks.contains(chunkIndex)) {
        mapChunk(chunkIndex);
    }

    m_mappedChunks[chunkIndex].lastAccessTime = QDateTime::currentMSecsSinceEpoch();

    qint64 chunkOffset = offset % CHUNK_SIZE;

    return m_mappedChunks[chunkIndex].data + chunkOffset;
}

void LazyLoadCsvModel::mapChunk(qint64 chunkIndex) const
{
    if (m_mappedChunks.size() >= m_maxMappedChunks) {
        unmapOldestChunk();
    }

    qint64 fileOffset = chunkIndex * CHUNK_SIZE;
    qint64 chunkSize = qMin(CHUNK_SIZE, m_fileSize - fileOffset);

    if (chunkSize <= 0)
        return;

    uchar *data = m_file.map(fileOffset, chunkSize);
    if (!data) {
        qWarning() << "Failed to map chunk" << chunkIndex << "at offset" << fileOffset;
        return;
    }

    MappedChunk chunk;
    chunk.chunkIndex = chunkIndex;
    chunk.fileOffset = fileOffset;
    chunk.size = chunkSize;
    chunk.data = data;
    chunk.lastAccessTime = QDateTime::currentMSecsSinceEpoch();

    m_mappedChunks[chunkIndex] = chunk;
}

void LazyLoadCsvModel::unmapChunk(qint64 chunkIndex) const
{
    if (!m_mappedChunks.contains(chunkIndex))
        return;

    MappedChunk chunk = m_mappedChunks[chunkIndex];
    if (chunk.data) {
        m_file.unmap(chunk.data);
    }

    m_mappedChunks.remove(chunkIndex);
}

void LazyLoadCsvModel::unmapOldestChunk() const
{
    if (m_mappedChunks.isEmpty())
        return;

    qint64 oldestChunk = -1;
    qint64 oldestTime = LLONG_MAX;

    for (auto it = m_mappedChunks.begin(); it != m_mappedChunks.end(); ++it) {
        if (it.value().lastAccessTime < oldestTime) {
            oldestTime = it.value().lastAccessTime;
            oldestChunk = it.key();
        }
    }

    if (oldestChunk >= 0) {
        unmapChunk(oldestChunk);
    }
}

void LazyLoadCsvModel::unmapAllChunks()
{
    for (auto it = m_mappedChunks.begin(); it != m_mappedChunks.end(); ++it) {
        if (it.value().data) {
            m_file.unmap(it.value().data);
        }
    }

    m_mappedChunks.clear();
}
