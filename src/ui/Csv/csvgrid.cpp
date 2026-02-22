#include "include/Csv/csvgrid.h"
#include "include/Csv/csvmodel.h"
#include "include/Csv/lazyloadcsvmodel.h"
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>

CsvGrid::CsvGrid(QWidget *parent)
    : QTableView(parent)
    , m_csvModel(nullptr)
    , m_lastSortColumn(-1)
    , m_lastSortOrder(Qt::AscendingOrder)
    , m_columnCut(false)
{
    setAlternatingRowColors(true);
    setShowGrid(true);

    setSelectionBehavior(QAbstractItemView::SelectItems);
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    setEditTriggers(QAbstractItemView::DoubleClicked |
                    QAbstractItemView::EditKeyPressed |
                    QAbstractItemView::AnyKeyPressed);

    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);

    verticalHeader()->setDefaultSectionSize(25);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    setSortingEnabled(false);

    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &CsvGrid::onHeaderContextMenu);

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
}

CsvGrid::~CsvGrid()
{
}

void CsvGrid::setCsvModel(QAbstractTableModel *model)
{
    m_csvModel = model;
    setModel(model);

    if (model && model->columnCount() > 0) {
        autoResizeColumns();
    }
}

QAbstractTableModel* CsvGrid::csvModel() const
{
    return m_csvModel;
}

void CsvGrid::setAlternatingRowColors(bool enable)
{
    QTableView::setAlternatingRowColors(enable);
}

void CsvGrid::setShowGridLines(bool show)
{
    setShowGrid(show);
}

void CsvGrid::autoResizeColumns()
{
    if (!model())
        return;

    QFontMetrics fm(horizontalHeader()->font());

    for (int i = 0; i < model()->columnCount(); i++) {
        QString headerText = model()->headerData(i, Qt::Horizontal).toString();
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        int headerWidth = fm.horizontalAdvance(headerText) + 30;
#else
        int headerWidth = fm.width(headerText) + 30;
#endif

        resizeColumnToContents(i);

        int currentWidth = columnWidth(i);
        int finalWidth = qMax(currentWidth, headerWidth);

        if (finalWidth > 300) {
            finalWidth = 300;
        } else if (finalWidth < headerWidth) {
            finalWidth = headerWidth;
        }

        setColumnWidth(i, finalWidth);
    }
}

void CsvGrid::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copyCells();
        return;
    }

    if (event->matches(QKeySequence::Cut)) {
        cutCells();
        return;
    }

    if (event->matches(QKeySequence::Paste)) {
        pasteCells();
        return;
    }

    QTableView::keyPressEvent(event);
}

void CsvGrid::onHeaderClicked(int column)
{
    Q_UNUSED(column);
}

void CsvGrid::onHeaderContextMenu(const QPoint &pos)
{
    int column = horizontalHeader()->logicalIndexAt(pos);
    if (column >= 0) {
        showHeaderContextMenu(horizontalHeader()->mapToGlobal(pos), column);
    }
}

void CsvGrid::showHeaderContextMenu(const QPoint &pos, int column)
{
    if (!m_csvModel)
        return;

    CsvModel* csvModel = qobject_cast<CsvModel*>(m_csvModel);
    bool isCsvModel = (csvModel != nullptr);

    QMenu contextMenu(this);

    QString columnName = m_csvModel->headerData(column, Qt::Horizontal).toString();
    QAction *headerAction = contextMenu.addAction(QString("Column: %1").arg(columnName));
    headerAction->setEnabled(false);
    contextMenu.addSeparator();

    QAction *sortAscAction = contextMenu.addAction(tr("Sort Ascending"));
    sortAscAction->setEnabled(isCsvModel);
    QAction *sortDescAction = contextMenu.addAction(tr("Sort Descending"));
    sortDescAction->setEnabled(isCsvModel);

    contextMenu.addSeparator();

    QAction *insertLeftAction = contextMenu.addAction(tr("Insert Column Left"));
    insertLeftAction->setEnabled(isCsvModel);
    QAction *insertRightAction = contextMenu.addAction(tr("Insert Column Right"));
    insertRightAction->setEnabled(isCsvModel);
    QAction *deleteColAction = contextMenu.addAction(tr("Delete Column"));
    deleteColAction->setEnabled(isCsvModel);

    contextMenu.addSeparator();

    QAction *hideColAction = contextMenu.addAction(tr("Hide Column"));
    hideColAction->setEnabled(isCsvModel);
    QAction *autoResizeAction = contextMenu.addAction(tr("Auto-Resize This Column"));
    QAction *autoResizeAllAction = contextMenu.addAction(tr("Auto-Resize All Columns"));

    contextMenu.addSeparator();

    QAction *copyColAction = contextMenu.addAction(tr("Copy Column"));
    copyColAction->setEnabled(isCsvModel);
    QAction *cutColAction = contextMenu.addAction(tr("Cut Column"));
    cutColAction->setEnabled(isCsvModel);
    QAction *pasteColAction = contextMenu.addAction(tr("Paste Column"));
    pasteColAction->setEnabled(isCsvModel && !m_copiedColumnData.isEmpty());

    QAction *selectedAction = contextMenu.exec(pos);

    if (selectedAction == sortAscAction && csvModel) {
        csvModel->sortByColumn(column, Qt::AscendingOrder);
        m_lastSortColumn = column;
        m_lastSortOrder = Qt::AscendingOrder;
        horizontalHeader()->setSortIndicatorShown(true);
        horizontalHeader()->setSortIndicator(column, Qt::AscendingOrder);

    } else if (selectedAction == sortDescAction && csvModel) {
        csvModel->sortByColumn(column, Qt::DescendingOrder);
        m_lastSortColumn = column;
        m_lastSortOrder = Qt::DescendingOrder;
        horizontalHeader()->setSortIndicatorShown(true);
        horizontalHeader()->setSortIndicator(column, Qt::DescendingOrder);

    } else if (selectedAction == insertLeftAction && csvModel) {
        csvModel->insertColumns(column, 1);

    } else if (selectedAction == insertRightAction && csvModel) {
        csvModel->insertColumns(column + 1, 1);

    } else if (selectedAction == deleteColAction && csvModel) {
        csvModel->removeColumns(column, 1);

    } else if (selectedAction == hideColAction && csvModel) {
        setColumnHidden(column, true);
        csvModel->hideColumn(column);

    } else if (selectedAction == autoResizeAction) {
        resizeColumnToContents(column);

    } else if (selectedAction == autoResizeAllAction) {
        autoResizeColumns();

    } else if (selectedAction == copyColAction) {
        copyColumn(column);

    } else if (selectedAction == cutColAction) {
        cutColumn(column);

    } else if (selectedAction == pasteColAction) {
        pasteColumn(column);
    }
}

