#ifndef CSVCONSISTENCYCHECKER_H
#define CSVCONSISTENCYCHECKER_H

#include <QDialog>
#include <QStringList>

class CsvModel;
class QTreeWidget;

struct ConsistencyIssue {
    enum Type { MixedTypes, EmptyCell, WhitespaceOnly, DuplicateInUnique, InconsistentFieldCount };
    Type type;
    int row;      // -1 for column-level issues
    int column;   // -1 for row-level issues
    QString description;
};

class CsvConsistencyChecker : public QDialog
{
    Q_OBJECT

public:
    explicit CsvConsistencyChecker(CsvModel *model, QWidget *parent = nullptr);

    QList<ConsistencyIssue> check();

private:
    void checkMixedTypes(QList<ConsistencyIssue> &issues);
    void checkEmptyCells(QList<ConsistencyIssue> &issues);
    void checkWhitespaceOnly(QList<ConsistencyIssue> &issues);
    void checkDuplicateIds(QList<ConsistencyIssue> &issues);
    void checkFieldCounts(QList<ConsistencyIssue> &issues);

    CsvModel *m_model;
    QTreeWidget *m_tree;
};

#endif // CSVCONSISTENCYCHECKER_H
