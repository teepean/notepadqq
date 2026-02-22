#ifndef CSVPARSER_H
#define CSVPARSER_H

#include <QString>
#include <QStringList>
#include <QVector>

class CsvParser
{
public:
    struct Options {
        char delimiter;
        char quoteChar;
        char escapeChar;
        bool trimWhitespace;
        bool skipEmptyLines;
        int skipRows;
        bool hasHeader;

        Options()
            : delimiter(',')
            , quoteChar('"')
            , escapeChar('\\')
            , trimWhitespace(false)
            , skipEmptyLines(false)
            , skipRows(0)
            , hasHeader(true)
        {}
    };

    explicit CsvParser(const Options &options = Options());
    ~CsvParser();

    // Parsing
    QVector<QStringList> parseFile(const QString &path);
    QVector<QStringList> parseString(const QString &data);
    QStringList parseLine(const QString &line);

    // Delimiter detection
    static char detectDelimiter(const QString &path, int sampleLines = 100);
    static char detectDelimiterFromString(const QString &data);

    // Validation
    static bool isValidCsvFile(const QString &path);

    // Options
    void setOptions(const Options &options);
    Options options() const { return m_options; }

private:
    Options m_options;

    // Helper methods
    QString parseQuotedField(const QString &line, int &pos);
    QString parseUnquotedField(const QString &line, int &pos);

    // Delimiter detection heuristics
    struct DelimiterCandidate {
        char delimiter;
        int count;
        double variance;
    };
    static QVector<DelimiterCandidate> analyzeDelimiters(const QString &data);
};

#endif // CSVPARSER_H
