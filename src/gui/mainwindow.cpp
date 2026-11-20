#include "gui/mainwindow.h"

#include "core/exporter.h"
#include "gui/choosedialog.h"
#include "gui/filetyperefdialog.h"
#include "gui/infopanel.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPair>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace rfv {

MainWindow::MainWindow(ConfigRepository* repository, QWidget* parent)
    : QMainWindow(parent)
    , m_repository(repository)
{
    m_model = new TableModel(this);

    createActions();
    buildUi();
    buildMenus();
    loadRecentFiles();

    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("windowGeometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(1280, 800);

    const bool showInfoPanel = settings.value(QStringLiteral("showInfoPanel"), false).toBool();
    {
        const QSignalBlocker blocker(m_toggleInfoAction);
        m_toggleInfoAction->setChecked(showInfoPanel);
    }
    m_infoPanel->setVisible(showInfoPanel);

    updateStatusInfo();
    statusBar()->showMessage(QStringLiteral("就绪。可直接把接口文件拖入窗口，或按 Ctrl+O 打开。"));
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    m_openAction = new QAction(QStringLiteral("打开文件(&O)..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::chooseFile);

    m_chooseAction = new QAction(QStringLiteral("重新选择解析方案(&R)..."), this);
    m_chooseAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_R));
    m_chooseAction->setEnabled(false);
    connect(m_chooseAction, &QAction::triggered, this, &MainWindow::chooseDefinition);

    m_exportCsvAction = new QAction(QStringLiteral("导出 CSV(&C)..."), this);
    m_exportCsvAction->setEnabled(false);
    connect(m_exportCsvAction, &QAction::triggered, this, &MainWindow::exportCsv);

    m_exportJsonAction = new QAction(QStringLiteral("导出 JSON(&J)..."), this);
    m_exportJsonAction->setEnabled(false);
    connect(m_exportJsonAction, &QAction::triggered, this, &MainWindow::exportJson);

    m_saveAction = new QAction(QStringLiteral("保存(&S)"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(false);
    m_saveAction->setToolTip(QStringLiteral("把编辑后的字段写回接口文件（Ctrl+S）"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveDocument);

    m_saveAsAction = new QAction(QStringLiteral("另存为(&A)..."), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setEnabled(false);
    m_saveAsAction->setToolTip(QStringLiteral("把当前文件另存为新的接口文件（Ctrl+Shift+S）"));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);

    m_exitAction = new QAction(QStringLiteral("退出(&X)"), this);
    m_exitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    m_copyAction = new QAction(QStringLiteral("复制(&C)"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setEnabled(false);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelectionOrRows);

    m_pasteAction = new QAction(QStringLiteral("粘贴(&P)"), this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    m_pasteAction->setEnabled(false);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::pasteFromClipboard);

    m_selectAllAction = new QAction(QStringLiteral("全选(&A)"), this);
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    m_selectAllAction->setEnabled(false);
    connect(m_selectAllAction, &QAction::triggered, this, &MainWindow::selectAll);

    m_focusFilterAction = new QAction(QStringLiteral("聚焦筛选框(&F)"), this);
    m_focusFilterAction->setShortcut(QKeySequence::Find);
    connect(m_focusFilterAction, &QAction::triggered, this, [this]() {
        if (!m_filterEdit)
            return;
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });

    m_dictionaryAction = new QAction(QStringLiteral("字典翻译"), this);
    m_dictionaryAction->setCheckable(true);
    m_dictionaryAction->setChecked(true);
    connect(m_dictionaryAction, &QAction::toggled, this, [this](bool enabled) {
        m_model->setTranslateDictionary(enabled);
        if (m_dictionaryBox && m_dictionaryBox->isChecked() != enabled) {
            const QSignalBlocker blocker(m_dictionaryBox);
            m_dictionaryBox->setChecked(enabled);
        }
    });

    m_englishHeaderAction = new QAction(QStringLiteral("表头显示英文名"), this);
    m_englishHeaderAction->setCheckable(true);
    connect(m_englishHeaderAction, &QAction::toggled, this, [this](bool enabled) {
        m_model->setShowEnglishHeader(enabled);
        autoSizeColumns();
    });

    m_autoWidthAction = new QAction(QStringLiteral("自适应列宽(&W)"), this);
    m_autoWidthAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_W));
    connect(m_autoWidthAction, &QAction::triggered, this, &MainWindow::autoSizeColumns);

    m_toggleInfoAction = new QAction(QStringLiteral("显示详情面板(&I)"), this);
    m_toggleInfoAction->setCheckable(true);
    m_toggleInfoAction->setChecked(false);
    m_toggleInfoAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_I));
    m_toggleInfoAction->setToolTip(QStringLiteral("显示/隐藏左侧详情面板（Ctrl+I）"));
    connect(m_toggleInfoAction, &QAction::toggled, this, [this](bool visible) {
        if (m_infoPanel)
            m_infoPanel->setVisible(visible);
        QSettings().setValue(QStringLiteral("showInfoPanel"), visible);
    });

    m_duplicateRowAction = new QAction(QStringLiteral("复制选中行(&Y)"), this);
    m_duplicateRowAction->setEnabled(false);
    m_duplicateRowAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D));
    connect(m_duplicateRowAction, &QAction::triggered,
            this, &MainWindow::duplicateSelectedRows);

    m_addRowAction = new QAction(QStringLiteral("在末尾添加空白行(&B)"), this);
    m_addRowAction->setEnabled(false);
    m_addRowAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_N));
    connect(m_addRowAction, &QAction::triggered, this, &MainWindow::addBlankRow);

    m_deleteRowAction = new QAction(QStringLiteral("删除选中行(&D)"), this);
    m_deleteRowAction->setEnabled(false);
    m_deleteRowAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Delete));
    m_deleteRowAction->setToolTip(QStringLiteral("删除选中的行（Ctrl+Shift+Delete）"));
    connect(m_deleteRowAction, &QAction::triggered,
            this, &MainWindow::deleteSelectedRows);

    m_aboutAction = new QAction(QStringLiteral("关于(&A)..."), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    m_ofdFileTypeAction = new QAction(QStringLiteral("中登文件(&C)..."), this);
    connect(m_ofdFileTypeAction, &QAction::triggered, this, [this]() {
        showFileTypeReference(FileTypeReferenceKind::Ofd);
    });

    m_wealthFileTypeAction = new QAction(QStringLiteral("理财登文件(&W)..."), this);
    connect(m_wealthFileTypeAction, &QAction::triggered, this, [this]() {
        showFileTypeReference(FileTypeReferenceKind::WealthManagement);
    });
}

