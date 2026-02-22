#include "include/Csv/csvmodel.h"
#include "include/Csv/csvparser.h"
#include <QFile>
#include <QStringConverter>
#include <QTextStream>
#include <QDebug>
#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QSet>
#include <QTextCodec>
#include <algorithm>

CsvModel::CsvModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_fileSize(0)
    , m_lineEnding(LineEnding::LF)
    , m_encoding("UTF-8")
    , m_delimiter(',')
    , m_quoteChar('"')
    , m_hasHeader(true)
    , m_columnCount(0)
    , m_isFiltered(false)
    , m_filterColumn(-1)
    , m_filterUseRegex(false)
{
    m_parser = new CsvParser();
}

CsvModel::~CsvModel()
{
    delete m_parser;
}

int CsvModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_isFiltered ? m_visibleRows.size() : m_data.size();
}

int CsvModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_columnCount;
}

QVariant CsvModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    int row = index.row();
    const int col = index.column();

    // Map to actual row if filtered
    if (m_isFiltered) {
        if (row < 0 || row >= m_visibleRows.size())
            return QVariant();
        row = m_visibleRows[row];
    }

    if (row < 0 || row >= m_data.size() || col < 0 || col >= m_columnCount)
        return QVariant();

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        if (row < m_data.size() && col < m_data[row].size()) {
            return m_data[row][col];
        }
        break;

    case Qt::BackgroundRole:
        // Alternating row colors
        if (row % 2 == 0) {
            return QColor(255, 255, 255);
        } else {
            return QColor(245, 248, 255);
        }
        break;

    case Qt::TextAlignmentRole:
        // Right-align numbers
        if (row < m_data.size() && col < m_data[row].size()) {
            QString value = m_data[row][col];
            bool isNumber;
            value.toDouble(&isNumber);
            if (isNumber) {
                return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            }
        }
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        break;
    }

    return QVariant();
}

QVariant CsvModel::headerData(int section, Qt::Orientation orientation, int role) const
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

Qt::ItemFlags CsvModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool CsvModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;

    const int row = index.row();
    const int col = index.column();

    if (row < 0 || row >= m_data.size() || col < 0 || col >= m_columnCount)
        return false;

    if (col >= m_data[row].size()) {
        while (m_data[row].size() < m_columnCount)
            m_data[row].append(QString());
    }
    m_data[row][col] = value.toString();

    emit dataChanged(index, index, {role});
    return true;
}

bool CsvModel::loadFromFile(const QString &path, char delimiter)
{
    qDebug() << "Loading CSV file:" << path;
    emit loadingStarted();

    m_filePath = path;
    m_comments.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << path;
        emit loadingFinished(false);
        return false;
    }

    QByteArray fileData = file.readAll();
    m_fileSize = fileData.size();
    file.close();
    qDebug() << "File size:" << m_fileSize << "bytes";

    m_lineEnding = detectLineEnding(fileData);
    qDebug() << "Detected line ending:" << lineEndingString();

    m_encoding = detectEncoding(fileData);
    qDebug() << "Detected encoding:" << m_encoding;

    QString fileContent;
    if (m_encoding == "UTF-8" || m_encoding == "UTF-8 BOM") {
        if (m_encoding == "UTF-8 BOM" && fileData.startsWith("\xEF\xBB\xBF")) {
            fileData = fileData.mid(3);
        }
        fileContent = QString::fromUtf8(fileData);
    } else if (m_encoding == "UTF-16 LE") {
        fileContent = QString::fromUtf16(reinterpret_cast<const char16_t*>(fileData.constData() + 2));
    } else if (m_encoding == "UTF-16 BE") {
        fileContent = QString::fromUtf8(fileData);
    } else if (m_encoding == "Latin-1") {
        fileContent = QString::fromLatin1(fileData);
    } else {
        fileContent = QString::fromUtf8(fileData);
    }

    QStringList allLines = fileContent.split(QRegularExpression("\\r\\n|\\r|\\n"));

    QStringList nonCommentLines;
    QString sampleForDelimiter;
    int lineCount = 0;

    for (const QString &line : allLines) {
        lineCount++;

        if (line.trimmed().startsWith('#')) {
            m_comments.append(line);
            if (lineCount <= 20) {
                qDebug() << "Comment line" << lineCount << ":" << line.left(50);
            }
        } else if (!line.trimmed().isEmpty()) {
            nonCommentLines.append(line);
            if (sampleForDelimiter.length() < 10000) {
                sampleForDelimiter += line + "\n";
            }
        }
    }

    if (m_comments.size() > 20) {
        qDebug() << "Found" << m_comments.size() << "comment lines (showing first 20)";
    } else {
        qDebug() << "Found" << m_comments.size() << "comment lines";
    }

    if (delimiter == '\0') {
        m_delimiter = CsvParser::detectDelimiterFromString(sampleForDelimiter);
        qDebug() << "Auto-detected delimiter:" << m_delimiter;
    } else {
        m_delimiter = delimiter;
    }

    CsvParser::Options options;
    options.delimiter = m_delimiter;
    options.quoteChar = m_quoteChar;
    options.hasHeader = m_hasHeader;
    m_parser->setOptions(options);

    beginResetModel();

    m_data.clear();
    for (const QString &line : nonCommentLines) {
        if (!line.trimmed().isEmpty()) {
            QStringList row = m_parser->parseLine(line);
            m_data.append(row);
        }
    }

    if (!m_data.isEmpty()) {
        if (m_hasHeader && !m_data.isEmpty()) {
            m_headers = m_data.first();
            m_data.removeFirst();
        }

        m_columnCount = 0;
        for (const QStringList &row : m_data) {
            if (row.size() > m_columnCount) {
                m_columnCount = row.size();
            }
        }
        if (m_headers.size() > m_columnCount) {
            m_columnCount = m_headers.size();
        }

        for (QStringList &row : m_data) {
            while (row.size() < m_columnCount) {
                row.append(QString());
            }
        }
        while (m_headers.size() < m_columnCount) {
            m_headers.append(QString("Column %1").arg(m_headers.size() + 1));
        }
    }

    endResetModel();

    qDebug() << "Loaded" << m_data.size() << "rows," << m_columnCount << "columns";
    qDebug() << "Preserved" << m_comments.size() << "comment lines";

    emit loadingFinished(true);
    return true;
}

