// QuestionBankWindow.cpp — ProctorOps

#include "windows/QuestionBankWindow.h"
#include "dialogs/QuestionEditorDialog.h"
#include "app/AppContext.h"
#include "services/QuestionService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>

static constexpr int kPageSize = 100;

QuestionBankWindow::QuestionBankWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QLatin1String(WindowId));
    setWindowTitle(QStringLiteral("Question Bank"));
    setupUi();
    QTimer::singleShot(0, this, [this] { loadQuestions(); });
}

void QuestionBankWindow::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(6);

    setupToolbar();
    root->addWidget(findChild<QToolBar*>());

    setupFilterPanel();
    root->addWidget(m_filterBox);

    // Table
    m_model = new QStandardItemModel(0, 5, this);
    m_model->setHorizontalHeaderLabels({
        QStringLiteral("Body Text"),
        QStringLiteral("Difficulty"),
        QStringLiteral("Discrimination"),
        QStringLiteral("Status"),
        QStringLiteral("ID"),
    });

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setStretchLastSection(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tableView->setColumnWidth(1, 80);
    m_tableView->setColumnWidth(2, 110);
    m_tableView->setColumnWidth(3, 90);
    m_tableView->setColumnHidden(4, true);   // ID column hidden; accessed via data()
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_tableView, &QTableView::doubleClicked,
            this, &QuestionBankWindow::onTableDoubleClicked);
    connect(m_tableView, &QTableView::customContextMenuRequested,
            this, &QuestionBankWindow::onContextMenu);

    root->addWidget(m_tableView, 1);

    // Footer
    auto* footer = new QHBoxLayout;
    m_statusLabel = new QLabel(QStringLiteral("0 questions"), this);
    m_loadMoreBtn = new QPushButton(QStringLiteral("Load More"), this);
    m_loadMoreBtn->setVisible(false);
    footer->addWidget(m_statusLabel);
    footer->addStretch();
    footer->addWidget(m_loadMoreBtn);
    root->addLayout(footer);

    connect(m_loadMoreBtn, &QPushButton::clicked, this, &QuestionBankWindow::onLoadMore);
}

void QuestionBankWindow::setupToolbar()
{
    auto* toolbar = new QToolBar(this);
    toolbar->setObjectName(QStringLiteral("QuestionBankToolbar"));

    auto* newAct    = toolbar->addAction(QStringLiteral("New"),     this, &QuestionBankWindow::onNewQuestion);
    auto* editAct   = toolbar->addAction(QStringLiteral("Edit"),    this, &QuestionBankWindow::onEditQuestion);
    auto* deleteAct = toolbar->addAction(QStringLiteral("Delete"),  this, &QuestionBankWindow::onDeleteQuestion);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Refresh"), this, &QuestionBankWindow::onRefresh);

    auto* filterToggle = new QPushButton(QStringLiteral("▾ Filter"), toolbar);
    filterToggle->setCheckable(true);
    filterToggle->setChecked(true);
    toolbar->addWidget(filterToggle);

    // The filter toggle will be wired after setupFilterPanel creates m_filterBox
    // Store connection lambda for later binding
    connect(filterToggle, &QPushButton::toggled, this, [this](bool checked) {
        if (m_filterBox) m_filterBox->setVisible(checked);
    });

    Q_UNUSED(newAct); Q_UNUSED(editAct); Q_UNUSED(deleteAct);
}