void MainWindow::buildUi()
{
    setAcceptDrops(true);
    setMinimumSize(960, 620);

    auto* toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_chooseAction);
    toolbar->addSeparator();

    auto* exportMenu = new QMenu(QStringLiteral("导出"), toolbar);
    exportMenu->addAction(m_exportCsvAction);
    exportMenu->addAction(m_exportJsonAction);

    auto* exportButton = new QToolButton(toolbar);
    exportButton->setText(QStringLiteral("导出"));
    exportButton->setMenu(exportMenu);
    exportButton->setPopupMode(QToolButton::InstantPopup);
    exportButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    exportButton->setAutoRaise(true);
    toolbar->addWidget(exportButton);

    const auto updateExportButton = [this, exportButton]() {
        exportButton->setEnabled(m_exportCsvAction->isEnabled()
                                 || m_exportJsonAction->isEnabled());
    };
    connect(m_exportCsvAction, &QAction::changed, exportButton, updateExportButton);
    connect(m_exportJsonAction, &QAction::changed, exportButton, updateExportButton);
    updateExportButton();

    toolbar->addSeparator();
    toolbar->addAction(m_saveAction);
    toolbar->addAction(m_saveAsAction);
    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(QStringLiteral(" 筛选："), this));
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(QStringLiteral("全文筛选，回车立即执行"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(320);
    toolbar->addWidget(m_filterEdit);

    auto* filterGap = new QWidget(this);
    filterGap->setFixedWidth(12);
    toolbar->addWidget(filterGap);

    m_regexBox = new QCheckBox(QStringLiteral("正则"), this);
    toolbar->addWidget(m_regexBox);

    auto* optionGap = new QWidget(this);
    optionGap->setFixedWidth(8);
    toolbar->addWidget(optionGap);

    m_dictionaryBox = new QCheckBox(QStringLiteral("字典"), this);
    m_dictionaryBox->setChecked(true);
    toolbar->addWidget(m_dictionaryBox);

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    m_infoPanel = new InfoPanel(splitter);
    m_infoPanel->setMinimumWidth(320);
    m_infoPanel->setMaximumWidth(620);

    m_tabs = new QTabWidget(splitter);

    auto* dataPage = new QWidget(m_tabs);
    auto* dataLayout = new QVBoxLayout(dataPage);
    dataLayout->setContentsMargins(0, 0, 0, 0);
    dataLayout->setSpacing(0);

    auto* infoBar = new QWidget(dataPage);
    infoBar->setObjectName(QStringLiteral("infoBar"));
    auto* infoLayout = new QHBoxLayout(infoBar);
    infoLayout->setContentsMargins(6, 5, 6, 5);
    infoLayout->setSpacing(6);

    const auto addInfoField = [&](const QString& title, QLineEdit** edit,
                                  int minimumWidth, int stretch) {
        auto* label = new QLabel(title, infoBar);
        label->setStyleSheet(QStringLiteral("QLabel { color: #6b7280; }"));
        auto* line = new QLineEdit(infoBar);
        line->setReadOnly(true);
        line->setMinimumWidth(minimumWidth);
        line->setPlaceholderText(QStringLiteral("-"));
        infoLayout->addWidget(label);
        infoLayout->addWidget(line, stretch);
        *edit = line;
    };

    addInfoField(QStringLiteral("接口"), &m_interfaceEdit, 160, 3);
    addInfoField(QStringLiteral("文件类型"), &m_fileTypeEdit, 56, 0);
    addInfoField(QStringLiteral("发送方"), &m_senderEdit, 90, 1);
    addInfoField(QStringLiteral("接收方"), &m_receiverEdit, 90, 1);
    addInfoField(QStringLiteral("文件日期"), &m_dateEdit, 84, 0);
    dataLayout->addWidget(infoBar);

    m_table = new QTableView(dataPage);
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setCornerButtonEnabled(false);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setDefaultSectionSize(130);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::EditKeyPressed);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    dataLayout->addWidget(m_table, 1);
    m_tabs->addTab(dataPage, QStringLiteral("数据"));

    m_headerView = new QPlainTextEdit(m_tabs);
    m_headerView->setReadOnly(true);
    m_headerView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tabs->addTab(m_headerView, QStringLiteral("文件头"));

    m_fieldsTable = new QTableWidget(m_tabs);
    m_fieldsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fieldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldsTable->setAlternatingRowColors(true);
    m_fieldsTable->verticalHeader()->setVisible(false);
    m_tabs->addTab(m_fieldsTable, QStringLiteral("字段定义"));

    m_diagnosticsView = new QPlainTextEdit(m_tabs);
    m_diagnosticsView->setReadOnly(true);
    m_diagnosticsView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tabs->addTab(m_diagnosticsView, QStringLiteral("诊断"));

    m_overviewTable = new QTableWidget(m_tabs);
    m_overviewTable->setColumnCount(2);
    m_overviewTable->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("项目") << QStringLiteral("值"));
    m_overviewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_overviewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_overviewTable->setAlternatingRowColors(true);
    m_overviewTable->verticalHeader()->setVisible(false);
    m_overviewTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_overviewTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tabs->addTab(m_overviewTable, QStringLiteral("数据概况"));

    splitter->addWidget(m_infoPanel);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 360 << 920);

    setCentralWidget(splitter);

    m_statusFileLabel = new QLabel(this);
    m_statusProtocolLabel = new QLabel(this);
    m_statusConfigLabel = new QLabel(this);
    m_statusLabel = new QLabel(this);

    const QList<QLabel*> statusLabels = {m_statusFileLabel, m_statusProtocolLabel,
                                         m_statusConfigLabel, m_statusLabel};
    for (QLabel* label : statusLabels) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setStyleSheet(QStringLiteral("QLabel { color: #4b5563; }"));
    }
    m_statusFileLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    const auto addStatusSeparator = [this]() {
        auto* separator = new QLabel(QStringLiteral("│"), this);
        separator->setStyleSheet(QStringLiteral("QLabel { color: #c9ced8; }"));
        statusBar()->addPermanentWidget(separator);
    };

    statusBar()->addPermanentWidget(m_statusFileLabel, 1);
    addStatusSeparator();
    statusBar()->addPermanentWidget(m_statusProtocolLabel);
    addStatusSeparator();
    statusBar()->addPermanentWidget(m_statusConfigLabel);
    addStatusSeparator();
    statusBar()->addPermanentWidget(m_statusLabel);

    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(220);
    connect(m_filterTimer, &QTimer::timeout, this, &MainWindow::applyFilter);

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        m_filterTimer->start();
    });
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &MainWindow::applyFilter);
    connect(m_regexBox, &QCheckBox::toggled, this, [this](bool) { applyFilter(); });
    connect(m_dictionaryBox, &QCheckBox::toggled, this, [this](bool enabled) {
        m_model->setTranslateDictionary(enabled);
        if (m_dictionaryAction->isChecked() != enabled) {
            const QSignalBlocker blocker(m_dictionaryAction);
            m_dictionaryAction->setChecked(enabled);
        }
    });

    connect(m_table, &QTableView::customContextMenuRequested,
            this, &MainWindow::showTableContextMenu);
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (index.isValid() && index.column() == 0)
            m_table->selectRow(index.row());
    });
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this]() {
        fillOverviewTab();
        if (m_saveAction)
            m_saveAction->setEnabled(m_model->hasUnsavedChanges());
    });
    connect(m_infoPanel, &InfoPanel::diagnosticsRequested, this, [this]() {
        m_tabs->setCurrentWidget(m_diagnosticsView);
    });
    connect(m_infoPanel, &InfoPanel::closeRequested, this, [this]() {
        m_toggleInfoAction->setChecked(false);
    });
}

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(m_openAction);
    m_recentMenu = fileMenu->addMenu(QStringLiteral("最近打开(&R)"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportCsvAction);
    fileMenu->addAction(m_exportJsonAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    auto* parseMenu = menuBar()->addMenu(QStringLiteral("解析(&P)"));
    parseMenu->addAction(m_chooseAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addAction(m_selectAllAction);
    editMenu->addSeparator();
    editMenu->addAction(m_duplicateRowAction);
    editMenu->addAction(m_addRowAction);
    editMenu->addAction(m_deleteRowAction);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(m_toggleInfoAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_dictionaryAction);
    viewMenu->addAction(m_englishHeaderAction);
    viewMenu->addAction(m_autoWidthAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_focusFilterAction);

    auto* referenceMenu = menuBar()->addMenu(QStringLiteral("文件类型参考(&T)"));
    referenceMenu->addAction(m_ofdFileTypeAction);
    referenceMenu->addAction(m_wealthFileTypeAction);

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::openFile(const QString& path, bool forceChoose)
{
    if (path.isEmpty())
        return;

    const QString absolutePath = QFileInfo(path).absoluteFilePath();

    FileProbe probe;
    QString error;
    if (!FileParser::probe(absolutePath, probe, &error)) {
        QMessageBox::warning(this, QStringLiteral("无法打开文件"), error);
        return;
    }

    // 内部清算等竖线分隔文件：直接在当前窗口打开
    if (probe.isDelimited) {
        if (!maybeSaveChanges())
            return;

        Document document;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = FileParser::parseDelimited(absolutePath, document, &error);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::critical(this, QStringLiteral("解析失败"), error);
            return;
        }

        showDelimitedDocument(std::move(document));
        addRecentFile(absolutePath);
        QSettings().setValue(QStringLiteral("lastDir"),
                             QFileInfo(absolutePath).absolutePath());
        statusBar()->showMessage(
            QStringLiteral("已打开：%1").arg(QFileInfo(absolutePath).fileName()), 5000);
        return;
    }

    if (!maybeSaveChanges())
        return;

    QList<InterfaceDefinition> candidates;
    if (!forceChoose) {
        candidates = m_repository->candidates(probe.isOfd, probe.version, probe.fileType,
                                              probe.fileName, probe.firstLine);
    }

    // 若多套配置同时命中，用文件头里的字段名给候选打分：
    // 只有一套明显最匹配时自动选中，否则交给用户确认。
    if (!forceChoose && candidates.size() > 1 && !probe.headerFieldNames.isEmpty()) {
        std::stable_sort(candidates.begin(), candidates.end(),
                         [&probe](const InterfaceDefinition& a,
                                  const InterfaceDefinition& b) {
            return headerFieldMatchCount(a, probe.headerFieldNames)
                    > headerFieldMatchCount(b, probe.headerFieldNames);
        });

        const int best = headerFieldMatchCount(candidates.at(0), probe.headerFieldNames);
        const int second = headerFieldMatchCount(candidates.at(1), probe.headerFieldNames);
        const int comparable = qMin(probe.headerFieldNames.size(),
                                    candidates.at(0).fieldCount);
        if (best >= 5 && best > second && best * 10 >= comparable * 8) {
            const InterfaceDefinition bestDefinition = candidates.at(0);
            candidates.clear();
            candidates.append(bestDefinition);
        }
    }

    if (forceChoose || candidates.isEmpty()) {
        if (!forceChoose) {
            QMessageBox::information(this, QStringLiteral("无法自动识别"),
                QStringLiteral("未能自动识别该文件的解析方案。\n\n"
                               "首行：%1\n版本：%2\n文件类型：%3\n\n"
                               "请在下一个窗口中选择解析方案。")
                    .arg(probe.firstLine,
                         probe.version.isEmpty() ? QStringLiteral("-") : probe.version,
                         probe.fileType.isEmpty() ? QStringLiteral("-") : probe.fileType));
        }
        candidates = allDefinitionsFor(probe.isOfd);
        if (candidates.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("缺少解析配置"),
                QStringLiteral("conf 目录中没有可用的 %1 解析配置。")
                    .arg(probe.isOfd ? QStringLiteral("OFD") : QStringLiteral("定长")));
            return;
        }
    }

    InterfaceDefinition chosen;
    if (candidates.size() > 1) {
        ChooseDialog dialog(candidates, probe, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        chosen = dialog.selected();
    } else {
        chosen = candidates.first();
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    Document document;
    const bool ok = FileParser::parse(absolutePath, chosen, document, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("解析失败"), error);
        return;
    }

    m_currentFilePath = absolutePath;
    setDocument(std::move(document));
    addRecentFile(absolutePath);

    QSettings().setValue(QStringLiteral("lastDir"),
                         QFileInfo(absolutePath).absolutePath());
}

void MainWindow::showDelimitedDocument(Document document)
{
    m_currentFilePath = document.filePath;
    setDocument(std::move(document));

    // 分隔符文件没有预设解析方案可选
    m_chooseAction->setEnabled(false);
    m_chooseAction->setToolTip(QStringLiteral("内部清算文件不支持重新选择解析方案"));
}

QList<InterfaceDefinition> MainWindow::allDefinitionsFor(bool ofd) const
{
    QList<InterfaceDefinition> result;
    const QVector<InterfaceDefinition>& source =
            ofd ? m_repository->ofdDefinitions() : m_repository->fixedDefinitions();
    for (const InterfaceDefinition& def : source) {
        if (def.usable)
            result.append(def);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const InterfaceDefinition& a, const InterfaceDefinition& b) {
        if (a.version != b.version)
            return a.version > b.version;
        if (a.protocol != b.protocol)
            return a.protocol < b.protocol;
        return a.section < b.section;
    });
    return result;
}

void MainWindow::setDocument(Document document)
{
    m_table->setUpdatesEnabled(false);
    m_model->setDocument(std::move(document));
    m_table->setUpdatesEnabled(true);

    const Document* doc = m_model->document();
    if (!doc)
        return;

    if (!m_filterEdit->text().isEmpty())
        m_filterEdit->clear();

    fillInfoPanel();
    fillInfoBar();
    fillHeaderTab();
    fillFieldsTab();
    fillDiagnosticsTab();
    fillOverviewTab();
    autoSizeColumns();

    m_table->scrollToTop();
    m_tabs->setCurrentIndex(0);
    setWindowTitle(QStringLiteral("%1 - RegFileViewer")
                   .arg(QFileInfo(doc->filePath).fileName()));

    const bool hasData = doc->rowCount() > 0;
    m_exportCsvAction->setEnabled(hasData);
    m_exportJsonAction->setEnabled(hasData);
    m_copyAction->setEnabled(hasData);
    m_pasteAction->setEnabled(true);
    m_selectAllAction->setEnabled(hasData);
    m_chooseAction->setEnabled(true);
    m_chooseAction->setToolTip(QString());
    m_duplicateRowAction->setEnabled(hasData);
    m_addRowAction->setEnabled(hasData);
    m_deleteRowAction->setEnabled(hasData);
    m_saveAction->setEnabled(m_model->hasUnsavedChanges());
    m_saveAsAction->setEnabled(true);

    updateStatusInfo();
    updateCounts();

    const int diagnosticIndex = m_tabs->indexOf(m_diagnosticsView);
    if (!doc->warnings.isEmpty()) {
        m_tabs->setTabText(diagnosticIndex,
                           QStringLiteral("诊断 (%1)").arg(doc->warnings.size()));
        statusBar()->showMessage(
            QStringLiteral("解析完成，有 %1 条提示，详见“诊断”页").arg(doc->warnings.size()),
            10000);
    } else {
        m_tabs->setTabText(diagnosticIndex, QStringLiteral("诊断"));
        statusBar()->showMessage(QStringLiteral("解析完成"), 5000);
    }
}

void MainWindow::refreshDocumentViews()
{
    if (!m_model->document())
        return;

    const Document* doc = m_model->document();
    const bool hasDocumentRows = doc->rowCount() > 0;
    const bool hasVisibleRows = m_model->rowCount() > 0;
    m_exportCsvAction->setEnabled(hasDocumentRows);
    m_exportJsonAction->setEnabled(hasDocumentRows);
    m_copyAction->setEnabled(hasDocumentRows);
    m_selectAllAction->setEnabled(hasDocumentRows);
    m_duplicateRowAction->setEnabled(hasVisibleRows);
    m_deleteRowAction->setEnabled(hasVisibleRows);
    m_addRowAction->setEnabled(true);

    fillInfoPanel();
    fillInfoBar();
    fillHeaderTab();
    fillDiagnosticsTab();
    fillOverviewTab();
    updateStatusInfo();
}

void MainWindow::fillInfoPanel()
{
    m_infoPanel->setDocument(m_model->document(), m_repository);
}

void MainWindow::fillInfoBar()
{
    const Document* doc = m_model->document();
    if (!doc) {
        const QList<QLineEdit*> edits = {m_interfaceEdit, m_fileTypeEdit, m_senderEdit,
                                         m_receiverEdit, m_dateEdit};
        for (QLineEdit* edit : edits) {
            if (!edit)
                continue;
            edit->clear();
            edit->setToolTip(QString());
        }
        return;
    }

    const InterfaceDefinition& definition = doc->definition;
    m_interfaceEdit->setText(definition.describe);
    m_interfaceEdit->setToolTip(definition.describe);

    QString fileType;
    QString sender;
    QString receiver;
    QString date;

    if (definition.layout == Layout::Delimited) {
        fileType = definition.fileType;
        sender = doc->senderCode;
        date = doc->fileDate;
    } else if (definition.isOfd() && doc->headerLines.size() >= 7) {
        sender = doc->headerLines.at(2).trimmed();
        receiver = doc->headerLines.at(3).trimmed();
        date = doc->headerLines.at(4).trimmed();
        fileType = doc->headerLines.at(6).trimmed();
    } else {
        const QRegularExpression pattern(QStringLiteral("(?:^|[-_])(\\d{3})(?:[-_.]|$)"));
        const QRegularExpressionMatch match = pattern.match(definition.filePattern);
        if (match.hasMatch())
            fileType = match.captured(1);
    }

    const auto describeCode = [this](const QString& code) {
        if (code.isEmpty())
            return QString();
        const QString name = m_repository ? m_repository->codeName(code) : QString();
        return name.isEmpty() ? code : QStringLiteral("%1 · %2").arg(code, name);
    };

    const QString senderText = describeCode(sender);
    const QString receiverText = describeCode(receiver);

    m_fileTypeEdit->setText(fileType.isEmpty() ? QStringLiteral("-") : fileType);
    m_senderEdit->setText(senderText.isEmpty() ? QStringLiteral("-") : senderText);
    m_senderEdit->setToolTip(senderText);
    m_receiverEdit->setText(receiverText.isEmpty() ? QStringLiteral("-") : receiverText);
    m_receiverEdit->setToolTip(receiverText);
    m_dateEdit->setText(date.isEmpty() ? QStringLiteral("-") : date);
}

void MainWindow::fillOverviewTab()
{
    if (!m_overviewTable)
        return;

    const Document* doc = m_model->document();
    if (!doc) {
        m_overviewTable->clearContents();
        m_overviewTable->setRowCount(0);
        return;
    }

    const InterfaceDefinition& definition = doc->definition;
    QVector<QPair<QString, QString>> rows;
    const auto add = [&rows](const QString& key, const QString& value) {
        rows.append(qMakePair(key, value));
    };

    add(QStringLiteral("文件"), doc->filePath);
    add(QStringLiteral("协议"),
        definition.versionLabel.isEmpty()
        ? definition.protocol
        : QStringLiteral("%1 %2").arg(definition.protocol, definition.versionLabel));
    add(QStringLiteral("接口"), definition.describe);
    add(definition.isDelimited() ? QStringLiteral("数据文件")
                                 : QStringLiteral("配置文件"),
        definition.configFile);
    if (definition.isOfd())
        add(QStringLiteral("配置段"), definition.section);
    else if (definition.isDelimited())
        add(QStringLiteral("分隔符"), definition.delimiter);
    else
        add(QStringLiteral("文件规则"), definition.filePattern);

    add(QStringLiteral("记录数"),
        doc->declaredRecordCount >= 0
        ? QStringLiteral("声明 %1 / 实际 %2")
          .arg(doc->declaredRecordCount).arg(doc->rowCount())
        : QString::number(doc->rowCount()));
    add(QStringLiteral("字段数"), QString::number(definition.fieldCount));
    if (!definition.isDelimited()) {
        add(QStringLiteral("行长度"),
            QStringLiteral("%1 %2").arg(definition.rowLength)
            .arg(definition.lengthType == 0 ? QStringLiteral("字节") : QStringLiteral("字符")));
    }
    add(QStringLiteral("数据起始行"), QString::number(definition.dataBeginRow));
    add(QStringLiteral("编码"), definition.encoding);
    add(QStringLiteral("布局"), layoutLabel(definition.layout));
    if (definition.lastRowCheck.isEmpty()) {
        add(QStringLiteral("结束标识"), QStringLiteral("不适用"));
    } else {
        add(QStringLiteral("结束标识"),
            doc->sawEndMarker
            ? QStringLiteral("已找到 · %1").arg(definition.lastRowCheck)
            : QStringLiteral("未找到"));
    }
    add(QStringLiteral("解析耗时"), QStringLiteral("%1 ms").arg(doc->parseMilliseconds));
    add(QStringLiteral("当前列数"), QString::number(m_model->dataColumnCount()));
    add(QStringLiteral("新增行"), QString::number(m_model->addedRowCount()));
    add(QStringLiteral("已修改单元格"), QString::number(m_model->modifiedCellCount()));
    add(QStringLiteral("提示"), QString::number(doc->warnings.size()));

    m_overviewTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        auto* keyItem = new QTableWidgetItem(rows.at(row).first);
        keyItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto* valueItem = new QTableWidgetItem(rows.at(row).second);
        valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        valueItem->setToolTip(rows.at(row).second);
        m_overviewTable->setItem(row, 0, keyItem);
        m_overviewTable->setItem(row, 1, valueItem);
    }
    m_overviewTable->resizeColumnToContents(0);
}

