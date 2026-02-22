#ifndef CSVGRID_H
#define CSVGRID_H

#include <QTableView>
#include <QAbstractTableModel>

class CsvModel;

class CsvGrid : public QTableView
{
    Q_OBJECT

public:
    explicit CsvGrid(QWidget *parent = nullptr);
    ~CsvGrid();

    void setCsvModel(QAbstractTableModel *model);
    QAbstractTableModel* csvModel() const;

    // Display options
    void setAlternatingRowColors(bool enable);
    void setShowGridLines(bool show);

    // Column operations
    void autoResizeColumns();

    // Clipboard operations
    void copyCells();
    void cutCells();
    void pasteCells();
    void copyColumn(int column);
    void cutColumn(int column);
    void pasteColumn(int column);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onHeaderClicked(int column);
    void showContextMenu(const QPoint &pos);
    void onHeaderContextMenu(const QPoint &pos);

private:
    void showHeaderContextMenu(const QPoint &pos, int column);

    QAbstractTableModel *m_csvModel;
    int m_lastSortColumn;
    Qt::SortOrder m_lastSortOrder;

    // Column clipboard
    QStringList m_copiedColumnData;
    QString m_copiedColumnHeader;
    bool m_columnCut;
};

#endif // CSVGRID_H
