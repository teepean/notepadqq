#include "include/Csv/csvparser.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QtMath>
#include <algorithm>

CsvParser::CsvParser(const Options &options)
    : m_options(options)
{
}

CsvParser::~CsvParser()
{
}

QVector<QStringList> CsvParser::parseFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file:" << path;
        return QVector<QStringList>();
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    return parseString(content);
}

QVector<QStringList> CsvParser::parseString(const QString &data)
{
    QVector<QStringList> result;
    QStringList lines = data.split('\n');

    // Skip rows if configured
    int startRow = m_options.skipRows;

    for (int i = startRow; i < lines.size(); i++) {
        QString line = lines[i].trimmed();

        // Skip empty lines if configured
        if (m_options.skipEmptyLines && line.isEmpty()) {
            continue;
        }

        if (!line.isEmpty()) {
            QStringList fields = parseLine(line);
            result.append(fields);
        }
    }

    return result;
}

QStringList CsvParser::parseLine(const QString &line)
{
    QStringList fields;
    int pos = 0;

    while (pos < line.length()) {
        QString field;

        // Skip whitespace if trim is enabled
        if (m_options.trimWhitespace) {
            while (pos < line.length() && line[pos].isSpace() && line[pos] != m_options.delimiter) {
                pos++;
            }
        }

        // Check if field is quoted
        if (pos < line.length() && line[pos] == m_options.quoteChar) {
            field = parseQuotedField(line, pos);
        } else {
            field = parseUnquotedField(line, pos);
        }

        if (m_options.trimWhitespace) {
            field = field.trimmed();
        }

        fields.append(field);

        // Skip delimiter
        if (pos < line.length() && line[pos] == m_options.delimiter) {
            pos++;
        }
    }

    return fields;
}

QString CsvParser::parseQuotedField(const QString &line, int &pos)
{
    QString field;
    pos++;  // Skip opening quote

    while (pos < line.length()) {
        QChar c = line[pos];

        if (c == m_options.quoteChar) {
            // Check if it's an escaped quote
            if (pos + 1 < line.length() && line[pos + 1] == m_options.quoteChar) {
                // Escaped quote (doubled)
                field.append(m_options.quoteChar);
                pos += 2;
            } else {
                // End of quoted field
                pos++;
                break;
            }
        } else if (c == m_options.escapeChar && pos + 1 < line.length()) {
            // Escape sequence
            pos++;
            field.append(line[pos]);
            pos++;
        } else {
            field.append(c);
            pos++;
        }
    }

    return field;
}

QString CsvParser::parseUnquotedField(const QString &line, int &pos)
{
    QString field;

    while (pos < line.length()) {
        QChar c = line[pos];

        if (c == m_options.delimiter) {
            break;
        }

        field.append(c);
        pos++;
    }

    return field;
}

char CsvParser::detectDelimiter(const QString &path, int sampleLines)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for delimiter detection:" << path;
        return ',';  // Default
    }

    QTextStream stream(&file);
    QString sample;

    // Read sample lines
    for (int i = 0; i < sampleLines && !stream.atEnd(); i++) {
        sample += stream.readLine() + "\n";
    }

    file.close();

    return detectDelimiterFromString(sample);
}

char CsvParser::detectDelimiterFromString(const QString &data)
{
    // Common delimiters to check
    QVector<char> candidates = {',', '\t', ';', '|', ':'};
    QVector<DelimiterCandidate> results;

    for (char delim : candidates) {
        QStringList lines = data.split('\n');
        QVector<int> counts;

        for (const QString &line : lines) {
            if (line.trimmed().isEmpty()) continue;

            int count = 0;
            bool inQuote = false;

            for (QChar c : line) {
                if (c == '"') {
                    inQuote = !inQuote;
                } else if (c == delim && !inQuote) {
                    count++;
                }
            }

            counts.append(count);
        }

        if (counts.isEmpty()) continue;

        // Calculate mean and variance
        double sum = 0;
        for (int c : counts) sum += c;
        double mean = sum / counts.size();

        double variance = 0;
        for (int c : counts) {
            variance += (c - mean) * (c - mean);
        }
        variance /= counts.size();

        // Low variance = consistent delimiter = good candidate
        // High count = more fields = better candidate
        results.append({delim, (int)mean, variance});
    }

    // Sort by variance (ascending) and count (descending)
    std::sort(results.begin(), results.end(), [](const DelimiterCandidate &a, const DelimiterCandidate &b) {
        // Prefer delimiter with count > 0
        if (a.count == 0 && b.count > 0) return false;
        if (b.count == 0 && a.count > 0) return true;

        // Prefer low variance (consistent)
        if (qAbs(a.variance - b.variance) > 0.1) {
            return a.variance < b.variance;
        }

        // Prefer high count (more fields)
        return a.count > b.count;
    });

    if (!results.isEmpty()) {
        char detected = results.first().delimiter;
        qDebug() << "Detected delimiter:" << detected
                 << "count:" << results.first().count
                 << "variance:" << results.first().variance;
        return detected;
    }

    return ',';  // Default fallback
}

bool CsvParser::isValidCsvFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    QString firstLine = stream.readLine();
    file.close();

    // Check if first line has at least one delimiter
    char delim = detectDelimiterFromString(firstLine);
    return firstLine.contains(delim);
}

void CsvParser::setOptions(const Options &options)
{
    m_options = options;
}