void MainWindow::updateStatusInfo()
{
    if (!m_statusFileLabel || !m_statusProtocolLabel
        || !m_statusConfigLabel || !m_statusLabel) {
        return;
    }

    const Document* doc = m_model->document();
    if (!doc) {
        m_statusFileLabel->setText(QStringLiteral("文件：未打开"));
        m_statusFileLabel->setToolTip(QString());
        m_statusProtocolLabel->setText(QStringLiteral("协议：-"));
        m_statusConfigLabel->setText(QStringLiteral("配置：-"));
        m_statusLabel->setText(QStringLiteral("未打开文件"));
        return;
    }

    const InterfaceDefinition& definition = doc->definition;
    const QString path = doc->filePath;

    int available = m_statusFileLabel->width() - 16;
    if (available < 240)
        available = 600;
    const QString elidedPath = QFontMetrics(m_statusFileLabel->font())
            .elidedText(path, Qt::ElideMiddle, available);

    m_statusFileLabel->setText(QStringLiteral("文件：%1").arg(elidedPath));
    m_statusFileLabel->setToolTip(path);

    const QString protocol = definition.versionLabel.isEmpty()
            ? definition.protocol
            : QStringLiteral("%1 %2").arg(definition.protocol, definition.versionLabel);
    m_statusProtocolLabel->setText(QStringLiteral("协议：%1").arg(protocol));
    m_statusProtocolLabel->setToolTip(protocol);

    const QString configLabel = definition.isDelimited()
            ? QStringLiteral("数据文件") : QStringLiteral("配置");
    m_statusConfigLabel->setText(QStringLiteral("%1：%2")
                                 .arg(configLabel, definition.configFile));
    m_statusConfigLabel->setToolTip(definition.configFile);

    updateCounts();
}