bool CsvModel::loadFromString(const QString &content, char delimiter)
{
    qDebug() << "Loading CSV from string, delimiter:" << delimiter;

    m_filePath.clear();
    m_comments.clear();
    m_fileSize = content.size();
    m_lineEnding = LineEnding::LF;
    m_encoding = "UTF-8";

    m_delimiter = delimiter;

    QStringList allLines = content.split(QRegularExpression("\\r\\n|\\r|\\n"));

    QStringList nonCommentLines;
    for (const QString &line : allLines) {
        if (line.trimmed().startsWith('#')) {
            m_comments.append(line);
        } else if (!line.trimmed().isEmpty()) {
            nonCommentLines.append(line);
        }
    }

    CsvParser::Options options;
    options.delimiter = m_delimiter;
    options.quoteChar = m_quoteChar;
    options.hasHeader = m_hasHeader;
    m_parser->setOptions(options);

    beginResetModel();

    m_data.clear();
    for (const QString &line : nonCommentLines) {
        if (!line.trimmed().isEmpty()) {
            QStringList row = m_parser->parseLine(line);
            m_data.append(row);
        }
    }

    if (!m_data.isEmpty()) {
        if (m_hasHeader && !m_data.isEmpty()) {
            m_headers = m_data.first();
            m_data.removeFirst();
        }

        m_columnCount = 0;
        for (const QStringList &row : m_data) {
            if (row.size() > m_columnCount) {
                m_columnCount = row.size();
            }
        }
        if (m_headers.size() > m_columnCount) {
            m_columnCount = m_headers.size();
        }

        for (QStringList &row : m_data) {
            while (row.size() < m_columnCount) {
                row.append(QString());
            }
        }
        while (m_headers.size() < m_columnCount) {
            m_headers.append(QString("Column %1").arg(m_headers.size() + 1));
        }
    }

    endResetModel();

    qDebug() << "Parsed" << m_data.size() << "rows," << m_columnCount << "columns from string";
    return true;
}