void CsvGrid::contextMenuEvent(QContextMenuEvent *event)
{
    showContextMenu(event->pos());
}

void CsvGrid::showContextMenu(const QPoint &pos)
{
    QModelIndex index = indexAt(pos);
    if (!index.isValid())
        return;

    CsvModel* csvModel = qobject_cast<CsvModel*>(m_csvModel);
    LazyLoadCsvModel* lazyModel = qobject_cast<LazyLoadCsvModel*>(m_csvModel);
    bool isCsvModel = (csvModel != nullptr);
    bool isLazyModel = (lazyModel != nullptr);
    bool canEdit = isCsvModel || isLazyModel;

    QMenu contextMenu(this);

    QAction *copyCellsAction = contextMenu.addAction(tr("Copy"));
    copyCellsAction->setShortcut(QKeySequence::Copy);
    QAction *cutCellsAction = contextMenu.addAction(tr("Cut"));
    cutCellsAction->setShortcut(QKeySequence::Cut);
    cutCellsAction->setEnabled(canEdit);
    QAction *pasteCellsAction = contextMenu.addAction(tr("Paste"));
    pasteCellsAction->setShortcut(QKeySequence::Paste);
    pasteCellsAction->setEnabled(canEdit);

    contextMenu.addSeparator();

    QAction *insertRowAboveAction = contextMenu.addAction(tr("Insert Row Above"));
    insertRowAboveAction->setEnabled(canEdit);
    QAction *insertRowBelowAction = contextMenu.addAction(tr("Insert Row Below"));
    insertRowBelowAction->setEnabled(canEdit);
    QAction *deleteRowAction = contextMenu.addAction(tr("Delete Row"));
    deleteRowAction->setEnabled(canEdit);

    contextMenu.addSeparator();

    QAction *insertColLeftAction = contextMenu.addAction(tr("Insert Column Left"));
    insertColLeftAction->setEnabled(isCsvModel);
    QAction *insertColRightAction = contextMenu.addAction(tr("Insert Column Right"));
    insertColRightAction->setEnabled(isCsvModel);
    QAction *deleteColAction = contextMenu.addAction(tr("Delete Column"));
    deleteColAction->setEnabled(isCsvModel);

    QAction *selectedAction = contextMenu.exec(mapToGlobal(pos));

    if (selectedAction == copyCellsAction) {
        copyCells();
    } else if (selectedAction == cutCellsAction) {
        cutCells();
    } else if (selectedAction == pasteCellsAction) {
        pasteCells();
    } else if (selectedAction == insertRowAboveAction) {
        m_csvModel->insertRows(index.row(), 1);
    } else if (selectedAction == insertRowBelowAction) {
        m_csvModel->insertRows(index.row() + 1, 1);
    } else if (selectedAction == deleteRowAction) {
        m_csvModel->removeRows(index.row(), 1);
    } else if (selectedAction == insertColLeftAction && csvModel) {
        csvModel->insertColumns(index.column(), 1);
    } else if (selectedAction == insertColRightAction && csvModel) {
        csvModel->insertColumns(index.column() + 1, 1);
    } else if (selectedAction == deleteColAction && csvModel) {
        csvModel->removeColumns(index.column(), 1);
    }
}