void MainWindow::fillHeaderTab()
{
    const Document* doc = m_model->document();
    if (!doc) {
        m_headerView->clear();
        return;
    }

    QStringList lines;
    lines.reserve(doc->headerLines.size() + 2);
    lines << QStringLiteral("文件头共 %1 行（数据从第 %2 行开始）")
             .arg(doc->headerLines.size())
             .arg(doc->definition.dataBeginRow)
          << QString();

    for (int i = 0; i < doc->headerLines.size(); ++i) {
        lines << QStringLiteral("%1 | %2")
                 .arg(i + 1, 4, 10, QLatin1Char(' '))
                 .arg(doc->headerLines.at(i));
    }
    m_headerView->setPlainText(lines.join(QStringLiteral("\n")));
}

void MainWindow::fillFieldsTab()
{
    const Document* doc = m_model->document();
    if (!doc) {
        m_fieldsTable->clear();
        m_fieldsTable->setRowCount(0);
        m_fieldsTable->setColumnCount(0);
        return;
    }

    const InterfaceDefinition& def = doc->definition;

    m_fieldsTable->clear();
    m_fieldsTable->setColumnCount(7);
    m_fieldsTable->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("序号")
        << QStringLiteral("中文名")
        << QStringLiteral("英文名")
        << QStringLiteral("类型")
        << QStringLiteral("长度")
        << QStringLiteral("小数位")
        << QStringLiteral("字典项"));
    m_fieldsTable->setRowCount(def.fields.size());

    for (int row = 0; row < def.fields.size(); ++row) {
        const FieldDefinition& field = def.fields.at(row);

        auto* indexItem = new QTableWidgetItem(QString::number(field.index));
        indexItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fieldsTable->setItem(row, 0, indexItem);

        m_fieldsTable->setItem(row, 1, new QTableWidgetItem(field.nameCn));
        m_fieldsTable->setItem(row, 2, new QTableWidgetItem(field.nameEn));
        m_fieldsTable->setItem(row, 3, new QTableWidgetItem(QString(field.type)));

        auto* lengthItem = new QTableWidgetItem(QString::number(field.length));
        lengthItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fieldsTable->setItem(row, 4, lengthItem);

        auto* decimalItem = new QTableWidgetItem(QString::number(field.decimals));
        decimalItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fieldsTable->setItem(row, 5, decimalItem);

        int dictionaryCount = 0;
        if (def.dictionaries) {
            auto it = def.dictionaries->constFind(field.nameEn);
            if (it == def.dictionaries->constEnd())
                it = def.dictionaries->constFind(field.nameCn);
            if (it != def.dictionaries->constEnd())
                dictionaryCount = it->size();
        }
        auto* dictItem = new QTableWidgetItem(
                    dictionaryCount > 0 ? QString::number(dictionaryCount) : QString());
        dictItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_fieldsTable->setItem(row, 6, dictItem);

        if (!field.tip.isEmpty())
            m_fieldsTable->item(row, 1)->setToolTip(field.tip);
    }

    // 所有列都保持可手动拖拽调整；中文名列只设置一个初始宽度，不再强制拉伸
    QHeaderView* fieldHeader = m_fieldsTable->horizontalHeader();
    fieldHeader->setSectionResizeMode(QHeaderView::Interactive);
    fieldHeader->setStretchLastSection(false);
    fieldHeader->setMinimumSectionSize(48);
    m_fieldsTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_fieldsTable->resizeColumnsToContents();
    m_fieldsTable->setColumnWidth(1, qBound(180, m_fieldsTable->columnWidth(1), 420));
    m_fieldsTable->scrollToTop();
}