bool CsvModel::saveToFile(const QString &path, char delimiter, LineEnding lineEnding)
{
    qDebug() << "Saving CSV file:" << path;

    if (delimiter == '\0') {
        delimiter = m_delimiter;
    }

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

    QString content;

    if (!m_comments.isEmpty()) {
        for (const QString &comment : m_comments) {
            content += comment + lineEndStr;
        }
    }

    if (m_hasHeader && !m_headers.isEmpty()) {
        for (int i = 0; i < m_headers.size(); i++) {
            if (i > 0) content += delimiter;
            content += escapeCsvField(m_headers[i], delimiter);
        }
        content += lineEndStr;
    }

    for (const QStringList &row : m_data) {
        for (int i = 0; i < row.size(); i++) {
            if (i > 0) content += delimiter;
            content += escapeCsvField(row[i], delimiter);
        }
        content += lineEndStr;
    }

    QString tempPath = path + ".tmp";
    QFile file(tempPath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << tempPath;
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;

    file.close();

    QFile::remove(path);
    if (!QFile::rename(tempPath, path)) {
        qWarning() << "Failed to rename temp file to" << path;
        return false;
    }

    m_lineEnding = lineEnding;

    qDebug() << "Saved" << m_data.size() << "rows to" << path;
    return true;
}

void CsvModel::clear()
{
    beginResetModel();

    m_data.clear();
    m_headers.clear();
    m_comments.clear();

    m_isFiltered = false;
    m_filterPattern.clear();
    m_visibleRows.clear();
    m_filterColumn = -1;

    m_hiddenColumns.clear();

    m_filePath.clear();
    m_fileSize = 0;
    m_columnCount = 0;

    m_lineEnding = LineEnding::LF;
    m_encoding = "UTF-8";

    endResetModel();
}

QString CsvModel::escapeCsvField(const QString &field, char delimiter) const
{
    if (field.contains(delimiter) || field.contains(m_quoteChar) ||
        field.contains('\n') || field.contains('\r')) {

        QString escaped = field;
        escaped.replace(m_quoteChar, QString(m_quoteChar) + m_quoteChar);
        return m_quoteChar + escaped + m_quoteChar;
    }

    return field;
}

void CsvModel::setDelimiter(char delimiter)
{
    m_delimiter = delimiter;
}

void CsvModel::setHasHeader(bool hasHeader)
{
    m_hasHeader = hasHeader;
}

bool CsvModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (count < 1 || row < 0 || row > m_data.size())
        return false;

    beginInsertRows(parent, row, row + count - 1);

    for (int i = 0; i < count; i++) {
        QStringList emptyRow;
        emptyRow.reserve(m_columnCount);
        for (int j = 0; j < m_columnCount; j++) {
            emptyRow.append(QString());
        }
        m_data.insert(row + i, emptyRow);
    }

    endInsertRows();
    return true;
}

bool CsvModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (count < 1 || row < 0 || row + count > m_data.size())
        return false;

    beginRemoveRows(parent, row, row + count - 1);
    m_data.remove(row, count);
    endRemoveRows();

    return true;
}

bool CsvModel::insertColumns(int column, int count, const QModelIndex &parent)
{
    if (count < 1 || column < 0 || column > m_columnCount)
        return false;

    beginInsertColumns(parent, column, column + count - 1);

    for (QStringList &row : m_data) {
        while (row.size() < column)
            row.append(QString());
        for (int i = 0; i < count; i++) {
            row.insert(column + i, QString());
        }
    }

    while (m_headers.size() < column)
        m_headers.append(QString("Column %1").arg(m_headers.size() + 1));
    for (int i = 0; i < count; i++) {
        m_headers.insert(column + i, QString("New Column %1").arg(column + i + 1));
    }

    m_columnCount += count;

    endInsertColumns();
    return true;
}

bool CsvModel::removeColumns(int column, int count, const QModelIndex &parent)
{
    if (count < 1 || column < 0 || column + count > m_columnCount)
        return false;

    beginRemoveColumns(parent, column, column + count - 1);

    for (QStringList &row : m_data) {
        for (int i = 0; i < count; i++) {
            if (column < row.size()) {
                row.removeAt(column);
            }
        }
    }

    for (int i = 0; i < count; i++) {
        if (column < m_headers.size()) {
            m_headers.removeAt(column);
        }
    }

    m_columnCount -= count;

    endRemoveColumns();
    return true;
}

void CsvModel::sortByColumn(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= m_columnCount)
        return;

    emit layoutAboutToBeChanged();

    std::sort(m_data.begin(), m_data.end(),
        [column, order](const QStringList &a, const QStringList &b) {
            QString valA = (column < a.size()) ? a[column] : QString();
            QString valB = (column < b.size()) ? b[column] : QString();

            bool okA, okB;
            double numA = valA.toDouble(&okA);
            double numB = valB.toDouble(&okB);

            if (okA && okB) {
                return (order == Qt::AscendingOrder) ? (numA < numB) : (numA > numB);
            }

            int cmp = QString::compare(valA, valB, Qt::CaseInsensitive);
            return (order == Qt::AscendingOrder) ? (cmp < 0) : (cmp > 0);
        });

    emit layoutChanged();
}

