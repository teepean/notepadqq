#ifndef CSVUNDOCOMMANDS_H
#define CSVUNDOCOMMANDS_H

#include <QUndoCommand>
#include <QModelIndex>
#include <QStringList>
#include <QVector>

class CsvModel;

class CsvEditCellCommand : public QUndoCommand
{
public:
    CsvEditCellCommand(CsvModel *model, int row, int col,
                       const QString &oldValue, const QString &newValue,
                       QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    CsvModel *m_model;
    int m_row;
    int m_col;
    QString m_oldValue;
    QString m_newValue;
};

class CsvInsertRowsCommand : public QUndoCommand
{
public:
    CsvInsertRowsCommand(CsvModel *model, int row, int count,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    int m_row;
    int m_count;
};

class CsvRemoveRowsCommand : public QUndoCommand
{
public:
    CsvRemoveRowsCommand(CsvModel *model, int row, int count,
                         const QVector<QStringList> &removedData,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    int m_row;
    int m_count;
    QVector<QStringList> m_removedData;
};

class CsvInsertColumnsCommand : public QUndoCommand
{
public:
    CsvInsertColumnsCommand(CsvModel *model, int column, int count,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    int m_column;
    int m_count;
};

class CsvRemoveColumnsCommand : public QUndoCommand
{
public:
    CsvRemoveColumnsCommand(CsvModel *model, int column, int count,
                            const QVector<QStringList> &removedData,
                            const QStringList &removedHeaders,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    int m_column;
    int m_count;
    QVector<QStringList> m_removedData; // one QStringList per row (column values)
    QStringList m_removedHeaders;
};

class CsvSortCommand : public QUndoCommand
{
public:
    CsvSortCommand(CsvModel *model,
                   const QVector<QStringList> &preSortData,
                   int column, Qt::SortOrder order,
                   QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    QVector<QStringList> m_preSortData;
    int m_column;
    Qt::SortOrder m_order;
};

struct CellValue {
    int row;
    int col;
    QString value;
};

class CsvCutCellsCommand : public QUndoCommand
{
public:
    CsvCutCellsCommand(CsvModel *model,
                       const QList<CellValue> &oldValues,
                       QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    QList<CellValue> m_oldValues;
};

class CsvPasteCommand : public QUndoCommand
{
public:
    CsvPasteCommand(CsvModel *model,
                    int startRow, int startCol,
                    const QList<CellValue> &overwrittenValues,
                    const QStringList &pastedRows,
                    int addedRows, int addedCols,
                    QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    CsvModel *m_model;
    int m_startRow;
    int m_startCol;
    QList<CellValue> m_overwrittenValues;
    QStringList m_pastedRows;
    int m_addedRows;
    int m_addedCols;
};

#endif // CSVUNDOCOMMANDS_H