void MainWindow::fillDiagnosticsTab()
{
    const Document* doc = m_model->document();
    if (!doc) {
        m_diagnosticsView->clear();
        return;
    }

    const InterfaceDefinition& def = doc->definition;
    QStringList lines;
    lines << QStringLiteral("%1：%2")
             .arg(def.isDelimited() ? QStringLiteral("数据文件")
                                    : QStringLiteral("解析配置"),
                  def.configFile);
    if (def.isOfd())
        lines << QStringLiteral("配置段　：%1").arg(def.section);
    else if (def.isDelimited())
        lines << QStringLiteral("分隔符　：%1").arg(def.delimiter);
    else
        lines << QStringLiteral("文件规则：%1").arg(def.filePattern);
    lines << QStringLiteral("协议　　：%1")
             .arg(def.versionLabel.isEmpty()
                  ? def.protocol
                  : QStringLiteral("%1 %2").arg(def.protocol, def.versionLabel));
    lines << QStringLiteral("接口说明：%1").arg(def.describe);
    lines << QStringLiteral("字段数　：%1").arg(def.fieldCount);
    if (!def.isDelimited()) {
        lines << QStringLiteral("行长度　：%1（%2）").arg(def.rowLength)
             .arg(def.lengthType == 0 ? QStringLiteral("字节") : QStringLiteral("字符"));
    }
    lines << QStringLiteral("数据起始：第 %1 行").arg(def.dataBeginRow);
    lines << QStringLiteral("文件编码：%1").arg(def.encoding);
    lines << QStringLiteral("记录数　：声明 %1，实际 %2")
             .arg(doc->declaredRecordCount >= 0 ? QString::number(doc->declaredRecordCount)
                                                : QStringLiteral("-"))
             .arg(doc->rowCount());
    if (def.lastRowCheck.isEmpty()) {
        lines << QStringLiteral("结束标识：不适用");
    } else {
        lines << QStringLiteral("结束标识：%1").arg(doc->sawEndMarker
            ? QStringLiteral("已找到") : QStringLiteral("未找到"));
    }
    lines << QStringLiteral("解析耗时：%1 ms").arg(doc->parseMilliseconds);
    lines << QString();

    if (doc->warnings.isEmpty()) {
        lines << QStringLiteral("没有发现异常。");
    } else {
        lines << QStringLiteral("提示（%1）：").arg(doc->warnings.size());
        int index = 1;
        for (const QString& warning : doc->warnings) {
            lines << QStringLiteral("%1. %2").arg(index++).arg(warning);
        }
    }

    m_diagnosticsView->setPlainText(lines.join(QStringLiteral("\n")));
}

void MainWindow::updateCounts()
{
    const Document* doc = m_model->document();
    if (!doc) {
        m_statusLabel->setText(QStringLiteral("未打开文件"));
        return;
    }

    m_statusLabel->setText(QStringLiteral("共 %1 行 / 显示 %2 行 · %3 列 · %4 ms")
                           .arg(doc->rowCount())
                           .arg(m_model->rowCount())
                           .arg(m_model->dataColumnCount())
                           .arg(doc->parseMilliseconds));
}

void MainWindow::autoSizeColumns()
{
    if (!m_model->document())
        return;

    const QFontMetrics metrics(m_table->font());
    const int sampleRows = qMin(m_model->rowCount(), 200);
    const int columns = m_model->columnCount();

    for (int column = 0; column < columns; ++column) {
        int width = m_table->horizontalHeader()->sectionSizeHint(column);
        for (int row = 0; row < sampleRows; ++row) {
            const QString text = m_model->data(m_model->index(row, column),
                                               Qt::DisplayRole).toString();
            width = qMax(width, metrics.horizontalAdvance(text) + 24);
        }
        m_table->setColumnWidth(column, qBound(64, width, 380));
    }
}

void MainWindow::applyFilter()
{
    if (!m_model->document())
        return;

    const QString filterText = m_filterEdit->text();
    if (m_regexBox->isChecked() && !filterText.trimmed().isEmpty()) {
        const QRegularExpression regex(filterText);
        if (!regex.isValid()) {
            m_filterEdit->setStyleSheet(
                        QStringLiteral("QLineEdit { border: 1px solid #dc2626; }"));
            m_filterEdit->setToolTip(
                        QStringLiteral("正则表达式无效：%1").arg(regex.errorString()));
            statusBar()->showMessage(
                        QStringLiteral("正则表达式无效：%1").arg(regex.errorString()),
                        8000);
            return;
        }
    }
    m_filterEdit->setStyleSheet(QString());
    m_filterEdit->setToolTip(QString());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_model->setFilter(filterText, m_regexBox->isChecked());
    QApplication::restoreOverrideCursor();
    updateCounts();

    if (!filterText.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("筛选 “%1”：显示 %2 / %3 行")
                .arg(filterText)
                .arg(m_model->rowCount())
                .arg(m_model->document()->rowCount()), 5000);
    }
}

void MainWindow::chooseFile()
{
    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("lastDir"),
                                           QDir::homePath()).toString();

    const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("打开接口数据文件"), lastDir,
                QStringLiteral("接口数据文件 (*.txt *.TXT *.dat *.DAT *.ofd *.OFD);;"
                               "所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    settings.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    openFile(path);
}

void MainWindow::chooseDefinition()
{
    if (m_currentFilePath.isEmpty()) {
        chooseFile();
        return;
    }
    openFile(m_currentFilePath, true);
}

