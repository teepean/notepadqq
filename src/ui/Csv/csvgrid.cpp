#include "include/Csv/csvgrid.h"
#include "include/Csv/csvmodel.h"
#include "include/Csv/csvundocommands.h"
#include "include/Csv/lazyloadcsvmodel.h"
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QPainter>
#include <QTextEdit>

CsvGrid::CsvGrid(QWidget *parent)
    : QTableView(parent)
    , m_csvModel(nullptr)
    , m_lastSortColumn(-1)
    , m_lastSortOrder(Qt::AscendingOrder)
    , m_lastSelectedRow(-1)
    , m_lastSelectedColumn(-1)
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
    horizontalHeader()->setSectionsClickable(true);

    verticalHeader()->setDefaultSectionSize(25);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    setSortingEnabled(false);

    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &CsvGrid::onHeaderContextMenu);
    connect(horizontalHeader(), &QHeaderView::sectionClicked,
            this, &CsvGrid::onHeaderClicked);

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    connect(verticalHeader(), &QHeaderView::sectionClicked,
            this, &CsvGrid::onRowHeaderClicked);

    // Intercept keyboard events on headers so copy/cut/paste work
    // even when the header has focus after a click
    horizontalHeader()->installEventFilter(this);
    verticalHeader()->installEventFilter(this);

    m_multiLineDelegate = new CsvMultiLineDelegate(this);
    setItemDelegate(m_multiLineDelegate);
}

CsvGrid::~CsvGrid()
{
}

bool CsvGrid::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == horizontalHeader() || obj == verticalHeader())
        && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            copyCells();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Cut)) {
            cutCells();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Paste)) {
            pasteCells();
            return true;
        }
    }
    return QTableView::eventFilter(obj, event);
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
        int headerWidth = fm.horizontalAdvance(headerText) + 30;

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

bool CsvGrid::event(QEvent *event)
{
    // Claim ShortcutOverride for Copy/Cut/Paste so Qt delivers the key
    // event to us instead of routing it to a QAction or letting the hidden
    // QWebEngineView (Editor) consume it via Chromium's input handling.
    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        if (ke->matches(QKeySequence::Copy) ||
            ke->matches(QKeySequence::Cut) ||
            ke->matches(QKeySequence::Paste)) {
            event->accept();
            return true;
        }
    }
    return QTableView::event(event);
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
    if (!model() || model()->rowCount() == 0)
        return;

    Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    int lastRow = model()->rowCount() - 1;

    if (mods & Qt::ShiftModifier && m_lastSelectedColumn >= 0) {
        // Shift+Click: select range of columns
        int fromCol = qMin(m_lastSelectedColumn, column);
        int toCol = qMax(m_lastSelectedColumn, column);
        QItemSelection rangeSel(
            model()->index(0, fromCol),
            model()->index(lastRow, toCol));
        selectionModel()->select(rangeSel,
            QItemSelectionModel::ClearAndSelect);
    } else if (mods & Qt::ControlModifier) {
        // Ctrl+Click: toggle this column in the selection
        QItemSelection colSel(
            model()->index(0, column),
            model()->index(lastRow, column));
        selectionModel()->select(colSel,
            QItemSelectionModel::Toggle);
        m_lastSelectedColumn = column;
    } else {
        // Plain click: select only this column
        QItemSelection colSel(
            model()->index(0, column),
            model()->index(lastRow, column));
        selectionModel()->select(colSel,
            QItemSelectionModel::ClearAndSelect);
        m_lastSelectedColumn = column;
    }

    // Return focus to the grid so Ctrl+C/X/V shortcuts work
    setFocus();
}

