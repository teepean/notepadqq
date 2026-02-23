#include "include/Csv/csvconsistencychecker.h"
#include "include/Csv/csvmodel.h"

#include <QVBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSet>

CsvConsistencyChecker::CsvConsistencyChecker(CsvModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(tr("Data Consistency Check"));
    resize(600, 400);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Type"), tr("Location"), tr("Description")});
    m_tree->header()->setStretchLastSection(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree);

    QList<ConsistencyIssue> issues = check();

    if (issues.isEmpty()) {
        QLabel *lbl = new QLabel(tr("No issues found. Data looks consistent."), this);
        lbl->setAlignment(Qt::AlignCenter);
        layout->addWidget(lbl);
    } else {
        for (const ConsistencyIssue &issue : issues) {
            QTreeWidgetItem *item = new QTreeWidgetItem();
            switch (issue.type) {
            case ConsistencyIssue::MixedTypes:       item->setText(0, tr("Mixed Types")); break;
            case ConsistencyIssue::EmptyCell:         item->setText(0, tr("Empty Cell")); break;
            case ConsistencyIssue::WhitespaceOnly:    item->setText(0, tr("Whitespace Only")); break;
            case ConsistencyIssue::DuplicateInUnique: item->setText(0, tr("Duplicate ID")); break;
            case ConsistencyIssue::InconsistentFieldCount: item->setText(0, tr("Field Count")); break;
            }

            QString loc;
            if (issue.row >= 0 && issue.column >= 0)
                loc = tr("Row %1, Col %2").arg(issue.row + 1).arg(issue.column + 1);
            else if (issue.column >= 0)
                loc = tr("Column %1").arg(issue.column + 1);
            else if (issue.row >= 0)
                loc = tr("Row %1").arg(issue.row + 1);
            item->setText(1, loc);
            item->setText(2, issue.description);
            m_tree->addTopLevelItem(item);
        }
    }

    QLabel *summary = new QLabel(tr("%1 issue(s) found").arg(issues.size()), this);
    layout->addWidget(summary);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QList<ConsistencyIssue> CsvConsistencyChecker::check()
{
    QList<ConsistencyIssue> issues;
    checkMixedTypes(issues);
    checkEmptyCells(issues);
    checkWhitespaceOnly(issues);
    checkDuplicateIds(issues);
    checkFieldCounts(issues);
    return issues;
}

void CsvConsistencyChecker::checkMixedTypes(QList<ConsistencyIssue> &issues)
{
    for (int col = 0; col < m_model->totalColumns(); ++col) {
        int numericCount = 0;
        int textCount = 0;
        for (int row = 0; row < m_model->totalRows(); ++row) {
            QModelIndex idx = m_model->index(row, col);
            QString val = idx.data(Qt::DisplayRole).toString();
            if (val.isEmpty()) continue;
            bool ok;
            val.toDouble(&ok);
            if (ok) numericCount++;
            else textCount++;
        }
        if (numericCount > 0 && textCount > 0) {
            ConsistencyIssue issue;
            issue.type = ConsistencyIssue::MixedTypes;
            issue.row = -1;
            issue.column = col;
            QString header = m_model->headerData(col, Qt::Horizontal).toString();
            issue.description = tr("Column '%1' has %2 numeric and %3 text values")
                .arg(header).arg(numericCount).arg(textCount);
            issues.append(issue);
        }
    }
}

void CsvConsistencyChecker::checkEmptyCells(QList<ConsistencyIssue> &issues)
{
    for (int col = 0; col < m_model->totalColumns(); ++col) {
        int emptyCount = 0;
        int totalCount = m_model->totalRows();
        QList<int> emptyRows;
        for (int row = 0; row < totalCount; ++row) {
            QModelIndex idx = m_model->index(row, col);
            if (idx.data(Qt::DisplayRole).toString().isEmpty()) {
                emptyCount++;
                if (emptyRows.size() < 5) emptyRows.append(row);
            }
        }
        // Report if column is mostly filled but has some empties
        if (emptyCount > 0 && emptyCount < totalCount * 0.5 && totalCount > 3) {
            ConsistencyIssue issue;
            issue.type = ConsistencyIssue::EmptyCell;
            issue.row = -1;
            issue.column = col;
            QString header = m_model->headerData(col, Qt::Horizontal).toString();
            issue.description = tr("Column '%1' has %2 empty cell(s) out of %3 rows")
                .arg(header).arg(emptyCount).arg(totalCount);
            issues.append(issue);
        }
    }
}

void CsvConsistencyChecker::checkWhitespaceOnly(QList<ConsistencyIssue> &issues)
{
    for (int row = 0; row < m_model->totalRows(); ++row) {
        for (int col = 0; col < m_model->totalColumns(); ++col) {
            QModelIndex idx = m_model->index(row, col);
            QString val = idx.data(Qt::DisplayRole).toString();
            if (!val.isEmpty() && val.trimmed().isEmpty()) {
                ConsistencyIssue issue;
                issue.type = ConsistencyIssue::WhitespaceOnly;
                issue.row = row;
                issue.column = col;
                issue.description = tr("Cell contains only whitespace (%1 chars)")
                    .arg(val.length());
                issues.append(issue);
            }
        }
    }
}

void CsvConsistencyChecker::checkDuplicateIds(QList<ConsistencyIssue> &issues)
{
    // Check columns that look like IDs (all unique except duplicates)
    for (int col = 0; col < m_model->totalColumns(); ++col) {
        if (m_model->totalRows() < 3) continue;

        QMap<QString, int> valueCounts;
        int nonEmptyCount = 0;
        for (int row = 0; row < m_model->totalRows(); ++row) {
            QModelIndex idx = m_model->index(row, col);
            QString val = idx.data(Qt::DisplayRole).toString();
            if (!val.isEmpty()) {
                valueCounts[val]++;
                nonEmptyCount++;
            }
        }

        // If most values are unique (>90%), this might be an ID column
        int uniqueCount = valueCounts.size();
        if (nonEmptyCount > 0 && uniqueCount >= nonEmptyCount * 0.9 && uniqueCount < nonEmptyCount) {
            int dupeCount = nonEmptyCount - uniqueCount;
            ConsistencyIssue issue;
            issue.type = ConsistencyIssue::DuplicateInUnique;
            issue.row = -1;
            issue.column = col;
            QString header = m_model->headerData(col, Qt::Horizontal).toString();
            issue.description = tr("Column '%1' appears to be a unique ID but has %2 duplicate(s)")
                .arg(header).arg(dupeCount);
            issues.append(issue);
        }
    }
}

void CsvConsistencyChecker::checkFieldCounts(QList<ConsistencyIssue> &issues)
{
    int expectedCols = m_model->totalColumns();
    for (int row = 0; row < m_model->totalRows(); ++row) {
        QStringList rowData = m_model->rowData(row);
        // Count non-trailing-empty fields
        int actualCols = rowData.size();
        while (actualCols > 0 && rowData[actualCols - 1].isEmpty()) {
            actualCols--;
        }
        if (actualCols > 0 && actualCols < expectedCols - 1) {
            ConsistencyIssue issue;
            issue.type = ConsistencyIssue::InconsistentFieldCount;
            issue.row = row;
            issue.column = -1;
            issue.description = tr("Row has %1 non-empty fields, expected %2")
                .arg(actualCols).arg(expectedCols);
            issues.append(issue);
        }
    }
}