void MainWindow::exportCsv()
{
    const Document* doc = m_model->document();
    if (!doc || doc->rowCount() == 0)
        return;
    if (m_model->rowCount() == 0) {
        statusBar()->showMessage(QStringLiteral("当前筛选结果为空，没有可导出的行"), 4000);
        return;
    }

    const QFileInfo info(doc->filePath);
    const QString suggested = info.absolutePath() + QLatin1Char('/')
            + info.completeBaseName() + QStringLiteral(".csv");

    const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("导出 CSV"), suggested,
                QStringLiteral("CSV 文件 (*.csv);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    ExportOptions options;
    options.delimiter = QLatin1Char(',');
    options.includeHeader = true;
    const auto dictionaries = doc->definition.dictionaries;
    options.translate = m_model->translateDictionary()
            && dictionaries && !dictionaries->isEmpty();

    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = Exporter::exportCsv(path, *m_model, m_model->visibleRows(), options, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已导出 %1 行到 %2")
                             .arg(m_model->rowCount()).arg(path), 8000);
}

void MainWindow::exportJson()
{
    const Document* doc = m_model->document();
    if (!doc || doc->rowCount() == 0)
        return;
    if (m_model->rowCount() == 0) {
        statusBar()->showMessage(QStringLiteral("当前筛选结果为空，没有可导出的行"), 4000);
        return;
    }

    const QFileInfo info(doc->filePath);
    const QString suggested = info.absolutePath() + QLatin1Char('/')
            + info.completeBaseName() + QStringLiteral(".json");

    const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("导出 JSON"), suggested,
                QStringLiteral("JSON 文件 (*.json);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = Exporter::exportJson(path, *m_model, m_model->visibleRows(),
                                         ExportOptions(), &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已导出 %1 行到 %2")
                             .arg(m_model->rowCount()).arg(path), 8000);
}

void MainWindow::saveDocument()
{
    const Document* doc = m_model->document();
    if (!doc)
        return;

    if (!m_model->hasUnsavedChanges()) {
        if (m_model->hasExtraColumnValues()) {
            statusBar()->showMessage(
                QStringLiteral("新增列数据请使用“导出 CSV/JSON”保存"), 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("没有需要保存的修改"), 3000);
        }
        return;
    }

    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = m_model->saveToFile(doc->filePath, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), error);
        return;
    }

    m_saveAction->setEnabled(false);
    refreshDocumentViews();
    statusBar()->showMessage(QStringLiteral("已保存到 %1").arg(doc->filePath), 6000);

    if (m_model->hasExtraColumnValues()) {
        QMessageBox::information(this, QStringLiteral("保存完成"),
            QStringLiteral("接口字段已写回文件。\n\n"
                           "新增列不会写入接口文件，如需保留请使用“导出 CSV/JSON”。"));
    }
}

void MainWindow::saveDocumentAs()
{
    const Document* doc = m_model->document();
    if (!doc)
        return;

    const QFileInfo info(doc->filePath);
    const QString suggested = info.absoluteFilePath();

    const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("另存为"), suggested,
                QStringLiteral("接口数据文件 (*.txt *.TXT *.dat *.DAT);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = m_model->saveToFile(path, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("另存为失败"), error);
        return;
    }

    m_currentFilePath = QFileInfo(path).absoluteFilePath();
    addRecentFile(m_currentFilePath);
    setWindowTitle(QStringLiteral("%1 - RegFileViewer")
                   .arg(QFileInfo(m_currentFilePath).fileName()));

    m_saveAction->setEnabled(false);
    refreshDocumentViews();
    statusBar()->showMessage(QStringLiteral("已另存为 %1").arg(m_currentFilePath), 6000);

    if (m_model->hasExtraColumnValues()) {
        QMessageBox::information(this, QStringLiteral("另存为完成"),
            QStringLiteral("接口字段已写入新文件。\n\n"
                           "新增列不会写入接口文件，如需保留请使用“导出 CSV/JSON”。"));
    }
}

bool MainWindow::maybeSaveChanges()
{
    if (m_model && m_model->hasUnsavedChanges()) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
                    this, QStringLiteral("未保存的修改"),
                    QStringLiteral("当前接口文件有未保存的字段修改，是否保存？"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Save);

        if (reply == QMessageBox::Cancel || reply == QMessageBox::NoButton)
            return false;
        if (reply == QMessageBox::Discard)
            return true;

        saveDocument();
        if (m_model->hasUnsavedChanges())
            return false;
    }

    if (m_model && m_model->hasExtraColumnValues()) {
        const QMessageBox::StandardButton reply = QMessageBox::warning(
                    this, QStringLiteral("新增列数据"),
                    QStringLiteral("新增列数据不会写入接口文件，只能通过"
                                   "“导出 CSV/JSON”保留。\n\n继续将丢失这些新增列数据。"),
                    QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Cancel);
        if (reply != QMessageBox::Discard)
            return false;
    }

    return true;
}

void MainWindow::copySelectionOrRows()
{
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
        edit->copy();
        return;
    }

    if (m_model->document() && m_table->selectionModel()
        && !m_table->selectionModel()->selectedRows().isEmpty()) {
        copyRowsToClipboard();
        return;
    }

    copySelection();
}

void MainWindow::copyRowsToClipboard()
{
    const Document* doc = m_model->document();
    if (!doc || !m_table->selectionModel())
        return;

    const QModelIndexList selectedRows = m_table->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    const QVector<int>& visible = m_model->visibleRows();
    QVector<int> documentRows;
    for (const QModelIndex& index : selectedRows) {
        const int documentRow = visible.value(index.row(), -1);
        if (documentRow >= 0 && !documentRows.contains(documentRow))
            documentRows.append(documentRow);
    }
    if (documentRows.isEmpty())
        return;

    const QByteArray data = m_model->rowsClipboardData(documentRows);
    if (data.isEmpty())
        return;

    QStringList lines;
    for (int row : documentRows) {
        QStringList cells;
        cells.reserve(m_model->dataColumnCount());
        for (int column = 0; column < m_model->dataColumnCount(); ++column)
            cells.append(m_model->cellValue(row, column));
        lines.append(cells.join(QLatin1Char('\t')));
    }

    auto* mime = new QMimeData;
    mime->setData(rowsClipboardMimeType(), data);
    mime->setText(lines.join(QLatin1Char('\n')));
    QApplication::clipboard()->setMimeData(mime);

    statusBar()->showMessage(QStringLiteral("已复制 %1 行").arg(documentRows.size()), 3000);
}

void MainWindow::pasteFromClipboard()
{
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
        edit->paste();
        return;
    }

    if (!m_model->document())
        return;

    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (mime && mime->hasFormat(rowsClipboardMimeType())) {
        const bool rowSelection = m_table->selectionModel()
                && !m_table->selectionModel()->selectedRows().isEmpty();
        if (!rowSelection) {
            QMessageBox::information(this, QStringLiteral("粘贴"),
                QStringLiteral("行信息无法写入单元格。\n\n"
                               "请先选中整行（点击行号列或行标题），再按 Ctrl+V 追加到数据末尾。"));
            return;
        }
        pasteRowsFromClipboard(mime->data(rowsClipboardMimeType()));
        return;
    }

    pasteCellsFromClipboard();
}

void MainWindow::pasteRowsFromClipboard(const QByteArray& data)
{
    resetFilterForRowEdit();

    const int before = m_model->rowCount();

    QString error;
    if (!m_model->appendRowsFromClipboard(data, &error)) {
        QMessageBox::warning(this, QStringLiteral("粘贴失败"), error);
        return;
    }

    const int pasted = m_model->rowCount() - before;
    const int lastRow = m_model->rowCount() - 1;
    if (lastRow >= 0)
        m_table->scrollTo(m_model->index(lastRow, 0), QAbstractItemView::EnsureVisible);

    refreshDocumentViews();
    m_saveAction->setEnabled(m_model->hasUnsavedChanges());
    statusBar()->showMessage(
        QStringLiteral("已粘贴 %1 行到数据末尾").arg(pasted), 4000);
}

