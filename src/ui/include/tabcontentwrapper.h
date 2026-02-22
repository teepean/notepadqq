#ifndef TABCONTENTWRAPPER_H
#define TABCONTENTWRAPPER_H

#include <QStackedWidget>

namespace EditorNS {
class Editor;
}

class CsvGrid;
class CsvModel;
class LazyLoadCsvModel;
class QAbstractTableModel;

/**
 * @brief A QStackedWidget wrapper that holds an Editor on page 0
 *        and an optional CsvGrid on page 1. The Editor is always
 *        present; the CsvGrid is created lazily on first CSV mode switch.
 */
class TabContentWrapper : public QStackedWidget
{
    Q_OBJECT

public:
    enum Mode {
        TextMode = 0,
        CsvMode  = 1
    };

    explicit TabContentWrapper(EditorNS::Editor *editor, QWidget *parent = nullptr);
    ~TabContentWrapper();

    EditorNS::Editor *editor() const { return m_editor; }
    CsvGrid *csvGrid() const { return m_csvGrid; }

    Mode currentMode() const { return m_currentMode; }
    bool isCsvMode() const { return m_currentMode == CsvMode; }

    /**
     * @brief Switch to CSV grid mode.
     * @param delimiter The delimiter character. Use '\0' for auto-detect.
     */
    void switchToCsvMode(char delimiter = '\0');

    /**
     * @brief Switch back to text (Editor) mode.
     */
    void switchToTextMode();

signals:
    void modeChanged(TabContentWrapper::Mode mode);

private:
    void ensureCsvGrid();

    EditorNS::Editor *m_editor;
    CsvGrid *m_csvGrid;
    Mode m_currentMode;

    // Owned models - cleaned up on mode switch or destruction
    CsvModel *m_csvModel;
    LazyLoadCsvModel *m_lazyModel;

    static const qint64 LARGE_FILE_THRESHOLD = 100 * 1024 * 1024; // 100 MB
};

#endif // TABCONTENTWRAPPER_H
