#pragma once

#include "core/configrepository.h"
#include "core/fileparser.h"
#include "core/tablemodel.h"

#include <QMainWindow>
#include <QStringList>

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QModelIndex;
class QPlainTextEdit;
class QPoint;
class QTabWidget;
class QTableView;
class QTableWidget;
class QTimer;

namespace rfv {

class InfoPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ConfigRepository* repository, QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString& path, bool forceChoose = false);
    void showDelimitedDocument(Document document);
    bool hasDocument() const { return m_model && m_model->document() != nullptr; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void buildUi();
    void buildMenus();

    void setDocument(Document document);
    void fillInfoPanel();
    void fillInfoBar();
    void fillHeaderTab();
    void fillFieldsTab();
    void fillDiagnosticsTab();
    void fillOverviewTab();
    void updateCounts();
    void updateStatusInfo();
    void autoSizeColumns();

    void applyFilter();
    void chooseFile();
    void chooseDefinition();
    void exportCsv();
    void exportJson();
    void saveDocument();
    void saveDocumentAs();
    bool maybeSaveChanges();
    void copySelection();
    void copySelectionOrRows();
    void copyRowsToClipboard();
    void pasteFromClipboard();
    void pasteRowsFromClipboard(const QByteArray& data);
    void pasteCellsFromClipboard();
    void selectAll();
    void showCellInfo(const QModelIndex& index);
    void showTableContextMenu(const QPoint& position);
    void duplicateSelectedRows();
    void addBlankRow();
    void resetFilterForRowEdit();
    void showAbout();

    void loadRecentFiles();
    void addRecentFile(const QString& path);
    void refreshRecentMenu();

    QList<InterfaceDefinition> allDefinitionsFor(bool ofd) const;

    ConfigRepository* m_repository = nullptr;

    TableModel* m_model = nullptr;
    QTableView* m_table = nullptr;
    QTableWidget* m_fieldsTable = nullptr;
    QTableWidget* m_overviewTable = nullptr;
    QPlainTextEdit* m_headerView = nullptr;
    QPlainTextEdit* m_diagnosticsView = nullptr;
    QTabWidget* m_tabs = nullptr;
    InfoPanel* m_infoPanel = nullptr;

    QLineEdit* m_interfaceEdit = nullptr;
    QLineEdit* m_fileTypeEdit = nullptr;
    QLineEdit* m_senderEdit = nullptr;
    QLineEdit* m_receiverEdit = nullptr;
    QLineEdit* m_dateEdit = nullptr;

    QLineEdit* m_filterEdit = nullptr;
    QCheckBox* m_regexBox = nullptr;
    QCheckBox* m_dictionaryBox = nullptr;
    QTimer* m_filterTimer = nullptr;

    QLabel* m_statusLabel = nullptr;
    QLabel* m_statusFileLabel = nullptr;
    QLabel* m_statusProtocolLabel = nullptr;
    QLabel* m_statusConfigLabel = nullptr;
    QMenu* m_recentMenu = nullptr;

    QAction* m_openAction = nullptr;
    QAction* m_chooseAction = nullptr;
    QAction* m_exportCsvAction = nullptr;
    QAction* m_exportJsonAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_selectAllAction = nullptr;
    QAction* m_dictionaryAction = nullptr;
    QAction* m_englishHeaderAction = nullptr;
    QAction* m_autoWidthAction = nullptr;
    QAction* m_toggleInfoAction = nullptr;
    QAction* m_duplicateRowAction = nullptr;
    QAction* m_addRowAction = nullptr;
    QAction* m_aboutAction = nullptr;

    QString m_currentFilePath;
    QStringList m_recentFiles;
};

} // namespace rfv