void MainWindow::pasteCellsFromClipboard()
{
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid())
        return;

    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return;

    const int startRow = current.row();
    const int startColumn = qMax(1, current.column());
    int pasted = 0;

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const int targetRow = startRow + lineIndex;
        if (targetRow >= m_model->rowCount())
            break;

        QString line = lines.at(lineIndex);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const QStringList cells = line.split(QLatin1Char('\t'));
        for (int cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
            const int targetColumn = startColumn + cellIndex;
            if (targetColumn >= m_model->columnCount())
                break;
            if (m_model->setData(m_model->index(targetRow, targetColumn),
                                 cells.at(cellIndex), Qt::EditRole)) {
                ++pasted;
            }
        }
    }

    if (pasted > 0)
        statusBar()->showMessage(QStringLiteral("已粘贴 %1 个单元格").arg(pasted), 3000);
}

void MainWindow::copySelection()
{
    if (!m_model->document() || !m_table->selectionModel())
        return;

    QModelIndexList indexes = m_table->selectionModel()->selectedIndexes();
    if (indexes.isEmpty()) {
        const QModelIndex current = m_table->currentIndex();
        if (current.isValid())
            indexes.append(current);
    }
    if (indexes.isEmpty())
        return;

    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex& a, const QModelIndex& b) {
        if (a.row() != b.row())
            return a.row() < b.row();
        return a.column() < b.column();
    });

    QStringList lines;
    int currentRow = -1;
    QStringList cells;

    for (const QModelIndex& index : indexes) {
        if (index.row() != currentRow) {
            if (currentRow >= 0)
                lines.append(cells.join(QLatin1Char('\t')));
            cells.clear();
            currentRow = index.row();
        }
        const QVariant value = index.column() > 0
                ? m_model->data(index, Qt::EditRole)
                : m_model->data(index, Qt::DisplayRole);
        cells.append(value.toString());
    }
    if (currentRow >= 0)
        lines.append(cells.join(QLatin1Char('\t')));

    QApplication::clipboard()->setText(lines.join(QStringLiteral("\n")));
    statusBar()->showMessage(QStringLiteral("已复制 %1 个单元格").arg(indexes.size()), 3000);
}

void MainWindow::selectAll()
{
    m_table->selectAll();
}

void MainWindow::showTableContextMenu(const QPoint& position)
{
    if (!m_model->document())
        return;

    const QModelIndex index = m_table->indexAt(position);

    const QMimeData* mime = QApplication::clipboard()->mimeData();
    const bool hasRowData = mime && mime->hasFormat(rowsClipboardMimeType());
    const QByteArray rowData = hasRowData
            ? mime->data(rowsClipboardMimeType()) : QByteArray();

    // 表格下方/右侧的空白区域：只能粘贴整行数据，不能粘贴到单元格
    if (!index.isValid()) {
        QMenu blankMenu(this);
        QAction* pasteRowsAction = blankMenu.addAction(
                    QStringLiteral("粘贴行数据到末尾"));
        pasteRowsAction->setEnabled(hasRowData);

        QAction* addRowAction = blankMenu.addAction(
                    QStringLiteral("在末尾添加空白行"));

        QAction* addColumnAction = blankMenu.addAction(
                    QStringLiteral("新增空白列"));

        QAction* chosen = blankMenu.exec(m_table->viewport()->mapToGlobal(position));
        if (chosen == pasteRowsAction && hasRowData)
            pasteRowsFromClipboard(rowData);
        else if (chosen == addRowAction)
            addBlankRow();
        else if (chosen == addColumnAction)
            addEmptyColumn();
        return;
    }

    if (m_table->selectionModel() && !m_table->selectionModel()->isSelected(index))
        m_table->setCurrentIndex(index);

    QMenu menu(this);
    QAction* copyAction = menu.addAction(QStringLiteral("复制"));
    copyAction->setEnabled(true);

    QAction* pasteAction = menu.addAction(QStringLiteral("粘贴"));
    pasteAction->setEnabled(mime && (hasRowData || mime->hasText()));

    QAction* detailAction = menu.addAction(QStringLiteral("查看字段详情"));
    detailAction->setEnabled(index.isValid() && index.column() > 0);

    menu.addSeparator();

    QAction* addColumnAction = menu.addAction(QStringLiteral("新增空白列"));
    QAction* duplicateColumnAction = menu.addAction(QStringLiteral("复制此列"));
    duplicateColumnAction->setEnabled(index.column() > 0);
    QAction* renameColumnAction = menu.addAction(QStringLiteral("重命名此列"));
    renameColumnAction->setEnabled(index.column() > 0
                                   && m_model->isExtraColumn(index.column() - 1));

    menu.addSeparator();

    QAction* duplicateAction = menu.addAction(QStringLiteral("复制选中行"));
    duplicateAction->setEnabled(m_model->rowCount() > 0);

    QAction* addAction = menu.addAction(QStringLiteral("在末尾添加空白行"));

    QAction* deleteAction = menu.addAction(QStringLiteral("删除选中行"));
    deleteAction->setEnabled(m_model->rowCount() > 0);

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (!chosen)
        return;

    if (chosen == copyAction) {
        if (m_table->selectionModel()
            && !m_table->selectionModel()->selectedRows().isEmpty()) {
            copyRowsToClipboard();
        } else {
            copySelection();
        }
    } else if (chosen == pasteAction) {
        pasteFromClipboard();
    } else if (chosen == detailAction) {
        showCellInfo(index);
    } else if (chosen == addColumnAction) {
        addEmptyColumn();
    } else if (chosen == duplicateColumnAction) {
        duplicateSelectedColumn();
    } else if (chosen == renameColumnAction) {
        renameSelectedColumn();
    } else if (chosen == duplicateAction) {
        duplicateSelectedRows();
    } else if (chosen == addAction) {
        addBlankRow();
    } else if (chosen == deleteAction) {
        deleteSelectedRows();
    }
}

void MainWindow::duplicateSelectedRows()
{
    if (!m_model->document() || m_model->rowCount() == 0)
        return;

    resetFilterForRowEdit();

    QVector<int> rows;
    if (m_table->selectionModel()) {
        const QModelIndexList selected = m_table->selectionModel()->selectedIndexes();
        for (const QModelIndex& index : selected) {
            if (index.isValid() && !rows.contains(index.row()))
                rows.append(index.row());
        }
    }
    if (rows.isEmpty()) {
        const QModelIndex current = m_table->currentIndex();
        if (current.isValid())
            rows.append(current.row());
    }
    if (rows.isEmpty())
        return;

    if (!m_model->duplicateRows(rows))
        return;

    refreshDocumentViews();
    m_saveAction->setEnabled(m_model->hasUnsavedChanges());
    statusBar()->showMessage(QStringLiteral("已复制 %1 行到下方").arg(rows.size()), 4000);
}

