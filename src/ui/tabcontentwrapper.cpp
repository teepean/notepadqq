#include "include/tabcontentwrapper.h"
#include "include/EditorNS/editor.h"
#include "include/Csv/csvgrid.h"
#include "include/Csv/csvmodel.h"
#include "include/Csv/csvparser.h"
#include "include/Csv/lazyloadcsvmodel.h"

#include <QFileInfo>
#include <QDebug>

TabContentWrapper::TabContentWrapper(EditorNS::Editor *editor, QWidget *parent)
    : QStackedWidget(parent)
    , m_editor(editor)
    , m_csvGrid(nullptr)
    , m_currentMode(TextMode)
    , m_csvModel(nullptr)
    , m_lazyModel(nullptr)
{
    addWidget(m_editor); // Page 0: Editor (always present)
}

TabContentWrapper::~TabContentWrapper()
{
    delete m_csvModel;
    delete m_lazyModel;
    // m_csvGrid is a child widget, deleted by QObject hierarchy
    // m_editor ownership is managed by QSharedPointer in EditorTabWidget
}

void TabContentWrapper::ensureCsvGrid()
{
    if (!m_csvGrid) {
        m_csvGrid = new CsvGrid(this);
        addWidget(m_csvGrid); // Page 1: CsvGrid
    }
}

void TabContentWrapper::switchToCsvMode(char delimiter)
{
    ensureCsvGrid();

    // Clean up previous models
    delete m_csvModel;
    m_csvModel = nullptr;
    delete m_lazyModel;
    m_lazyModel = nullptr;

    QUrl fileUrl = m_editor->filePath();
    bool isSavedFile = fileUrl.isLocalFile() && !fileUrl.isEmpty();
    qint64 fileSize = 0;

    if (isSavedFile) {
        QFileInfo fi(fileUrl.toLocalFile());
        fileSize = fi.size();
    }

    // For large saved files, use LazyLoadCsvModel directly from disk
    if (isSavedFile && fileSize > LARGE_FILE_THRESHOLD) {
        qDebug() << "Using LazyLoadCsvModel for large file:" << fileSize << "bytes";
        m_lazyModel = new LazyLoadCsvModel(this);
        m_lazyModel->loadFromFile(fileUrl.toLocalFile(), delimiter);
        m_csvGrid->setCsvModel(m_lazyModel);
    } else {
        // For small files or unsaved buffers, use CsvModel from editor content
        QString content = m_editor->value();

        // Auto-detect delimiter if not specified
        if (delimiter == '\0') {
            delimiter = CsvParser::detectDelimiterFromString(content);
        }

        qDebug() << "Using CsvModel, delimiter:" << delimiter;
        m_csvModel = new CsvModel(this);
        m_csvModel->loadFromString(content, delimiter);
        m_csvGrid->setCsvModel(m_csvModel);
    }

    m_currentMode = CsvMode;
    setCurrentIndex(1);

    emit modeChanged(CsvMode);
}

void TabContentWrapper::switchToTextMode()
{
    m_currentMode = TextMode;
    setCurrentIndex(0);

    // Clean up models to free memory
    if (m_csvGrid) {
        m_csvGrid->setCsvModel(nullptr);
    }
    delete m_csvModel;
    m_csvModel = nullptr;
    delete m_lazyModel;
    m_lazyModel = nullptr;

    emit modeChanged(TextMode);
}