void CsvGrid::onRowHeaderClicked(int row)
{
    if (!model() || model()->columnCount() == 0)
        return;

    Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    int lastCol = model()->columnCount() - 1;

    if (mods & Qt::ShiftModifier && m_lastSelectedRow >= 0) {
        // Shift+Click: select range from last selected row to clicked row
        int fromRow = qMin(m_lastSelectedRow, row);
        int toRow = qMax(m_lastSelectedRow, row);
        QItemSelection rangeSel(
            model()->index(fromRow, 0),
            model()->index(toRow, lastCol));
        selectionModel()->select(rangeSel,
            QItemSelectionModel::ClearAndSelect);
    } else if (mods & Qt::ControlModifier) {
        // Ctrl+Click: toggle this row in the selection
        QItemSelection rowSel(
            model()->index(row, 0),
            model()->index(row, lastCol));
        selectionModel()->select(rowSel,
            QItemSelectionModel::Toggle);
        m_lastSelectedRow = row;
    } else {
        // Plain click: select only this row
        QItemSelection rowSel(
            model()->index(row, 0),
            model()->index(row, lastCol));
        selectionModel()->select(rowSel,
            QItemSelectionModel::ClearAndSelect);
        m_lastSelectedRow = row;
    }

    // Return focus to the grid so Ctrl+C/X/V shortcuts work
    setFocus();
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

    contextMenu.addSeparator();

    QAction *flagRowAction = contextMenu.addAction(tr("Flag/Unflag Selected Rows"));
    flagRowAction->setEnabled(isCsvModel);
    QAction *unflagAllAction = contextMenu.addAction(tr("Unflag All"));
    unflagAllAction->setEnabled(isCsvModel && csvModel && csvModel->flaggedRowCount() > 0);
    QAction *invertFlagsAction = contextMenu.addAction(tr("Invert Flags"));
    invertFlagsAction->setEnabled(isCsvModel);
    QAction *deleteFlaggedAction = contextMenu.addAction(tr("Delete Flagged Rows"));
    deleteFlaggedAction->setEnabled(isCsvModel && csvModel && csvModel->flaggedRowCount() > 0);
    QAction *keepFlaggedAction = contextMenu.addAction(tr("Keep Only Flagged Rows"));
    keepFlaggedAction->setEnabled(isCsvModel && csvModel && csvModel->flaggedRowCount() > 0);

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
    } else if (selectedAction == flagRowAction && csvModel) {
        QModelIndexList sel = selectedIndexes();
        QSet<int> rows;
        for (const QModelIndex &idx : sel) rows.insert(idx.row());
        for (int r : rows) csvModel->toggleRowFlag(r);
    } else if (selectedAction == unflagAllAction && csvModel) {
        csvModel->unflagAll();
    } else if (selectedAction == invertFlagsAction && csvModel) {
        csvModel->invertFlags();
    } else if (selectedAction == deleteFlaggedAction && csvModel) {
        csvModel->deleteFlaggedRows();
    } else if (selectedAction == keepFlaggedAction && csvModel) {
        csvModel->keepOnlyFlaggedRows();
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
    if (indexes.isEmpty())
        return;

    int totalRows = m_csvModel->rowCount();
    int totalCols = m_csvModel->columnCount();

    // Determine which rows and columns are in the selection
    QMap<int, int> rowCellCount;  // row -> number of selected cells in that row
    QMap<int, int> colCellCount;  // col -> number of selected cells in that col
    for (const QModelIndex &idx : indexes) {
        rowCellCount[idx.row()]++;
        colCellCount[idx.column()]++;
    }

    // Check if we have complete row selections (all columns selected for those rows)
    QList<int> completeRows;
    for (auto it = rowCellCount.constBegin(); it != rowCellCount.constEnd(); ++it) {
        if (it.value() == totalCols) {
            completeRows.append(it.key());
        }
    }

    // Check if we have complete column selections (all rows selected for those cols)
    QList<int> completeCols;
    for (auto it = colCellCount.constBegin(); it != colCellCount.constEnd(); ++it) {
        if (it.value() == totalRows) {
            completeCols.append(it.key());
        }
    }

    // If ALL selected cells form complete rows, remove those rows
    bool allCellsInCompleteRows = !completeRows.isEmpty() &&
        (completeRows.size() * totalCols == indexes.size());

    // If ALL selected cells form complete columns, remove those columns
    bool allCellsInCompleteCols = !completeCols.isEmpty() &&
        (completeCols.size() * totalRows == indexes.size());

    CsvModel *csvModel = qobject_cast<CsvModel*>(m_csvModel);

    if (allCellsInCompleteRows) {
        // Remove entire rows (in reverse order to keep indices valid)
        std::sort(completeRows.begin(), completeRows.end(), std::greater<int>());
        for (int row : completeRows) {
            m_csvModel->removeRows(row, 1);
        }
    } else if (allCellsInCompleteCols && csvModel) {
        // Remove entire columns (in reverse order to keep indices valid)
        std::sort(completeCols.begin(), completeCols.end(), std::greater<int>());
        for (int col : completeCols) {
            csvModel->removeColumns(col, 1);
        }
    } else {
        // Clear individual cells
        if (csvModel) {
            QList<CellValue> oldValues;
            for (const QModelIndex &idx : indexes) {
                CellValue cv;
                cv.row = idx.row();
                cv.col = idx.column();
                cv.value = idx.data(Qt::DisplayRole).toString();
                oldValues.append(cv);
            }
            csvModel->undoStack()->push(new CsvCutCellsCommand(csvModel, oldValues));
        } else {
            for (const QModelIndex &index : indexes) {
                m_csvModel->setData(index, QString(), Qt::EditRole);
            }
        }
    }
}

