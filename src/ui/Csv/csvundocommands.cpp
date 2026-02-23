#include "include/Csv/csvundocommands.h"
#include "include/Csv/csvmodel.h"

// ---- CsvEditCellCommand ----

CsvEditCellCommand::CsvEditCellCommand(CsvModel *model, int row, int col,
                                       const QString &oldValue, const QString &newValue,
                                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_row(row)
    , m_col(col)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
    setText(QString("Edit cell (%1,%2)").arg(row).arg(col));
}

void CsvEditCellCommand::undo()
{
    m_model->setDataInternal(m_row, m_col, m_oldValue);
}

void CsvEditCellCommand::redo()
{
    m_model->setDataInternal(m_row, m_col, m_newValue);
}

bool CsvEditCellCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id())
        return false;
    const CsvEditCellCommand *cmd = static_cast<const CsvEditCellCommand*>(other);
    if (cmd->m_row != m_row || cmd->m_col != m_col || cmd->m_model != m_model)
        return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// ---- CsvInsertRowsCommand ----

CsvInsertRowsCommand::CsvInsertRowsCommand(CsvModel *model, int row, int count,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_row(row)
    , m_count(count)
{
    setText(QString("Insert %1 row(s) at %2").arg(count).arg(row));
}

void CsvInsertRowsCommand::undo()
{
    m_model->removeRowsInternal(m_row, m_count);
}

void CsvInsertRowsCommand::redo()
{
    m_model->insertRowsInternal(m_row, m_count);
}

// ---- CsvRemoveRowsCommand ----

CsvRemoveRowsCommand::CsvRemoveRowsCommand(CsvModel *model, int row, int count,
                                           const QVector<QStringList> &removedData,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_row(row)
    , m_count(count)
    , m_removedData(removedData)
{
    setText(QString("Remove %1 row(s) at %2").arg(count).arg(row));
}

void CsvRemoveRowsCommand::undo()
{
    m_model->insertRowsInternal(m_row, m_count);
    for (int i = 0; i < m_removedData.size(); ++i) {
        const QStringList &row = m_removedData[i];
        for (int c = 0; c < row.size(); ++c) {
            m_model->setDataInternal(m_row + i, c, row[c]);
        }
    }
}

void CsvRemoveRowsCommand::redo()
{
    m_model->removeRowsInternal(m_row, m_count);
}

// ---- CsvInsertColumnsCommand ----

CsvInsertColumnsCommand::CsvInsertColumnsCommand(CsvModel *model, int column, int count,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_column(column)
    , m_count(count)
{
    setText(QString("Insert %1 column(s) at %2").arg(count).arg(column));
}

void CsvInsertColumnsCommand::undo()
{
    m_model->removeColumnsInternal(m_column, m_count);
}

void CsvInsertColumnsCommand::redo()
{
    m_model->insertColumnsInternal(m_column, m_count);
}

// ---- CsvRemoveColumnsCommand ----

CsvRemoveColumnsCommand::CsvRemoveColumnsCommand(CsvModel *model, int column, int count,
                                                 const QVector<QStringList> &removedData,
                                                 const QStringList &removedHeaders,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_column(column)
    , m_count(count)
    , m_removedData(removedData)
    , m_removedHeaders(removedHeaders)
{
    setText(QString("Remove %1 column(s) at %2").arg(count).arg(column));
}

void CsvRemoveColumnsCommand::undo()
{
    m_model->insertColumnsInternal(m_column, m_count);
    // Restore headers
    for (int i = 0; i < m_removedHeaders.size(); ++i) {
        m_model->setHeaderDataInternal(m_column + i, m_removedHeaders[i]);
    }
    // Restore data
    for (int r = 0; r < m_removedData.size(); ++r) {
        const QStringList &colVals = m_removedData[r];
        for (int i = 0; i < colVals.size(); ++i) {
            m_model->setDataInternal(r, m_column + i, colVals[i]);
        }
    }
}

void CsvRemoveColumnsCommand::redo()
{
    m_model->removeColumnsInternal(m_column, m_count);
}

// ---- CsvSortCommand ----

CsvSortCommand::CsvSortCommand(CsvModel *model,
                               const QVector<QStringList> &preSortData,
                               int column, Qt::SortOrder order,
                               QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_preSortData(preSortData)
    , m_column(column)
    , m_order(order)
{
    setText(QString("Sort by column %1").arg(column));
}

void CsvSortCommand::undo()
{
    m_model->restoreDataInternal(m_preSortData);
}

void CsvSortCommand::redo()
{
    m_model->sortByColumnInternal(m_column, m_order);
}

// ---- CsvCutCellsCommand ----

CsvCutCellsCommand::CsvCutCellsCommand(CsvModel *model,
                                       const QList<CellValue> &oldValues,
                                       QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_oldValues(oldValues)
{
    setText(QString("Cut %1 cell(s)").arg(oldValues.size()));
}

void CsvCutCellsCommand::undo()
{
    for (const CellValue &cv : m_oldValues) {
        m_model->setDataInternal(cv.row, cv.col, cv.value);
    }
}

void CsvCutCellsCommand::redo()
{
    for (const CellValue &cv : m_oldValues) {
        m_model->setDataInternal(cv.row, cv.col, QString());
    }
}

// ---- CsvPasteCommand ----

CsvPasteCommand::CsvPasteCommand(CsvModel *model,
                                 int startRow, int startCol,
                                 const QList<CellValue> &overwrittenValues,
                                 const QStringList &pastedRows,
                                 int addedRows, int addedCols,
                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_model(model)
    , m_startRow(startRow)
    , m_startCol(startCol)
    , m_overwrittenValues(overwrittenValues)
    , m_pastedRows(pastedRows)
    , m_addedRows(addedRows)
    , m_addedCols(addedCols)
{
    setText("Paste cells");
}

void CsvPasteCommand::undo()
{
    // Remove any rows/cols that were added
    if (m_addedRows > 0) {
        m_model->removeRowsInternal(m_model->rowCount() - m_addedRows, m_addedRows);
    }
    if (m_addedCols > 0) {
        m_model->removeColumnsInternal(m_model->columnCount() - m_addedCols, m_addedCols);
    }
    // Restore overwritten values
    for (const CellValue &cv : m_overwrittenValues) {
        m_model->setDataInternal(cv.row, cv.col, cv.value);
    }
}

void CsvPasteCommand::redo()
{
    // Re-add rows/cols if needed
    if (m_addedRows > 0) {
        m_model->insertRowsInternal(m_model->rowCount(), m_addedRows);
    }
    if (m_addedCols > 0) {
        m_model->insertColumnsInternal(m_model->columnCount(), m_addedCols);
    }
    // Re-paste data
    for (int r = 0; r < m_pastedRows.size(); ++r) {
        QStringList cells = m_pastedRows[r].split('\t');
        for (int c = 0; c < cells.size(); ++c) {
            int targetRow = m_startRow + r;
            int targetCol = m_startCol + c;
            if (targetRow < m_model->rowCount() && targetCol < m_model->columnCount()) {
                m_model->setDataInternal(targetRow, targetCol, cells[c]);
            }
        }
    }
}