void CsvModel::removeDuplicateRows()
{
    QSet<QString> seen;
    QVector<QStringList> uniqueData;

    for (const QStringList &row : m_data) {
        QString rowKey = row.join('\t');
        if (!seen.contains(rowKey)) {
            seen.insert(rowKey);
            uniqueData.append(row);
        }
    }

    int removed = m_data.size() - uniqueData.size();

    if (removed > 0) {
        beginResetModel();
        m_data = uniqueData;
        endResetModel();
    }
}

void CsvModel::removeDuplicateRows(const QList<int> &columns)
{
    if (columns.isEmpty()) {
        removeDuplicateRows();
        return;
    }

    QSet<QString> seen;
    QVector<QStringList> uniqueData;

    for (const QStringList &row : m_data) {
        QStringList keyParts;
        for (int col : columns) {
            if (col >= 0 && col < row.size()) {
                keyParts.append(row[col]);
            }
        }
        QString rowKey = keyParts.join('\t');

        if (!seen.contains(rowKey)) {
            seen.insert(rowKey);
            uniqueData.append(row);
        }
    }

    int removed = m_data.size() - uniqueData.size();

    if (removed > 0) {
        beginResetModel();
        m_data = uniqueData;
        endResetModel();
    }
}

int CsvModel::findInColumn(const QString &pattern, int column, int startRow, bool useRegex)
{
    if (pattern.isEmpty() || column < 0 || column >= m_columnCount)
        return -1;

    QRegularExpression regex;
    if (useRegex) {
        regex.setPattern(pattern);
        if (!regex.isValid())
            return -1;
    }

    for (int row = startRow; row < m_data.size(); row++) {
        if (column < m_data[row].size()) {
            QString value = m_data[row][column];

            if (useRegex) {
                if (regex.match(value).hasMatch())
                    return row;
            } else {
                if (value.contains(pattern, Qt::CaseInsensitive))
                    return row;
            }
        }
    }

    return -1;
}

int CsvModel::replaceInColumn(const QString &pattern, const QString &replacement,
                               int column, bool useRegex)
{
    if (pattern.isEmpty() || column < 0 || column >= m_columnCount)
        return 0;

    int count = 0;
    QRegularExpression regex;

    if (useRegex) {
        regex.setPattern(pattern);
        if (!regex.isValid())
            return 0;
    }

    for (int row = 0; row < m_data.size(); row++) {
        if (column < m_data[row].size()) {
            QString &value = m_data[row][column];
            QString oldValue = value;

            if (useRegex) {
                value = value.replace(regex, replacement);
            } else {
                value = value.replace(pattern, replacement, Qt::CaseInsensitive);
            }

            if (value != oldValue) {
                count++;
                emit dataChanged(index(row, column), index(row, column));
            }
        }
    }

    return count;
}

void CsvModel::setFilter(const QString &pattern, int column, bool useRegex)
{
    if (pattern.isEmpty()) {
        clearFilter();
        return;
    }

    m_filterPattern = pattern;
    m_filterColumn = column;
    m_filterUseRegex = useRegex;
    m_isFiltered = true;

    m_visibleRows.clear();

    for (int row = 0; row < m_data.size(); row++) {
        if (rowMatchesFilter(row)) {
            m_visibleRows.append(row);
        }
    }

    beginResetModel();
    endResetModel();
}

void CsvModel::clearFilter()
{
    if (!m_isFiltered)
        return;

    m_isFiltered = false;
    m_filterPattern.clear();
    m_visibleRows.clear();

    beginResetModel();
    endResetModel();
}

bool CsvModel::rowMatchesFilter(int row) const
{
    if (!m_isFiltered || m_filterPattern.isEmpty())
        return true;

    QRegularExpression regex;
    if (m_filterUseRegex) {
        regex.setPattern(m_filterPattern);
        if (!regex.isValid())
            return false;
    }

    if (m_filterColumn == -1) {
        for (int col = 0; col < m_data[row].size(); col++) {
            QString value = m_data[row][col];

            if (m_filterUseRegex) {
                if (regex.match(value).hasMatch())
                    return true;
            } else {
                if (value.contains(m_filterPattern, Qt::CaseInsensitive))
                    return true;
            }
        }
        return false;
    } else {
        if (m_filterColumn >= m_data[row].size())
            return false;

        QString value = m_data[row][m_filterColumn];

        if (m_filterUseRegex) {
            return regex.match(value).hasMatch();
        } else {
            return value.contains(m_filterPattern, Qt::CaseInsensitive);
        }
    }
}