void QuestionBankWindow::setupFilterPanel()
{
    m_filterBox = new QGroupBox(QStringLiteral("Filter"), this);
    auto* layout = new QFormLayout(m_filterBox);

    m_diffMinSpin = new QSpinBox(m_filterBox);
    m_diffMinSpin->setRange(1, 5);
    m_diffMinSpin->setValue(1);
    m_diffMaxSpin = new QSpinBox(m_filterBox);
    m_diffMaxSpin->setRange(1, 5);
    m_diffMaxSpin->setValue(5);
    auto* diffRow = new QHBoxLayout;
    diffRow->addWidget(m_diffMinSpin);
    diffRow->addWidget(new QLabel(QStringLiteral("–"), m_filterBox));
    diffRow->addWidget(m_diffMaxSpin);
    layout->addRow(QStringLiteral("Difficulty:"), diffRow);

    m_kpCombo = new QComboBox(m_filterBox);
    m_kpCombo->addItem(QStringLiteral("(all topics)"), QString{});
    // Populate from service
    auto treeResult = m_ctx.questionService->getTree();
    if (treeResult.isOk()) {
        for (const auto& kp : treeResult.value()) {
            m_kpCombo->addItem(kp.path, kp.id);
        }
    }
    layout->addRow(QStringLiteral("Topic (KP):"), m_kpCombo);

    m_textSearchEdit = new QLineEdit(m_filterBox);
    m_textSearchEdit->setPlaceholderText(QStringLiteral("Search question text…"));
    layout->addRow(QStringLiteral("Text search:"), m_textSearchEdit);

    m_statusCombo = new QComboBox(m_filterBox);
    m_statusCombo->addItem(QStringLiteral("Active"),   static_cast<int>(QuestionStatus::Active));
    m_statusCombo->addItem(QStringLiteral("Draft"),    static_cast<int>(QuestionStatus::Draft));
    m_statusCombo->addItem(QStringLiteral("Archived"), static_cast<int>(QuestionStatus::Archived));
    layout->addRow(QStringLiteral("Status:"), m_statusCombo);

    m_discMinSpin = new QDoubleSpinBox(m_filterBox);
    m_discMinSpin->setRange(0.00, 1.00);
    m_discMinSpin->setSingleStep(0.01);
    m_discMinSpin->setDecimals(2);
    m_discMinSpin->setValue(0.00);
    m_discMaxSpin = new QDoubleSpinBox(m_filterBox);
    m_discMaxSpin->setRange(0.00, 1.00);
    m_discMaxSpin->setSingleStep(0.01);
    m_discMaxSpin->setDecimals(2);
    m_discMaxSpin->setValue(1.00);
    auto* discRow = new QHBoxLayout;
    discRow->addWidget(m_discMinSpin);
    discRow->addWidget(new QLabel(QStringLiteral("–"), m_filterBox));
    discRow->addWidget(m_discMaxSpin);
    layout->addRow(QStringLiteral("Discrimination:"), discRow);

    m_tagCombo = new QComboBox(m_filterBox);
    m_tagCombo->addItem(QStringLiteral("(all tags)"), QString{});
    if (m_ctx.questionService) {
        auto tagsResult = m_ctx.questionService->listTags();
        if (tagsResult.isOk()) {
            for (const auto& tag : tagsResult.value())
                m_tagCombo->addItem(tag.name, tag.id);
        }
    }
    layout->addRow(QStringLiteral("Tag:"), m_tagCombo);

    auto* applyBtn = new QPushButton(QStringLiteral("Apply Filter"), m_filterBox);
    layout->addRow(QString{}, applyBtn);
    connect(applyBtn, &QPushButton::clicked, this, &QuestionBankWindow::onFilterChanged);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void QuestionBankWindow::onNewQuestion()
{
    openEditor();
}

void QuestionBankWindow::onEditQuestion()
{
    openEditor(selectedQuestionId());
}

void QuestionBankWindow::onDeleteQuestion()
{
    deleteSelected();
}

void QuestionBankWindow::onRefresh()
{
    m_currentOffset = 0;
    loadQuestions();
}

void QuestionBankWindow::onFilterChanged()
{
    m_currentOffset = 0;
    loadQuestions();
}

void QuestionBankWindow::onTableDoubleClicked(const QModelIndex& index)
{
    Q_UNUSED(index);
    openEditor(selectedQuestionId());
}

void QuestionBankWindow::onContextMenu(const QPoint& pos)
{
    const QString qid = selectedQuestionId();
    if (qid.isEmpty()) return;

    QMenu menu(this);
    menu.addAction(QStringLiteral("Edit"),   this, &QuestionBankWindow::onEditQuestion);
    menu.addAction(QStringLiteral("Delete"), this, &QuestionBankWindow::onDeleteQuestion);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Map to Knowledge Point"),
                   this, &QuestionBankWindow::mapSelectedToKnowledgePoint);
    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void QuestionBankWindow::mapSelectedToKnowledgePoint()
{
    const QString qid = selectedQuestionId();
    if (qid.isEmpty()) return;

    // Present knowledge-point tree in an input dialog for selection
    auto treeResult = m_ctx.questionService->getTree();
    if (treeResult.isErr()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to load knowledge points: %1")
                                 .arg(treeResult.errorMessage()));
        return;
    }

    const auto& kpList = treeResult.value();
    if (kpList.isEmpty()) {
        QMessageBox::information(this, tr("No Knowledge Points"),
                                 tr("No knowledge points are available. "
                                    "Create one first via Content Management."));
        return;
    }

    QStringList items;
    QStringList ids;
    for (const auto& kp : kpList) {
        items << (kp.path.isEmpty() ? kp.name : kp.path);
        ids   << kp.id;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Map to Knowledge Point"),
        tr("Select knowledge point for question:"), items, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    const int idx = items.indexOf(chosen);
    if (idx < 0) return;

    auto result = m_ctx.questionService->mapQuestionToKP(
        qid, ids.at(idx), m_ctx.session.userId);
    if (result.isErr()) {
        QMessageBox::warning(this, tr("Mapping Failed"), result.errorMessage());
    } else {
        if (m_statusLabel)
            m_statusLabel->setText(tr("Knowledge point mapping created successfully."));
    }
}

void QuestionBankWindow::activateFilter()
{
    if (m_filterBox) {
        m_filterBox->setVisible(true);
        if (m_textSearchEdit)
            m_textSearchEdit->setFocus();
    }
}

void QuestionBankWindow::onLoadMore()
{
    loadQuestions(true);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void QuestionBankWindow::buildQuestionFilter(QuestionFilter& f) const
{
    f.difficultyMin = m_diffMinSpin->value();
    f.difficultyMax = m_diffMaxSpin->value();
    f.textSearch    = m_textSearchEdit->text().trimmed();
    f.statusFilter  = static_cast<QuestionStatus>(m_statusCombo->currentData().toInt());
    f.knowledgePointId = m_kpCombo->currentData().toString();

    // Discrimination bounds — only set when the range is narrower than full (0.00–1.00).
    const double discMin = m_discMinSpin->value();
    const double discMax = m_discMaxSpin->value();
    if (discMin > 0.0 || discMax < 1.0) {
        f.discriminationMin = discMin;
        f.discriminationMax = discMax;
    }

    // Tag filter — single-select; "(all tags)" has empty string as data.
    const QString tagId = m_tagCombo->currentData().toString();
    if (!tagId.isEmpty())
        f.tagIds = {tagId};

    f.limit  = kPageSize;
    f.offset = m_currentOffset;
}

void QuestionBankWindow::loadQuestions(bool append)
{
    QuestionFilter filter;
    buildQuestionFilter(filter);

    auto result = m_ctx.questionService->queryQuestions(filter);
    if (!result.isOk()) {
        m_statusLabel->setText(QStringLiteral("Error loading questions: ") + result.errorMessage());
        return;
    }

    populateTable(result.value(), append);

    const int total = append
        ? static_cast<int>(m_questions.size())
        : static_cast<int>(result.value().size());

    m_statusLabel->setText(QStringLiteral("%1 question(s)").arg(m_questions.size()));
    m_loadMoreBtn->setVisible(static_cast<int>(result.value().size()) == kPageSize);
    if (append)
        m_currentOffset += result.value().size();
    else
        m_currentOffset = result.value().size();

    Q_UNUSED(total);
}

void QuestionBankWindow::populateTable(const QList<Question>& questions, bool append)
{
    if (!append) {
        m_model->removeRows(0, m_model->rowCount());
        m_questions.clear();
    }

    for (const auto& q : questions) {
        m_questions.append(q);
        QList<QStandardItem*> row;

        // Truncate body text for display
        QString body = q.bodyText;
        if (body.length() > 120)
            body = body.left(117) + QStringLiteral("…");
        body.replace(QLatin1Char('\n'), QLatin1Char(' '));

        row << new QStandardItem(body);
        row << new QStandardItem(QString::number(q.difficulty));
        row << new QStandardItem(QString::number(q.discrimination, 'f', 2));

        QString statusStr;
        switch (q.status) {
        case QuestionStatus::Draft:    statusStr = QStringLiteral("Draft");    break;
        case QuestionStatus::Active:   statusStr = QStringLiteral("Active");   break;
        case QuestionStatus::Archived: statusStr = QStringLiteral("Archived"); break;
        case QuestionStatus::Deleted:  statusStr = QStringLiteral("Deleted");  break;
        }
        row << new QStandardItem(statusStr);
        row << new QStandardItem(q.id);   // hidden ID column

        for (auto* item : row) item->setEditable(false);
        m_model->appendRow(row);
    }
}

QString QuestionBankWindow::selectedQuestionId() const
{
    const auto selection = m_tableView->selectionModel()->selectedRows();
    if (selection.isEmpty()) return {};
    const int row = selection.first().row();
    return m_model->item(row, 4)->text();   // hidden ID column
}

void QuestionBankWindow::openEditor(const QString& questionId)
{
    QuestionEditorDialog dlg(m_ctx, questionId, this);
    if (dlg.exec() == QDialog::Accepted) {
        onRefresh();
    }
}

void QuestionBankWindow::deleteSelected()
{
    const QString qid = selectedQuestionId();
    if (qid.isEmpty()) return;

    const auto reply = QMessageBox::question(this,
        QStringLiteral("Delete Question"),
        QStringLiteral("Soft-delete this question? It will no longer appear in query results."),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    auto result = m_ctx.questionService->deleteQuestion(qid, m_ctx.session.userId);
    if (!result.isOk()) {
        QMessageBox::warning(this,
            QStringLiteral("Delete Failed"),
            result.errorMessage());
        return;
    }
    onRefresh();
}
