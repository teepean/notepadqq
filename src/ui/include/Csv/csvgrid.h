#ifndef CSVGRID_H
#define CSVGRID_H

#include <QTableView>
#include <QAbstractTableModel>
#include <QColor>
#include <QStyledItemDelegate>
#include <QTextEdit>

class CsvModel;

struct CsvGridTheme {
    QString name;
    QColor evenRow;
    QColor oddRow;
    QColor headerBg;
    QColor gridLine;
    QColor selectedBg;
    QColor selectedText;
};

class CsvMultiLineDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

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

    // Themes
    enum ThemeId { LightTheme = 0, DarkTheme, SolarizedTheme };
    void setTheme(ThemeId theme);
    ThemeId currentTheme() const { return m_currentTheme; }
    static QList<CsvGridTheme> availableThemes();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onHeaderClicked(int column);
    void onRowHeaderClicked(int row);
    void showContextMenu(const QPoint &pos);
    void onHeaderContextMenu(const QPoint &pos);

private:
    void showHeaderContextMenu(const QPoint &pos, int column);

    QAbstractTableModel *m_csvModel;
    int m_lastSortColumn;
    Qt::SortOrder m_lastSortOrder;
    int m_lastSelectedRow;     // For Shift+Click row range selection
    int m_lastSelectedColumn;  // For Shift+Click column range selection

    // Column clipboard
    QStringList m_copiedColumnData;
    QString m_copiedColumnHeader;
    bool m_columnCut;

    ThemeId m_currentTheme = LightTheme;
    CsvMultiLineDelegate *m_multiLineDelegate;
};

#endif // CSVGRID_H