void CsvGrid::copyCells()
{
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty())
        return;

    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex &a, const QModelIndex &b) {
        if (a.row() != b.row())
            return a.row() < b.row();
        return a.column() < b.column();
    });

    QString clipboardText;
    int lastRow = -1;

    for (const QModelIndex &index : indexes) {
        if (index.row() != lastRow) {
            if (lastRow != -1) {
                clipboardText += '\n';
            }
            lastRow = index.row();
        } else {
            clipboardText += '\t';
        }
        clipboardText += index.data(Qt::DisplayRole).toString();
    }

    QApplication::clipboard()->setText(clipboardText);
}

void CsvGrid::cutCells()
{
    copyCells();

    if (!m_csvModel)
        return;

    QModelIndexList indexes = selectedIndexes();
    for (const QModelIndex &index : indexes) {
        m_csvModel->setData(index, QString(), Qt::EditRole);
    }
}

void CsvGrid::pasteCells()
{
    if (!m_csvModel)
        return;

    QModelIndex startIndex = this->currentIndex();
    if (!startIndex.isValid())
        return;

    QString clipboardText = QApplication::clipboard()->text();
    if (clipboardText.isEmpty())
        return;

    QStringList rows = clipboardText.split('\n');

    if (!rows.isEmpty() && rows.last().isEmpty()) {
        rows.removeLast();
    }

    if (rows.isEmpty())
        return;

    int startRow = startIndex.row();
    int startCol = startIndex.column();

    QStringList firstRow = rows.first().split('\t');
    int maxRow = startRow + rows.size();
    int maxCol = startCol + firstRow.size();

    bool needsRowExpansion = maxRow > m_csvModel->rowCount();
    bool needsColExpansion = maxCol > m_csvModel->columnCount();

    if (needsRowExpansion) {
        int rowsToAdd = maxRow - m_csvModel->rowCount();
        if (!m_csvModel->insertRows(m_csvModel->rowCount(), rowsToAdd)) {
            maxRow = m_csvModel->rowCount();
        }
    }

    if (needsColExpansion) {
        int colsToAdd = maxCol - m_csvModel->columnCount();
        if (!m_csvModel->insertColumns(m_csvModel->columnCount(), colsToAdd)) {
            maxCol = m_csvModel->columnCount();
        }
    }

    for (int r = 0; r < rows.size(); ++r) {
        int targetRow = startRow + r;
        if (targetRow >= maxRow)
            break;

        QStringList cells = rows[r].split('\t');
        for (int c = 0; c < cells.size(); ++c) {
            int targetCol = startCol + c;
            if (targetCol >= maxCol)
                break;

            QModelIndex targetIndex = m_csvModel->index(targetRow, targetCol);
            if (targetIndex.isValid()) {
                m_csvModel->setData(targetIndex, cells[c], Qt::EditRole);
            }
        }
    }
}

void CsvGrid::copyColumn(int column)
{
    if (!m_csvModel || column < 0 || column >= m_csvModel->columnCount())
        return;

    m_copiedColumnData.clear();

    m_copiedColumnHeader = m_csvModel->headerData(column, Qt::Horizontal).toString();
    m_copiedColumnData.append(m_copiedColumnHeader);

    for (int row = 0; row < m_csvModel->rowCount(); ++row) {
        QModelIndex index = m_csvModel->index(row, column);
        m_copiedColumnData.append(index.data(Qt::DisplayRole).toString());
    }

    m_columnCut = false;
}

void CsvGrid::cutColumn(int column)
{
    copyColumn(column);
    m_columnCut = true;

    CsvModel* csvModel = qobject_cast<CsvModel*>(m_csvModel);
    if (csvModel) {
        csvModel->removeColumns(column, 1);
    }
}

void CsvGrid::pasteColumn(int column)
{
    if (!m_csvModel || m_copiedColumnData.isEmpty())
        return;

    if (column < 0 || column >= m_csvModel->columnCount())
        return;

    QString header = m_copiedColumnData.first();

    m_csvModel->setHeaderData(column, Qt::Horizontal, header);

    for (int row = 0; row < m_csvModel->rowCount() && row + 1 < m_copiedColumnData.size(); ++row) {
        QModelIndex index = m_csvModel->index(row, column);
        m_csvModel->setData(index, m_copiedColumnData[row + 1], Qt::EditRole);
    }

    if (m_copiedColumnData.size() - 1 > m_csvModel->rowCount()) {
        int rowsToAdd = m_copiedColumnData.size() - 1 - m_csvModel->rowCount();
        int startRow = m_csvModel->rowCount();
        m_csvModel->insertRows(startRow, rowsToAdd);

        for (int i = 0; i < rowsToAdd; ++i) {
            QModelIndex index = m_csvModel->index(startRow + i, column);
            m_csvModel->setData(index, m_copiedColumnData[startRow + i + 1], Qt::EditRole);
        }
    }

    m_columnCut = false;
}