void MainWindow::deleteSelectedRows()
{
    if (!m_model->document() || m_model->rowCount() == 0)
        return;

    QVector<int> rows;
    if (m_table->selectionModel()) {
        const QModelIndexList selected = m_table->selectionModel()->selectedIndexes();
        for (const QModelIndex& index : selected) {
            if (index.isValid() && !rows.contains(index.row()))
                rows.append(index.row());
        }
    }
    if (rows.isEmpty()) {
        const QModelIndex current = m_table->currentIndex();
        if (current.isValid())
            rows.append(current.row());
    }
    if (rows.isEmpty())
        return;

    const QMessageBox::StandardButton reply = QMessageBox::question(
                this, QStringLiteral("删除选中行"),
                QStringLiteral("确定删除选中的 %1 行吗？\n\n删除后需要保存才会写入文件。")
                    .arg(rows.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    const int firstRow = *std::min_element(rows.constBegin(), rows.constEnd());
    if (!m_model->deleteRows(rows))
        return;

    if (m_model->rowCount() > 0) {
        const int target = qBound(0, firstRow, m_model->rowCount() - 1);
        m_table->selectRow(target);
    }

    refreshDocumentViews();
    m_saveAction->setEnabled(m_model->hasUnsavedChanges());
    statusBar()->showMessage(
        QStringLiteral("已删除 %1 行，保存后写入文件").arg(rows.size()), 4000);
}

void MainWindow::addBlankRow()
{
    if (!m_model->document())
        return;

    resetFilterForRowEdit();
    if (!m_model->appendBlankRow())
        return;

    const int lastRow = m_model->rowCount() - 1;
    if (lastRow >= 0)
        m_table->scrollTo(m_model->index(lastRow, 0), QAbstractItemView::EnsureVisible);

    refreshDocumentViews();
    m_saveAction->setEnabled(m_model->hasUnsavedChanges());
    statusBar()->showMessage(
        QStringLiteral("已在末尾添加空白行，双击单元格填写内容"), 4000);
}

void MainWindow::addEmptyColumn()
{
    if (!m_model->document())
        return;

    QString name;
    if (!m_model->appendEmptyColumn(&name))
        return;

    autoSizeColumns();
    fillOverviewTab();
    statusBar()->showMessage(
        QStringLiteral("已新增列“%1”（仅导出时保留，不写入接口文件）").arg(name), 5000);
}

void MainWindow::duplicateSelectedColumn()
{
    if (!m_model->document())
        return;

    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid() || current.column() <= 0)
        return;

    QString name;
    if (!m_model->duplicateColumn(current.column() - 1, &name))
        return;

    autoSizeColumns();
    fillOverviewTab();
    statusBar()->showMessage(
        QStringLiteral("已复制列“%1”（仅导出时保留，不写入接口文件）").arg(name), 5000);
}

void MainWindow::renameSelectedColumn()
{
    if (!m_model->document())
        return;

    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid() || current.column() <= 0)
        return;

    const int dataColumn = current.column() - 1;
    if (!m_model->isExtraColumn(dataColumn))
        return;

    bool ok = false;
    const QString oldName = m_model->dataColumnHeader(dataColumn);
    const QString name = QInputDialog::getText(
                this, QStringLiteral("重命名列"), QStringLiteral("列名："),
                QLineEdit::Normal, oldName, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    if (m_model->renameColumn(current.column() - 1, name))
        statusBar()->showMessage(QStringLiteral("列已重命名为“%1”").arg(name.trimmed()), 4000);
}

void MainWindow::resetFilterForRowEdit()
{
    if (m_filterEdit->text().isEmpty() && m_model->filterText().isEmpty())
        return;

    const QSignalBlocker blocker(m_filterEdit);
    m_filterEdit->clear();
    m_model->setFilter(QString(), false);
    updateCounts();
}
void MainWindow::showCellInfo(const QModelIndex& index)
{
    const Document* doc = m_model->document();
    if (!doc || !index.isValid())
        return;

    if (index.column() == 0)
        return;

    const int fieldIndex = index.column() - 1;
    const int documentRow = m_model->visibleRows().value(index.row());

    if (m_model->isExtraColumn(fieldIndex)) {
        QMessageBox::information(this, QStringLiteral("新增列"),
            QStringLiteral("列名：%1\n值：%2\n源行号：%3\n\n"
                           "新增列仅用于导出 CSV/JSON，不会写入接口文件。")
                .arg(m_model->dataColumnHeader(fieldIndex),
                     m_model->cellValue(documentRow, fieldIndex))
                .arg(doc->sourceLines.value(documentRow)));
        return;
    }

    if (fieldIndex < 0 || fieldIndex >= doc->definition.fields.size())
        return;

    const FieldDefinition& field = doc->definition.fields.at(fieldIndex);
    const QString value = doc->value(documentRow, fieldIndex);

    QString details = QStringLiteral("原始值：%1\n").arg(value);
    if (doc->definition.dictionaries) {
        auto it = doc->definition.dictionaries->constFind(field.nameEn);
        if (it == doc->definition.dictionaries->constEnd())
            it = doc->definition.dictionaries->constFind(field.nameCn);
        if (it != doc->definition.dictionaries->constEnd()) {
            const auto meaning = it->constFind(value);
            if (meaning != it->constEnd())
                details += QStringLiteral("字典含义：%1\n").arg(*meaning);
            details += QStringLiteral("\n可用字典项：\n");
            int count = 0;
            for (auto dictIt = it->constBegin(); dictIt != it->constEnd() && count < 30;
                 ++dictIt, ++count) {
                details += QStringLiteral("  %1 = %2\n").arg(dictIt.key(), dictIt.value());
            }
            if (it->size() > 30)
                details += QStringLiteral("  …… 共 %1 项\n").arg(it->size());
        }
    }
    if (!field.tip.isEmpty())
        details += QStringLiteral("\n字段说明：%1").arg(field.tip);

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("字段详情"));
    box.setIcon(QMessageBox::Information);
    box.setText(QStringLiteral("%1（%2）").arg(field.nameCn, field.nameEn));
    box.setInformativeText(QStringLiteral(
        "第 %1 个字段 · 类型 %2 · 长度 %3 · 小数位 %4\n源行号 %5")
        .arg(field.index).arg(field.type).arg(field.length).arg(field.decimals)
        .arg(doc->sourceLines.value(documentRow)));
    box.setDetailedText(details);
    box.exec();
}

void MainWindow::showAbout()
{
    const QString version =
#ifdef RFV_PROJECT_VERSION
            QStringLiteral(RFV_PROJECT_VERSION);
#else
            QStringLiteral("1.0.0");
#endif

    QMessageBox::about(this, QStringLiteral("关于 RegFileViewer"),
        QStringLiteral(
            "<h3>RegFileViewer %1</h3>"
            "<p>中登开放式基金业务数据交换协议（V2.0 / V2.1 / V2.2）与"
            "理财登数据文件的阅读工具。</p>"
            "<p>基于 Qt 5.15.2 / C++ 开发，支持 Windows 7 SP1 及以上系统。</p>"
            "<p style='color:#9aa3b2;font-size:8pt'>接口解析配置基于 FFReader（LGPL-3.0）</p>")
        .arg(version));
}

void MainWindow::showFileTypeReference(FileTypeReferenceKind kind)
{
    FileTypeReferenceDialog dialog(*m_repository, kind, this);
    dialog.exec();
}

void MainWindow::loadRecentFiles()
{
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
    refreshRecentMenu();
}

void MainWindow::addRecentFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 10)
        m_recentFiles.removeLast();

    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    refreshRecentMenu();
}

void MainWindow::refreshRecentMenu()
{
    if (!m_recentMenu)
        return;

    m_recentMenu->clear();
    if (m_recentFiles.isEmpty()) {
        QAction* empty = m_recentMenu->addAction(QStringLiteral("（无）"));
        empty->setEnabled(false);
        return;
    }

    for (const QString& path : m_recentFiles) {
        QAction* action = m_recentMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        action->setData(path);
        connect(action, &QAction::triggered, this, [this, path]() { openFile(path); });
    }

    m_recentMenu->addSeparator();
    QAction* clearAction = m_recentMenu->addAction(QStringLiteral("清除最近记录"));
    connect(clearAction, &QAction::triggered, this, [this]() {
        m_recentFiles.clear();
        QSettings().remove(QStringLiteral("recentFiles"));
        refreshRecentMenu();
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;

    const QString path = urls.first().toLocalFile();
    if (!path.isEmpty()) {
        event->acceptProposedAction();
        openFile(path);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSaveChanges()) {
        event->ignore();
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("windowGeometry"), saveGeometry());
    QMainWindow::closeEvent(event);
}

} // namespace rfv
