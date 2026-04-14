// QuestionEditorDialog.cpp — ProctorOps

#include "dialogs/QuestionEditorDialog.h"
#include "app/AppContext.h"
#include "services/QuestionService.h"
#include "utils/Validation.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QMessageBox>

QuestionEditorDialog::QuestionEditorDialog(AppContext& ctx,
                                            const QString& questionId,
                                            QWidget* parent)
    : QDialog(parent)
    , m_ctx(ctx)
    , m_questionId(questionId)
    , m_editMode(!questionId.isEmpty())
{
    setWindowTitle(m_editMode
        ? QStringLiteral("Edit Question")
        : QStringLiteral("New Question"));
    setMinimumSize(680, 560);

    setupUi();

    if (m_editMode) {
        loadQuestion(questionId);
        loadKpMappings(questionId);
        loadTagMappings(questionId);
    } else {
        // Create mode defaults
        m_difficultySpin->setValue(3);
        m_discriminationSpin->setValue(0.50);
        m_statusCombo->setCurrentIndex(0); // Draft
    }
}

void QuestionEditorDialog::setupUi()
{
    auto* root = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    // ── Tab: Question ─────────────────────────────────────────────────────────
    auto* mainTab    = new QWidget(tabs);
    auto* mainLayout = new QVBoxLayout(mainTab);
    auto* form       = new QFormLayout;

    m_bodyEdit = new QTextEdit(mainTab);
    m_bodyEdit->setPlaceholderText(QStringLiteral("Enter the question text (max 4000 characters)"));
    m_bodyEdit->setMinimumHeight(100);
    form->addRow(QStringLiteral("Question text:"), m_bodyEdit);

    m_statusCombo = new QComboBox(mainTab);
    m_statusCombo->addItem(QStringLiteral("Draft"),    static_cast<int>(QuestionStatus::Draft));
    m_statusCombo->addItem(QStringLiteral("Active"),   static_cast<int>(QuestionStatus::Active));
    m_statusCombo->addItem(QStringLiteral("Archived"), static_cast<int>(QuestionStatus::Archived));
    form->addRow(QStringLiteral("Status:"), m_statusCombo);

    m_difficultySpin = new QSpinBox(mainTab);
    m_difficultySpin->setRange(Validation::DifficultyMin, Validation::DifficultyMax);
    form->addRow(QStringLiteral("Difficulty (1–5):"), m_difficultySpin);

    m_discriminationSpin = new QDoubleSpinBox(mainTab);
    m_discriminationSpin->setRange(Validation::DiscriminationMin, Validation::DiscriminationMax);
    m_discriminationSpin->setSingleStep(0.05);
    m_discriminationSpin->setDecimals(2);
    form->addRow(QStringLiteral("Discrimination (0.00–1.00):"), m_discriminationSpin);

    m_externalIdEdit = new QLineEdit(mainTab);
    m_externalIdEdit->setPlaceholderText(QStringLiteral("Optional import identifier"));
    form->addRow(QStringLiteral("External ID:"), m_externalIdEdit);

    mainLayout->addLayout(form);
    tabs->addTab(mainTab, QStringLiteral("Question"));

    // ── Tab: Answer Options ────────────────────────────────────────────────────
    auto* answersTab    = new QWidget(tabs);
    auto* answersLayout = new QVBoxLayout(answersTab);

    auto* answerHint = new QLabel(
        QStringLiteral("Add 2 to 6 answer options. Select the correct answer below."),
        answersTab);
    answerHint->setWordWrap(true);
    answersLayout->addWidget(answerHint);

    m_answerList = new QListWidget(answersTab);
    m_answerList->setDragDropMode(QAbstractItemView::InternalMove);
    answersLayout->addWidget(m_answerList, 1);

    auto* answerBtns = new QHBoxLayout;
    auto* addAnswerBtn = new QPushButton(QStringLiteral("Add Option"), answersTab);
    auto* removeAnswerBtn = new QPushButton(QStringLiteral("Remove Selected"), answersTab);
    answerBtns->addWidget(addAnswerBtn);
    answerBtns->addWidget(removeAnswerBtn);
    answerBtns->addStretch();
    answersLayout->addLayout(answerBtns);

    auto* correctForm = new QFormLayout;
    m_correctAnswerCombo = new QComboBox(answersTab);
    correctForm->addRow(QStringLiteral("Correct answer:"), m_correctAnswerCombo);
    answersLayout->addLayout(correctForm);

    connect(addAnswerBtn,    &QPushButton::clicked, this, &QuestionEditorDialog::onAddAnswer);
    connect(removeAnswerBtn, &QPushButton::clicked, this, &QuestionEditorDialog::onRemoveAnswer);
    connect(m_answerList->model(), &QAbstractItemModel::rowsInserted,
            this, &QuestionEditorDialog::onAnswerListChanged);
    connect(m_answerList->model(), &QAbstractItemModel::rowsRemoved,
            this, &QuestionEditorDialog::onAnswerListChanged);
    connect(m_answerList, &QListWidget::itemChanged,
            this, &QuestionEditorDialog::onAnswerListChanged);

    tabs->addTab(answersTab, QStringLiteral("Answers"));

    // ── Tab: Knowledge Point Mappings ─────────────────────────────────────────
    auto* kpTab    = new QWidget(tabs);
    auto* kpLayout = new QVBoxLayout(kpTab);

    m_kpMappingList = new QListWidget(kpTab);
    kpLayout->addWidget(m_kpMappingList, 1);

    loadKpTree();

    auto* kpAddRow = new QHBoxLayout;
    m_kpTreeCombo = new QComboBox(kpTab);
    kpAddRow->addWidget(m_kpTreeCombo, 1);
    auto* addKpBtn    = new QPushButton(QStringLiteral("Add Mapping"), kpTab);
    auto* removeKpBtn = new QPushButton(QStringLiteral("Remove Selected"), kpTab);
    kpAddRow->addWidget(addKpBtn);
    kpAddRow->addWidget(removeKpBtn);
    kpLayout->addLayout(kpAddRow);

    connect(addKpBtn,    &QPushButton::clicked, this, &QuestionEditorDialog::onAddKpMapping);
    connect(removeKpBtn, &QPushButton::clicked, this, &QuestionEditorDialog::onRemoveKpMapping);

    tabs->addTab(kpTab, QStringLiteral("Knowledge Points"));

    // ── Tab: Tags ─────────────────────────────────────────────────────────────
    auto* tagTab    = new QWidget(tabs);
    auto* tagLayout = new QVBoxLayout(tagTab);

    m_tagList = new QListWidget(tagTab);
    tagLayout->addWidget(m_tagList, 1);

    auto* tagAddRow   = new QHBoxLayout;
    m_tagNameEdit = new QLineEdit(tagTab);
    m_tagNameEdit->setPlaceholderText(QStringLiteral("Tag name (creates new if not found)"));
    auto* addTagBtn    = new QPushButton(QStringLiteral("Add Tag"), tagTab);
    auto* removeTagBtn = new QPushButton(QStringLiteral("Remove Selected"), tagTab);
    tagAddRow->addWidget(m_tagNameEdit, 1);
    tagAddRow->addWidget(addTagBtn);
    tagAddRow->addWidget(removeTagBtn);
    tagLayout->addLayout(tagAddRow);

    connect(addTagBtn,    &QPushButton::clicked, this, &QuestionEditorDialog::onAddTag);
    connect(removeTagBtn, &QPushButton::clicked, this, &QuestionEditorDialog::onRemoveTag);

    tabs->addTab(tagTab, QStringLiteral("Tags"));

    root->addWidget(tabs, 1);

    // Error label
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    // Buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QuestionEditorDialog::onSave);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void QuestionEditorDialog::loadKpTree()
{
    auto treeResult = m_ctx.questionService->getTree();
    if (!treeResult.isOk()) return;
    for (const auto& kp : treeResult.value()) {
        m_kpTreeCombo->addItem(kp.path, kp.id);
    }
}

void QuestionEditorDialog::loadQuestion(const QString& questionId)
{
    auto result = m_ctx.questionService->getQuestion(questionId);
    if (!result.isOk()) return;
    const auto& q = result.value();

    m_bodyEdit->setPlainText(q.bodyText);
    m_externalIdEdit->setText(q.externalId);
    m_difficultySpin->setValue(q.difficulty);
    m_discriminationSpin->setValue(q.discrimination);

    // Status combo
    for (int i = 0; i < m_statusCombo->count(); ++i) {
        if (m_statusCombo->itemData(i).toInt() == static_cast<int>(q.status)) {
            m_statusCombo->setCurrentIndex(i);
            break;
        }
    }

    // Answer options
    m_answerList->clear();
    for (const auto& opt : q.answerOptions) {
        auto* item = new QListWidgetItem(opt, m_answerList);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    populateCorrectAnswerCombo();
    if (q.correctAnswerIndex >= 0 && q.correctAnswerIndex < m_correctAnswerCombo->count())
        m_correctAnswerCombo->setCurrentIndex(q.correctAnswerIndex);
}

void QuestionEditorDialog::loadKpMappings(const QString& questionId)
{
    auto result = m_ctx.questionService->getQuestionKPMappings(questionId);
    if (!result.isOk()) return;
    m_kpMappingList->clear();
    for (const auto& mapping : result.value()) {
        auto* item = new QListWidgetItem(mapping.knowledgePointId, m_kpMappingList);
        item->setData(Qt::UserRole, mapping.knowledgePointId);
    }
}

void QuestionEditorDialog::loadTagMappings(const QString& questionId)
{
    auto result = m_ctx.questionService->getQuestionTagMappings(questionId);
    if (!result.isOk()) return;
    m_tagList->clear();
    for (const auto& mapping : result.value()) {
        auto* item = new QListWidgetItem(mapping.tagId, m_tagList);
        item->setData(Qt::UserRole, mapping.tagId);
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void QuestionEditorDialog::onSave()
{
    m_errorLabel->hide();

    // Build the Question struct from form values
    Question q;
    q.id           = m_questionId;
    q.bodyText     = m_bodyEdit->toPlainText();
    q.externalId   = m_externalIdEdit->text().trimmed();
    q.difficulty   = m_difficultySpin->value();
    q.discrimination = m_discriminationSpin->value();
    q.status       = static_cast<QuestionStatus>(m_statusCombo->currentData().toInt());
    q.correctAnswerIndex = m_correctAnswerCombo->currentIndex();

    q.answerOptions.clear();
    for (int i = 0; i < m_answerList->count(); ++i)
        q.answerOptions << m_answerList->item(i)->text();

    // Save via service
    Result<Question> result = m_editMode
        ? m_ctx.questionService->updateQuestion(q, m_ctx.session.userId)
        : m_ctx.questionService->createQuestion(q, m_ctx.session.userId);

    if (!result.isOk()) {
        showError(result.errorMessage());
        return;
    }

    const QString savedId = result.value().id;

    // Apply pending KP mapping changes
    for (const auto& kpId : std::as_const(m_kpIdsToAdd)) {
        auto mapResult = m_ctx.questionService->mapQuestionToKP(savedId, kpId, m_ctx.session.userId);
        if (!mapResult.isOk()) {
            showError(QStringLiteral("Failed to map knowledge point: ") + mapResult.errorMessage());
            return;
        }
    }
    for (const auto& kpId : std::as_const(m_kpIdsToRemove)) {
        auto unmapResult = m_ctx.questionService->unmapQuestionFromKP(savedId, kpId, m_ctx.session.userId);
        if (!unmapResult.isOk()) {
            showError(QStringLiteral("Failed to unmap knowledge point: ") + unmapResult.errorMessage());
            return;
        }
    }

    // Apply pending tag changes
    for (const auto& tagId : std::as_const(m_tagIdsToAdd)) {
        auto applyTagResult = m_ctx.questionService->applyTag(savedId, tagId, m_ctx.session.userId);
        if (!applyTagResult.isOk()) {
            showError(QStringLiteral("Failed to apply tag: ") + applyTagResult.errorMessage());
            return;
        }
    }
    for (const auto& tagId : std::as_const(m_tagIdsToRemove)) {
        auto removeTagResult = m_ctx.questionService->removeTag(savedId, tagId, m_ctx.session.userId);
        if (!removeTagResult.isOk()) {
            showError(QStringLiteral("Failed to remove tag: ") + removeTagResult.errorMessage());
            return;
        }
    }

    accept();
}

void QuestionEditorDialog::onAddAnswer()
{
    if (m_answerList->count() >= Validation::AnswerOptionMaxCount) {
        showError(QStringLiteral("Maximum %1 answer options allowed.")
            .arg(Validation::AnswerOptionMaxCount));
        return;
    }
    auto* item = new QListWidgetItem(
        QStringLiteral("Answer option %1").arg(m_answerList->count() + 1),
        m_answerList);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_answerList->editItem(item);
}

void QuestionEditorDialog::onRemoveAnswer()
{
    if (m_answerList->count() <= Validation::AnswerOptionMinCount) {
        showError(QStringLiteral("At least %1 answer options required.")
            .arg(Validation::AnswerOptionMinCount));
        return;
    }
    const auto items = m_answerList->selectedItems();
    for (auto* item : items) delete item;
    populateCorrectAnswerCombo();
}

void QuestionEditorDialog::onAnswerListChanged()
{
    populateCorrectAnswerCombo();
}

void QuestionEditorDialog::onAddKpMapping()
{
    const QString kpId = m_kpTreeCombo->currentData().toString();
    if (kpId.isEmpty()) return;

    // Check for duplicate in current list
    for (int i = 0; i < m_kpMappingList->count(); ++i) {
        if (m_kpMappingList->item(i)->data(Qt::UserRole).toString() == kpId) return;
    }

    const QString kpPath = m_kpTreeCombo->currentText();
    auto* item = new QListWidgetItem(kpPath, m_kpMappingList);
    item->setData(Qt::UserRole, kpId);
    m_kpIdsToAdd << kpId;
    m_kpIdsToRemove.removeAll(kpId);
}

void QuestionEditorDialog::onRemoveKpMapping()
{
    const auto items = m_kpMappingList->selectedItems();
    for (auto* item : items) {
        const QString kpId = item->data(Qt::UserRole).toString();
        m_kpIdsToRemove << kpId;
        m_kpIdsToAdd.removeAll(kpId);
        delete item;
    }
}

void QuestionEditorDialog::onAddTag()
{
    const QString tagName = m_tagNameEdit->text().trimmed();
    if (tagName.isEmpty()) return;

    // Try to find existing tag or create new one
    auto listResult = m_ctx.questionService->listTags();
    QString tagId;
    if (listResult.isOk()) {
        for (const auto& tag : listResult.value()) {
            if (tag.name.compare(tagName, Qt::CaseInsensitive) == 0) {
                tagId = tag.id;
                break;
            }
        }
    }

    if (tagId.isEmpty()) {
        // Create new tag
        auto createResult = m_ctx.questionService->createTag(tagName, m_ctx.session.userId);
        if (!createResult.isOk()) {
            showError(QStringLiteral("Could not create tag: ") + createResult.errorMessage());
            return;
        }
        tagId = createResult.value().id;
    }

    // Check for duplicate
    for (int i = 0; i < m_tagList->count(); ++i) {
        if (m_tagList->item(i)->data(Qt::UserRole).toString() == tagId) return;
    }

    auto* item = new QListWidgetItem(tagName, m_tagList);
    item->setData(Qt::UserRole, tagId);
    m_tagIdsToAdd << tagId;
    m_tagIdsToRemove.removeAll(tagId);
    m_tagNameEdit->clear();
}

void QuestionEditorDialog::onRemoveTag()
{
    const auto items = m_tagList->selectedItems();
    for (auto* item : items) {
        const QString tagId = item->data(Qt::UserRole).toString();
        m_tagIdsToRemove << tagId;
        m_tagIdsToAdd.removeAll(tagId);
        delete item;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void QuestionEditorDialog::populateCorrectAnswerCombo()
{
    const int prev = m_correctAnswerCombo->currentIndex();
    m_correctAnswerCombo->clear();
    for (int i = 0; i < m_answerList->count(); ++i) {
        m_correctAnswerCombo->addItem(
            QStringLiteral("Option %1: %2").arg(i + 1).arg(m_answerList->item(i)->text().left(60)));
    }
    if (prev >= 0 && prev < m_correctAnswerCombo->count())
        m_correctAnswerCombo->setCurrentIndex(prev);
}

void QuestionEditorDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}