int CsvModel::visibleRowCount() const
{
    return m_isFiltered ? m_visibleRows.size() : m_data.size();
}

QString CsvModel::lineEndingString() const
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

CsvModel::LineEnding CsvModel::detectLineEnding(const QByteArray &data) const
{
    int crlfCount = data.count("\r\n");
    int lfCount = data.count('\n') - crlfCount;
    int crCount = data.count('\r') - crlfCount;

    if (crlfCount >= lfCount && crlfCount >= crCount && crlfCount > 0) {
        return LineEnding::CRLF;
    } else if (crCount > lfCount && crCount > 0) {
        return LineEnding::CR;
    } else {
        return LineEnding::LF;
    }
}

QString CsvModel::detectEncoding(const QByteArray &data) const
{
    if (data.startsWith("\xEF\xBB\xBF")) {
        return "UTF-8 BOM";
    } else if (data.startsWith("\xFF\xFE")) {
        return "UTF-16 LE";
    } else if (data.startsWith("\xFE\xFF")) {
        return "UTF-16 BE";
    }

    QString test = QString::fromUtf8(data);
    if (!test.contains(QChar::ReplacementCharacter)) {
        return "UTF-8";
    }

    bool isPureAscii = true;
    for (char c : data) {
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

void CsvModel::reorderColumns(const QList<int> &newOrder)
{
    if (newOrder.size() != m_columnCount)
        return;

    beginResetModel();

    QStringList newHeaders;
    for (int idx : newOrder) {
        if (idx >= 0 && idx < m_headers.size()) {
            newHeaders.append(m_headers[idx]);
        }
    }
    m_headers = newHeaders;

    for (QStringList &row : m_data) {
        QStringList newRow;
        for (int idx : newOrder) {
            if (idx >= 0 && idx < row.size()) {
                newRow.append(row[idx]);
            } else {
                newRow.append(QString());
            }
        }
        row = newRow;
    }

    endResetModel();
}

void CsvModel::duplicateColumn(int column, const QString &newName)
{
    if (column < 0 || column >= m_columnCount)
        return;

    beginInsertColumns(QModelIndex(), m_columnCount, m_columnCount);

    QString headerName = newName.isEmpty() ? m_headers[column] + " (copy)" : newName;
    m_headers.append(headerName);

    for (QStringList &row : m_data) {
        if (column < row.size()) {
            row.append(row[column]);
        } else {
            row.append(QString());
        }
    }

    m_columnCount++;

    endInsertColumns();
}

void CsvModel::combineColumns(const QList<int> &columns, const QString &delimiter, const QString &newName)
{
    if (columns.size() < 2)
        return;

    beginInsertColumns(QModelIndex(), m_columnCount, m_columnCount);

    m_headers.append(newName);

    for (QStringList &row : m_data) {
        QStringList parts;
        for (int col : columns) {
            if (col >= 0 && col < row.size()) {
                parts.append(row[col]);
            }
        }
        row.append(parts.join(delimiter));
    }

    m_columnCount++;

    endInsertColumns();
}

void CsvModel::splitColumn(int column, const QString &delimiter)
{
    if (column < 0 || column >= m_columnCount)
        return;

    int maxParts = 1;
    for (const QStringList &row : m_data) {
        if (column < row.size()) {
            int parts = row[column].split(delimiter).size();
            if (parts > maxParts) {
                maxParts = parts;
            }
        }
    }

    if (maxParts == 1)
        return;

    int numNewColumns = maxParts - 1;
    beginInsertColumns(QModelIndex(), column + 1, column + numNewColumns);

    QString originalHeader = m_headers[column];
    for (int i = 1; i <= numNewColumns; ++i) {
        m_headers.insert(column + i, QString("%1_%2").arg(originalHeader).arg(i + 1));
    }

    for (QStringList &row : m_data) {
        if (column < row.size()) {
            QStringList parts = row[column].split(delimiter);

            row[column] = parts.isEmpty() ? QString() : parts[0];

            for (int i = 1; i < parts.size(); ++i) {
                row.insert(column + i, parts[i]);
            }

            while (row.size() < column + maxParts) {
                row.insert(column + row.size() - column, QString());
            }
        }
    }

    m_columnCount += numNewColumns;

    endInsertColumns();
}

void CsvModel::hideColumn(int column)
{
    if (column < 0 || column >= m_columnCount)
        return;

    if (!m_hiddenColumns.contains(column)) {
        m_hiddenColumns.append(column);
    }
}

void CsvModel::showColumn(int column)
{
    m_hiddenColumns.removeAll(column);
}