void CsvGrid::pasteCells()
{
    if (!m_csvModel)
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

    // If model is empty, expand it to fit the pasted data
    CsvModel *csvModel = qobject_cast<CsvModel*>(m_csvModel);
    QStringList firstRowCells = rows.first().split('\t');
    int neededCols = firstRowCells.size();
    int neededRows = rows.size();

    if (m_csvModel->rowCount() == 0 || m_csvModel->columnCount() == 0) {
        if (csvModel) {
            if (csvModel->columnCount() < neededCols)
                csvModel->insertColumns(0, neededCols - csvModel->columnCount());
            if (csvModel->rowCount() < neededRows)
                csvModel->insertRows(0, neededRows - csvModel->rowCount());
        } else {
            if (m_csvModel->columnCount() < neededCols)
                m_csvModel->insertColumns(0, neededCols - m_csvModel->columnCount());
            if (m_csvModel->rowCount() < neededRows)
                m_csvModel->insertRows(0, neededRows - m_csvModel->rowCount());
        }
    }

    QModelIndex startIndex = this->currentIndex();
    if (!startIndex.isValid()) {
        // Default to top-left cell if no cell is selected (e.g. freshly opened tab)
        if (m_csvModel->rowCount() > 0 && m_csvModel->columnCount() > 0) {
            startIndex = m_csvModel->index(0, 0);
            setCurrentIndex(startIndex);
        } else {
            return;
        }
    }

    int startRow = startIndex.row();
    int startCol = startIndex.column();

    int maxRow = startRow + rows.size();
    int maxCol = startCol + firstRowCells.size();

    int addedRows = 0;
    int addedCols = 0;

    if (csvModel) {
        // Capture overwritten values for undo
        QList<CellValue> overwritten;

        if (maxRow > csvModel->rowCount()) {
            addedRows = maxRow - csvModel->rowCount();
        }
        if (maxCol > csvModel->columnCount()) {
            addedCols = maxCol - csvModel->columnCount();
        }

        // Capture existing values in paste region (before expansion)
        for (int r = 0; r < rows.size(); ++r) {
            int targetRow = startRow + r;
            if (targetRow >= csvModel->rowCount())
                break;
            QStringList cells = rows[r].split('\t');
            for (int c = 0; c < cells.size(); ++c) {
                int targetCol = startCol + c;
                if (targetCol >= csvModel->columnCount())
                    break;
                CellValue cv;
                cv.row = targetRow;
                cv.col = targetCol;
                QModelIndex idx = csvModel->index(targetRow, targetCol);
                cv.value = idx.data(Qt::DisplayRole).toString();
                overwritten.append(cv);
            }
        }

        csvModel->undoStack()->push(new CsvPasteCommand(
            csvModel, startRow, startCol, overwritten, rows, addedRows, addedCols));
    } else {
        // Non-CsvModel (lazy model) — no undo support
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

// --- Theme support ---

QList<CsvGridTheme> CsvGrid::availableThemes()
{
    QList<CsvGridTheme> themes;

    themes.append({"Light",
                   QColor(255, 255, 255), QColor(245, 248, 255),
                   QColor(230, 230, 230), QColor(200, 200, 200),
                   QColor(51, 153, 255), QColor(255, 255, 255)});

    themes.append({"Dark",
                   QColor(43, 43, 43), QColor(50, 50, 50),
                   QColor(60, 63, 65), QColor(70, 70, 70),
                   QColor(75, 110, 175), QColor(220, 220, 220)});

    themes.append({"Solarized",
                   QColor(253, 246, 227), QColor(238, 232, 213),
                   QColor(147, 161, 161), QColor(200, 200, 180),
                   QColor(38, 139, 210), QColor(253, 246, 227)});

    return themes;
}

void CsvGrid::setTheme(ThemeId theme)
{
    QList<CsvGridTheme> themes = availableThemes();
    if (theme < 0 || theme >= themes.size())
        return;

    m_currentTheme = theme;
    const CsvGridTheme &t = themes[theme];

    QString style = QString(
        "QTableView {"
        "  gridline-color: %1;"
        "  selection-background-color: %2;"
        "  selection-color: %3;"
        "}"
        "QTableView QHeaderView::section {"
        "  background-color: %4;"
        "  padding: 4px;"
        "  border: 1px solid %1;"
        "}")
        .arg(t.gridLine.name())
        .arg(t.selectedBg.name())
        .arg(t.selectedText.name())
        .arg(t.headerBg.name());

    setStyleSheet(style);

    // Force view update
    if (model()) {
        model()->layoutAboutToBeChanged();
        model()->layoutChanged();
    }
}

// --- Multi-line cell delegate ---

QWidget *CsvMultiLineDelegate::createEditor(QWidget *parent,
                                            const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const
{
    QString value = index.data(Qt::EditRole).toString();
    if (value.contains('\n') || value.contains('\r')) {
        QTextEdit *editor = new QTextEdit(parent);
        editor->setAcceptRichText(false);
        editor->setTabChangesFocus(true);
        return editor;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void CsvMultiLineDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    QTextEdit *textEdit = qobject_cast<QTextEdit*>(editor);
    if (textEdit) {
        textEdit->setPlainText(index.data(Qt::EditRole).toString());
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void CsvMultiLineDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    QTextEdit *textEdit = qobject_cast<QTextEdit*>(editor);
    if (textEdit) {
        model->setData(index, textEdit->toPlainText(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

void CsvMultiLineDelegate::paint(QPainter *painter,
                                 const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QString value = index.data(Qt::DisplayRole).toString();
    if (value.contains('\n') || value.contains('\r')) {
        // Show only first line with ellipsis indicator
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        int nlPos = value.indexOf('\n');
        if (nlPos < 0) nlPos = value.indexOf('\r');
        opt.text = value.left(nlPos) + QStringLiteral(" [...]");
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter);
        return;
    }
    QStyledItemDelegate::paint(painter, option, index);
}
